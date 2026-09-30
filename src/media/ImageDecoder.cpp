#include "ImageDecoder.h"
#include "HeifDecoder.h"
#include "RawProcessing.h"
#include <QColorSpace>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QSvgRenderer>
#include <exiv2/exiv2.hpp>
#include <libraw/libraw.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
QString tr(const char* text) { return QCoreApplication::translate("ImageDecoder", text); }
constexpr qint64 maximumPixels = 100000000;
constexpr qint64 maximumFileBytes = 512LL * 1024 * 1024;
const QStringList rawExtensions = QStringLiteral(
    "3fr arw bay cap cr2 cr3 crw dcr dcs dng drf eip erf fff gpr iiq k25 kdc mdc mef mos mrw nef nrw obm orf pef ptx pxn r3d raf raw rdc rw2 rwl rwz sr2 srf srw sti x3f")
    .split(' ');

bool validSize(QSize size)
{
    return size.width() > 0 && size.height() > 0
        && qint64(size.width()) * size.height() <= maximumPixels;
}

void readMetadata(const QString& path, ImageResult& result)
{
    // Function-local initialization is serialized even with multiple loaders.
    // Loaders join their workers before process shutdown destroys this runtime.
    struct XmpRuntime {
        XmpRuntime() { Exiv2::XmpParser::initialize(); }
        ~XmpRuntime() { Exiv2::XmpParser::terminate(); }
    };
    static const XmpRuntime xmpRuntime;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumFileBytes) return;
    // A mapped read supports Unicode paths on every platform; no file is modified.
    auto* bytes = file.map(0, file.size());
    if (!bytes) return;
    try {
        auto source = Exiv2::ImageFactory::open(bytes, static_cast<size_t>(file.size()));
        if (source) {
            source->readMetadata();
            const auto& exif = source->exifData();
            const auto value = [&exif](const char* key) -> QString {
                const auto entry = exif.findKey(Exiv2::ExifKey(key));
                return entry == exif.end() ? QString{} : QString::fromStdString(entry->print(&exif));
            };
            const std::pair<const char*, const char*> fields[] = {
                {"相机厂商", "Exif.Image.Make"}, {"相机型号", "Exif.Image.Model"},
                {"镜头", "Exif.Photo.LensModel"}, {"拍摄时间", "Exif.Photo.DateTimeOriginal"},
                {"时区", "Exif.Photo.OffsetTimeOriginal"}, {"光圈", "Exif.Photo.FNumber"},
                {"曝光时间", "Exif.Photo.ExposureTime"}, {"ISO", "Exif.Photo.ISOSpeedRatings"},
                {"焦距", "Exif.Photo.FocalLength"}, {"35mm 等效焦距", "Exif.Photo.FocalLengthIn35mmFilm"},
                {"曝光补偿", "Exif.Photo.ExposureBiasValue"}, {"闪光灯", "Exif.Photo.Flash"},
                {"作者", "Exif.Image.Artist"}, {"版权", "Exif.Image.Copyright"},
                {"软件", "Exif.Image.Software"}, {"方向", "Exif.Image.Orientation"}
            };
            for (const auto& [label, key] : fields) {
                const auto text = value(key);
                if (!text.isEmpty()) result.metadata.append({tr(label), text});
            }
            const auto coordinate = [&exif](const char* key, const char* refKey, double& output) {
                const auto entry = exif.findKey(Exiv2::ExifKey(key));
                const auto ref = exif.findKey(Exiv2::ExifKey(refKey));
                if (entry == exif.end() || ref == exif.end() || entry->count() != 3) return false;
                const double d = entry->toFloat(0), m = entry->toFloat(1), s = entry->toFloat(2);
                if (!std::isfinite(d) || !std::isfinite(m) || !std::isfinite(s)
                    || d < 0 || m < 0 || m >= 60 || s < 0 || s >= 60) return false;
                output = d + m / 60 + s / 3600;
                const auto direction = ref->toString();
                if (direction != "N" && direction != "S" && direction != "E" && direction != "W") return false;
                if (direction == "S" || direction == "W") output = -output;
                return true;
            };
            double latitude = 0, longitude = 0;
            if (coordinate("Exif.GPSInfo.GPSLatitude", "Exif.GPSInfo.GPSLatitudeRef", latitude)
                && coordinate("Exif.GPSInfo.GPSLongitude", "Exif.GPSInfo.GPSLongitudeRef", longitude)
                && std::abs(latitude) <= 90 && std::abs(longitude) <= 180) {
                result.metadata.append({tr("地理位置（纬度，经度）"),
                    QStringLiteral("%1, %2").arg(latitude, 0, 'f', 6).arg(longitude, 0, 'f', 6)});
            }
            const auto altitude = value("Exif.GPSInfo.GPSAltitude");
            if (!altitude.isEmpty()) result.metadata.append({tr("海拔"), altitude + " " + value("Exif.GPSInfo.GPSAltitudeRef")});
            int count = 0;
            for (const auto& entry : exif) {
                if (++count > 512) break;
                // Skip binary thumbnails and large MakerNote blobs in the readable list.
                if (entry.size() > 4096) continue;
                result.metadata.append({QString::fromStdString(entry.key()),
                    QString::fromStdString(entry.print(&exif)).left(4096)});
            }
            for (const auto& entry : source->iptcData()) {
                if (++count > 600) break;
                if (entry.size() <= 4096)
                    result.metadata.append({QString::fromStdString(entry.key()), QString::fromStdString(entry.toString()).left(4096)});
            }
            for (const auto& entry : source->xmpData()) {
                if (++count > 700) break;
                if (entry.size() <= 4096)
                    result.metadata.append({QString::fromStdString(entry.key()), QString::fromStdString(entry.toString()).left(4096)});
            }
        }
    } catch (const Exiv2::Error&) {
        // Metadata failure must not prevent viewing a successfully decoded image.
        result.metadata.append({tr("拍摄信息"), tr("此文件没有可读取的 EXIF/IPTC 信息。")});
    }
    file.unmap(bytes);
}

int rawProgress(void* context, enum LibRaw_progress, int, int)
{
    return static_cast<std::atomic_bool*>(context)->load() ? 1 : 0;
}

std::unique_ptr<LibRaw> makeRawDecoder(const std::shared_ptr<std::atomic_bool>& cancelled)
{
    configureRawProcessingThreads();
    auto raw = std::make_unique<LibRaw>();
    raw->set_progress_handler(rawProgress, cancelled.get());
    raw->imgdata.params.output_bps = 8;
    raw->imgdata.params.output_color = 1;
    raw->imgdata.params.use_camera_wb = 1;
    raw->imgdata.rawparams.max_raw_memory_mb = 512;
    return raw;
}

int openRawFile(LibRaw& raw, const QString& path)
{
#ifdef Q_OS_WIN
    return raw.open_file(reinterpret_cast<const wchar_t*>(path.utf16()));
#else
    const auto filename = QFile::encodeName(path);
    return raw.open_file(filename.constData());
#endif
}
}

QStringList ImageDecoder::supportedExtensions()
{
    QStringList result = rawExtensions;
    for (const auto& format : QImageReader::supportedImageFormats()) result.append(QString::fromLatin1(format).toLower());
    result.append({"svg", "svgz", "jpg", "jpeg", "png", "heic", "heif", "avif", "hif"});
    result.removeDuplicates();
    return result;
}

bool ImageDecoder::isImageCandidate(const QString& path)
{
    return supportedExtensions().contains(QFileInfo(path).suffix().toLower());
}

QList<ImageMetadataEntry> ImageDecoder::photographicMetadata(const QString& path)
{
    ImageResult result;
    readMetadata(path, result);
    return result.metadata;
}

ImageResult ImageDecoder::decode(const QString& path, const std::shared_ptr<std::atomic_bool>& cancelled,
                                 bool includeMetadata)
{
    ImageResult result;
    const auto fail = [&result](const QString& message) {
        result.status = ImageResult::Status::Failed;
        result.message = message;
        return result;
    };
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) return fail(tr("无法读取图片，文件可能已被移动或删除。"));
    if (info.size() > maximumFileBytes) {
        if (!isImageCandidate(path)) { result.message = tr("格式不支持"); return result; }
        return fail(tr("文件超过 512 MiB 的读取限制。"));
    }
    if (cancelled->load()) { result.status = ImageResult::Status::Cancelled; return result; }
    const auto suffix = info.suffix().toLower();
    QFile probe(path);
    if (!probe.open(QIODevice::ReadOnly)) return fail(tr("无法读取图片。"));
    const auto header = probe.read(128);
    probe.close();
    QImageReader reader(path);
    reader.setAutoTransform(true);
    reader.setDecideFormatFromContent(true);
    const bool rawCandidate = rawExtensions.contains(suffix);
    const bool qtReadable = reader.canRead();
    const auto qtFormat = reader.format().toLower();
    bool preferRaw = rawCandidate && !qtReadable;
    std::unique_ptr<LibRaw> identifiedRaw;
    if (qtFormat == "tiff" || qtFormat == "tif") {
        // TIFF can contain a RAW sensor image or just its preview. Identify
        // the contents before Qt consumes the first IFD, regardless of suffix.
        identifiedRaw = makeRawDecoder(cancelled);
        preferRaw = openRawFile(*identifiedRaw, path) == LIBRAW_SUCCESS
            && identifiedRaw->imgdata.idata.raw_count > 0;
        if (!preferRaw) identifiedRaw.reset();
    }
    if (HeifDecoder::recognizes(header)) {
        result = HeifDecoder::decode(path);
        if (result.status != ImageResult::Status::Ready) return result;
    } else if (!preferRaw && qtReadable) {
        result.originalSize = reader.size();
        result.format = QString::fromLatin1(reader.format()).toUpper();
        if (!validSize(result.originalSize)) return fail(tr("图片尺寸无效或超过 1 亿像素的限制。"));
        result.image = reader.read();
        if (result.image.isNull()) return fail(tr("无法读取图片：%1").arg(reader.errorString()));
        for (const auto& key : result.image.textKeys())
            result.metadata.append({key.left(256), result.image.text(key).left(4096)});
    } else if (suffix == "svg" || suffix == "svgz") {
        QSvgRenderer svg(path);
        if (!svg.isValid()) return fail(tr("无法读取 SVG 图片。"));
        result.originalSize = svg.defaultSize();
        if (!validSize(result.originalSize)) return fail(tr("SVG 尺寸无效或过大。"));
        result.image = QImage(result.originalSize, QImage::Format_ARGB32_Premultiplied);
        if (result.image.isNull()) return fail(tr("图片所需内存不足。"));
        result.image.fill(Qt::transparent);
        QPainter painter(&result.image);
        svg.render(&painter);
        result.format = "SVG";
    } else {
        const bool alreadyOpened = bool(identifiedRaw);
        if (!identifiedRaw) identifiedRaw = makeRawDecoder(cancelled);
        auto& raw = *identifiedRaw;
        int error = alreadyOpened ? LIBRAW_SUCCESS : openRawFile(raw, path);
        if (error != LIBRAW_SUCCESS) {
            if (rawCandidate && error == LIBRAW_FILE_UNSUPPORTED) {
                result.message = tr("格式不支持");
                return result;
            }
            if (rawCandidate || isImageCandidate(path)) return fail(tr("无法读取图片，文件可能已损坏或使用了尚不支持的编码。"));
            result.message = tr("格式不支持");
            return result;
        }
        result.originalSize = QSize(raw.imgdata.sizes.width, raw.imgdata.sizes.height);
        if (!validSize(result.originalSize)) return fail(tr("RAW 图片超过 1 亿像素的限制。"));
        if ((error = raw.unpack()) == LIBRAW_SUCCESS) error = raw.dcraw_process();
        if (cancelled->load()) { result.status = ImageResult::Status::Cancelled; return result; }
        if (error != LIBRAW_SUCCESS) return fail(tr("RAW 解码失败：%1").arg(QString::fromLatin1(libraw_strerror(error))));
        std::unique_ptr<libraw_processed_image_t, decltype(&LibRaw::dcraw_clear_mem)> decoded(
            raw.dcraw_make_mem_image(&error), &LibRaw::dcraw_clear_mem);
        if (!decoded || decoded->type != LIBRAW_IMAGE_BITMAP || decoded->colors != 3 || decoded->bits != 8)
            return fail(tr("RAW 解码未能生成可显示的图片。"));
        result.image = QImage(decoded->data, decoded->width, decoded->height,
                              decoded->width * 3, QImage::Format_RGB888).copy();
        result.format = QStringLiteral("RAW");
        if (rawCandidate) result.format += " / " + suffix.toUpper();
        result.metadata.append({tr("相机厂商"), QString::fromUtf8(raw.imgdata.idata.make)});
        result.metadata.append({tr("相机型号"), QString::fromUtf8(raw.imgdata.idata.model)});
        if (raw.imgdata.other.aperture > 0)
            result.metadata.append({tr("光圈"), QStringLiteral("f/%1").arg(raw.imgdata.other.aperture, 0, 'f', 1)});
        result.image.setColorSpace(QColorSpace::SRgb);
    }
    if (cancelled->load()) { result.image = {}; result.status = ImageResult::Status::Cancelled; return result; }
    if (result.image.isNull()) return fail(tr("图片所需内存不足。"));
    if (result.image.colorSpace().isValid()) result.image.convertToColorSpace(QColorSpace::SRgb);
    if (includeMetadata) readMetadata(path, result);
    result.status = ImageResult::Status::Ready;
    return result;
}
