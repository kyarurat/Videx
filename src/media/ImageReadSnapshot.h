#pragma once
#include <QByteArray>
#include <QTemporaryFile>
#include <atomic>
#include <memory>

// Immutable compressed input. Only the short, cancellable copy touches the
// original; decoders and metadata readers cannot lock or map the user's file.
class ImageReadSnapshot
{
public:
    bool read(const QString& path, const std::shared_ptr<std::atomic_bool>& cancelled);
    QByteArray bytes() const;
    QString error;
    bool interrupted = false;
    bool sourceChanged = false;
private:
    QByteArray m_memory;
    QTemporaryFile m_temporary;
    uchar* m_mapping = nullptr;
    qint64 m_size = 0;
};
