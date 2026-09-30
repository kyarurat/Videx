#include "HeifDecoder.h"
#include <QColorSpace>
#include <QCoreApplication>
#include <QFile>
#include <libheif/heif.h>

bool HeifDecoder::recognizes(const QByteArray& header)
{
    if (header.size() < 12 || header.mid(4, 4) != "ftyp") return false;
    // Generic mif1 files are valid HEIF, while an arbitrary ISO-BMFF file (MP4,
    // CR3, etc.) must not be claimed by this backend just because it has ftyp.
    for (const auto* brand : {"heic", "heix", "hevc", "hevx", "mif1", "mif2", "msf1", "avif", "avis"}) {
        if (header.mid(8, 4) == brand || heif_has_compatible_brand(
                reinterpret_cast<const uint8_t*>(header.constData()), static_cast<int>(header.size()), brand) == 1)
            return true;
    }
    return false;
}

ImageResult HeifDecoder::decode(const QString& path, const std::shared_ptr<std::atomic_bool>& cancelled, QSize targetSize)
{
    ImageResult result;
    result.status = ImageResult::Status::Failed;
    result.message = QCoreApplication::translate("ImageDecoder", "无法读取 HEIC/AVIF 图片。");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return result;
    auto* data = file.map(0, file.size());
    if (!data) return result;
    std::unique_ptr<heif_context, decltype(&heif_context_free)> context(heif_context_alloc(), &heif_context_free);
    if (!context) return result;
    heif_context_set_max_decoding_threads(context.get(), 2);
    auto error = heif_context_read_from_memory_without_copy(context.get(), data, static_cast<size_t>(file.size()), nullptr);
    if (error.code != heif_error_Ok) return result;
    heif_image_handle* rawHandle = nullptr;
    error = heif_context_get_primary_image_handle(context.get(), &rawHandle);
    std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)> handle(rawHandle, &heif_image_handle_release);
    if (error.code != heif_error_Ok || !handle) return result;
    const int width = heif_image_handle_get_width(handle.get());
    const int height = heif_image_handle_get_height(handle.get());
    if (width <= 0 || height <= 0 || qint64(width) * height > 100000000) {
        result.message = QCoreApplication::translate("ImageDecoder", "图片尺寸无效或超过 1 亿像素的限制。");
        return result;
    }
    result.originalSize = QSize(width, height);
    // Prefer a container thumbnail, but keep source dimensions for zoom/pan.
    heif_image_handle* rawThumbnail = nullptr;
    std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)> thumbnail(nullptr, &heif_image_handle_release);
    heif_item_id id = 0;
    if (targetSize.isValid() && heif_image_handle_get_list_of_thumbnail_IDs(handle.get(), &id, 1) > 0
        && heif_image_handle_get_thumbnail(handle.get(), id, &rawThumbnail).code == heif_error_Ok)
        thumbnail.reset(rawThumbnail);
    auto* decodeHandle = thumbnail ? thumbnail.get() : handle.get();
    std::unique_ptr<heif_decoding_options, decltype(&heif_decoding_options_free)> options(
        heif_decoding_options_alloc(), &heif_decoding_options_free);
#if LIBHEIF_NUMERIC_VERSION >= 0x01140000
    if (options) {
        options->cancel_decoding = [](void* token) { return static_cast<std::atomic_bool*>(token)->load() ? 1 : 0; };
        options->progress_user_data = cancelled.get();
    }
#endif
    if (cancelled->load()) { result.status = ImageResult::Status::Cancelled; return result; }
    heif_image* rawImage = nullptr;
    error = heif_decode_image(decodeHandle, &rawImage, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, options.get());
    std::unique_ptr<heif_image, decltype(&heif_image_release)> decoded(rawImage, &heif_image_release);
    if (cancelled->load()) { result.status = ImageResult::Status::Cancelled; return result; }
    if (thumbnail && error.code != heif_error_Ok) {
        decoded.reset();
        decodeHandle = handle.get();
        rawImage = nullptr;
        error = heif_decode_image(decodeHandle, &rawImage, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, options.get());
        decoded.reset(rawImage);
        if (cancelled->load()) { result.status = ImageResult::Status::Cancelled; return result; }
    }
    if (error.code != heif_error_Ok || !decoded) {
        if (error.code == heif_error_Unsupported_feature) {
            result.status = ImageResult::Status::Unsupported;
            result.message = QCoreApplication::translate("ImageDecoder", "格式不支持");
        }
        return result;
    }
    int stride = 0;
    const auto* pixels = heif_image_get_plane_readonly(decoded.get(), heif_channel_interleaved, &stride);
    if (!pixels) return result;
    result.image = QImage(pixels, heif_image_get_width(decoded.get(), heif_channel_interleaved),
                         heif_image_get_height(decoded.get(), heif_channel_interleaved), stride, QImage::Format_RGBA8888).copy();
    const size_t profileSize = heif_image_handle_get_raw_color_profile_size(decodeHandle);
    if (profileSize > 0 && profileSize <= 4 * 1024 * 1024) {
        QByteArray profile(static_cast<qsizetype>(profileSize), Qt::Uninitialized);
        if (heif_image_handle_get_raw_color_profile(decodeHandle, profile.data()).code == heif_error_Ok)
            result.image.setColorSpace(QColorSpace::fromIccProfile(profile));
    }
    result.format = QStringLiteral("HEIF / AVIF");
    result.status = result.image.isNull() ? ImageResult::Status::Failed : ImageResult::Status::Ready;
    if (result.status == ImageResult::Status::Ready) result.message.clear();
    return result;
}
