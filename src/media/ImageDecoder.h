#pragma once
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>

struct ImageMetadataEntry
{
    QString name;
    QString value;
};

struct ImageResult
{
    enum class Status { Ready, Unsupported, Failed, Cancelled };
    Status status = Status::Unsupported;
    QImage image;
    QSize originalSize;
    QString format;
    QString message;
    QList<ImageMetadataEntry> metadata;
};

namespace ImageDecoder {
// Extensions are only navigation hints. decode() checks the file contents.
bool isImageCandidate(const QString& path);
QStringList supportedExtensions();
ImageResult decode(const QString& path, const std::shared_ptr<std::atomic_bool>& cancelled,
                   bool includeMetadata = true);
QList<ImageMetadataEntry> photographicMetadata(const QString& path);
}
