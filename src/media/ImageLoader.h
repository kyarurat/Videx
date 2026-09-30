#pragma once
#include "ImageDecoder.h"
#include <QFutureWatcher>
#include <QObject>
#include <QThreadPool>
#include <QCache>
#include <QDateTime>
#include <functional>

class ImageLoader : public QObject
{
    Q_OBJECT
public:
    using Decode = std::function<ImageResult(const QString&, const std::shared_ptr<std::atomic_bool>&)>;
    using ReadMetadata = std::function<QList<ImageMetadataEntry>(const QString&)>;
    explicit ImageLoader(QObject* parent = nullptr, Decode decode = {}, ReadMetadata metadata = {});
    ~ImageLoader() override;
    void open(const QString& path, bool reload = false, QSize previewSize = {});
    void requestOriginal(bool speculative = false);
    void prefetch(const QStringList& paths);
    bool isCached(const QString& path);
    void clear();
signals:
    void loaded(const ImageResult& result);
    void metadataLoaded(const QList<ImageMetadataEntry>& metadata);
private:
    struct CachedImage {
        ImageResult result;
        qint64 fileSize;
        QDateTime modified;
        int cost;
    };
    struct Job {
        QFutureWatcher<ImageResult> watcher;
        std::shared_ptr<std::atomic_bool> cancelled;
        QString path;
        qint64 size = 0;
        QDateTime modified;
        bool running = false;
        QSize targetSize;
    };
    struct CachedMetadata {
        QList<ImageMetadataEntry> entries;
        qint64 size;
        QDateTime modified;
    };
    CachedImage* cached(const QString& path);
    void startPending();
    void start(Job& job, const QString& path, QSize targetSize);
    void finish(Job& job);
    void deliver(const ImageResult& result);
    void startMetadata();
    QThreadPool m_pool;
    QThreadPool m_metadataPool;
    Job m_foreground;
    Job m_background;
    QFutureWatcher<QList<ImageMetadataEntry>> m_metadataWatcher;
    Decode m_decode;
    ReadMetadata m_readMetadata;
    QCache<QString, CachedImage> m_cache{256 * 1024}; // KiB of decoded data
    QCache<QString, CachedMetadata> m_metadataCache{4 * 1024}; // KiB, separate from pixels
    QString m_requested;
    QStringList m_prefetch;
    QString m_prefetchPriority;
    quint64 m_generation = 0;
    bool m_waiting = false;
    bool m_metadataPending = false;
    bool m_metadataRunning = false;
    QSize m_previewSize;
    bool m_originalRequested = false;
    bool m_originalSpeculative = false;
    bool m_metadataDelivered = false;
    QSize m_currentSourceSize;
    bool m_currentFullResolution = false;
};
