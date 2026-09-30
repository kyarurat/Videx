#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <libraw/libraw.h>
#include "media/RawProcessing.h"
#include <memory>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream output(stdout);
    if (app.arguments().size() != 2) {
        output << "Usage: videx_raw_benchmark <RAW file or directory>\n";
        return 2;
    }
    const QString input = app.arguments().at(1);
    QStringList files;
    if (QFileInfo(input).isDir()) {
        QDirIterator it(input, {"*.cr2", "*.CR2"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files.append(it.next());
        files.sort();
    } else files.append(input);
    if (files.isEmpty()) return 2;
    int failures = 0;
    for (const auto& path : files) {
        configureRawProcessingThreads();
        LibRaw raw;
        raw.imgdata.params.output_bps = 8;
        raw.imgdata.params.output_color = 1;
        raw.imgdata.params.use_camera_wb = 1;
        raw.imgdata.rawparams.max_raw_memory_mb = 512;
        QElapsedTimer timer;
        timer.start();
#ifdef Q_OS_WIN
        int error = raw.open_file(reinterpret_cast<const wchar_t*>(path.utf16()));
#else
        const auto name = QFile::encodeName(path);
        int error = raw.open_file(name.constData());
#endif
        const auto opened = timer.elapsed();
        if (!error) error = raw.unpack();
        const auto unpacked = timer.elapsed();
        if (!error) error = raw.dcraw_process();
        const auto processed = timer.elapsed();
        std::unique_ptr<libraw_processed_image_t, decltype(&LibRaw::dcraw_clear_mem)> image(
            error ? nullptr : raw.dcraw_make_mem_image(&error), &LibRaw::dcraw_clear_mem);
        const auto finished = timer.elapsed();
        QJsonObject record{{"file", QFileInfo(path).fileName()}, {"open_ms", opened},
            {"unpack_ms", unpacked - opened}, {"process_ms", processed - unpacked},
            {"output_ms", finished - processed}, {"total_ms", finished}, {"error", error}};
        if (image) {
            record["width"] = image->width;
            record["height"] = image->height;
            record["sha256"] = QString::fromLatin1(QCryptographicHash::hash(
                QByteArrayView(reinterpret_cast<const char*>(image->data), image->data_size),
                QCryptographicHash::Sha256).toHex());
        } else ++failures;
        output << QJsonDocument(record).toJson(QJsonDocument::Compact) << Qt::endl;
    }
    return failures ? 1 : 0;
}
