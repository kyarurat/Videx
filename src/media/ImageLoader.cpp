#include "ImageLoader.h"
#include <QtConcurrentRun>
#include <QFileInfo>
#include <QTimer>

ImageLoader::ImageLoader(QObject* parent, Decode decode, ReadMetadata metadata)
    : QObject(parent),
      m_decode(std::move(decode)),
      m_readMetadata(std::move(metadata))
{
    // One reserved foreground slot and at most one speculative decode.
    m_pool.setMaxThreadCount(2);
    m_metadataPool.setMaxThreadCount(1);
    connect(&m_foreground.watcher, &QFutureWatcher<ImageResult>::finished, this,
            [this] { finish(m_foreground); });
    connect(&m_background.watcher, &QFutureWatcher<ImageResult>::finished, this,
            [this] { finish(m_background); });
}

ImageLoader::~ImageLoader()
{
    clear();
    m_pool.waitForDone();
    m_metadataPool.waitForDone();
}

void ImageLoader::finish(Job& job)
{
    const auto result = job.watcher.future().takeResult();
    const bool cancelled = job.cancelled->load();
    job.running = false;
    if (!cancelled && result.status == ImageResult::Status::Cancelled) {
        // A rename/replacement must finish before reopening the source. The
        // selected-file monitor normally supplies the new request first.
        QTimer::singleShot(200, this, &ImageLoader::startPending);
        return;
    }
    const QFileInfo info(job.path);
    const bool unchanged = info.isFile() && info.size() == job.size
        && info.lastModified() == job.modified && FileIdentity::read(job.path) == job.identity;
    if (!cancelled && unchanged && result.status == ImageResult::Status::Ready) {
        qint64 bytes = result.image.sizeInBytes();
        for (const auto& entry : result.metadata)
            bytes += (entry.name.size() + entry.value.size()) * 2 + sizeof(ImageMetadataEntry);
        const qint64 cost = (bytes + 1023) / 1024 + 1;
        const auto* priority = m_cache.object(m_prefetchPriority);
        const bool protectNext = job.path != m_requested && job.path != m_prefetchPriority
            && priority && cost + priority->cost > m_cache.maxCost();
        const auto* existing = m_cache.object(job.path);
        if (cost <= m_cache.maxCost() && !protectNext && (!existing || !existing->result.fullResolution || result.fullResolution))
            m_cache.insert(job.path, new CachedImage{result, job.size, job.modified, int(cost), job.identity}, int(cost));
    }
    if (!cancelled && m_requested == job.path && (m_waiting || m_originalRequested)) {
        // Missing files can deliver their error. Changed files must be decoded
        // again, including a preload promoted after an external replacement.
        if (unchanged || !info.isFile()) {
            if (unchanged || result.status != ImageResult::Status::Ready) {
                if (m_waiting || job.targetSize.isEmpty()) {
                    m_waiting = false;
                    if (job.targetSize.isEmpty()) m_originalRequested = false;
                    // An optional original failing must leave its preview usable.
                    if (!m_originalSpeculative || result.status == ImageResult::Status::Ready)
                        deliver(result);
                    m_originalSpeculative = false;
                }
            }
        }
    }
    startPending();
}

void ImageLoader::clear()
{
    if (m_metadataCancelled) m_metadataCancelled->store(true);
    ++m_generation;
    m_requested.clear();
    m_prefetch.clear();
    m_prefetchPriority.clear();
    m_waiting = false;
    m_metadataPending = false;
    m_originalRequested = false;
    m_currentSourceSize = {};
    m_currentFullResolution = false;
    m_cache.clear();
    m_metadataCache.clear();
    for (auto* job : {&m_foreground, &m_background})
        if (job->cancelled) job->cancelled->store(true);
}

ImageLoader::CachedImage* ImageLoader::cached(const QString& path)
{
    auto* entry = m_cache.object(path);
    if (!entry) return nullptr;
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable() || info.size() != entry->fileSize
        || info.lastModified() != entry->modified || FileIdentity::read(path) != entry->identity) {
        m_cache.remove(path);
        return nullptr;
    }
    return entry;
}

bool ImageLoader::isCached(const QString& path) { return cached(path) != nullptr; }

void ImageLoader::relocate(const QString& oldPath, const QString& newPath)
{
    if (auto* entry = m_cache.take(oldPath)) {
        const auto cost = entry->cost;
        m_cache.insert(newPath, entry, cost);
    }
    if (auto* entry = m_metadataCache.take(oldPath)) {
        qint64 bytes = 0;
        for (const auto& item : entry->entries) bytes += (item.name.size() + item.value.size()) * 2 + sizeof(item);
        m_metadataCache.insert(newPath, entry, int((bytes + 1023) / 1024 + 1));
    }
    open(newPath, false, m_previewSize);
}

void ImageLoader::open(const QString& path, bool reload, QSize previewSize)
{
    if (m_metadataCancelled) m_metadataCancelled->store(true);
    ++m_generation;
    m_metadataPending = false;
    m_metadataDelivered = false;
    m_originalRequested = false;
    m_originalSpeculative = false;
    m_previewSize = previewSize;
    m_currentSourceSize = {};
    m_currentFullResolution = false;
    m_requested = path;
    m_waiting = !path.isEmpty();
    m_prefetch.clear();
    m_prefetchPriority.clear();
    if (reload) { m_cache.remove(path); m_metadataCache.remove(path); }
    for (auto* job : {&m_foreground, &m_background}) {
        if (job->running && (job->path != path || reload)) job->cancelled->store(true);
    }
    if (auto* entry = cached(path)) {
        const auto result = entry->result;
        m_waiting = false;
        deliver(result);
    }
    startPending();
}

void ImageLoader::requestOriginal(bool speculative)
{
    if (m_requested.isEmpty() || !m_currentSourceSize.isValid() || m_currentFullResolution) return;
    if (const auto* entry = cached(m_requested); entry && entry->result.fullResolution) return;
    // Reserve half the cache for neighbors. Never speculatively allocate an
    // original too large to retain; explicit actual-size requests can still do so.
    if (speculative && qint64(m_currentSourceSize.width()) * m_currentSourceSize.height() * 4 > 128LL * 1024 * 1024) return;
    m_originalRequested = true;
    m_originalSpeculative = speculative;
    startPending();
}

void ImageLoader::prefetch(const QStringList& paths)
{
    m_prefetch = paths.mid(0, 2);
    m_prefetch.removeAll(m_requested);
    m_prefetch.removeDuplicates();
    m_prefetchPriority = m_prefetch.isEmpty() ? QString{} : m_prefetch.first();
    startPending();
}

void ImageLoader::startPending()
{
    if (m_waiting) {
        // Reuse a healthy in-flight preload, but never wait for an unrelated
        // background task that a decoder cannot interrupt.
        for (auto* job : {&m_foreground, &m_background})
            if (job->running && job->path == m_requested && !job->cancelled->load()) return;
        if (!m_foreground.running) start(m_foreground, m_requested, m_previewSize);
        else if (!m_background.running) start(m_background, m_requested, m_previewSize);
        return;
    }
    if (m_originalRequested) {
        for (auto* job : {&m_foreground, &m_background})
            if (job->running && job->path == m_requested && job->targetSize.isEmpty() && !job->cancelled->load()) return;
        if (!m_originalSpeculative && m_background.running && m_background.path != m_requested)
            m_background.cancelled->store(true);
        if (!m_foreground.running) start(m_foreground, m_requested, {});
        return;
    }
    // Do not add speculative work while cancelled foreground work drains.
    if (m_foreground.running || m_background.running) return;
    while (!m_prefetch.isEmpty()) {
        const auto path = m_prefetch.takeFirst();
        if (!cached(path)) { start(m_background, path, m_previewSize); break; }
    }
}

void ImageLoader::start(Job& job, const QString& path, QSize targetSize)
{
    const QFileInfo info(path);
    job.path = path;
    job.size = info.size();
    job.modified = info.lastModified();
    job.identity = FileIdentity::read(path);
    job.running = true;
    job.targetSize = targetSize;
    job.cancelled = std::make_shared<std::atomic_bool>(false);
    job.watcher.setFuture(QtConcurrent::run(&m_pool, [path, targetSize, token = job.cancelled, decode = m_decode] {
        try { return decode ? decode(path, token) : ImageDecoder::decode(path, token, false, targetSize); }
        catch (const std::exception&) {
            ImageResult result;
            result.status = ImageResult::Status::Failed;
            result.message = tr("图片解码失败或内存不足。");
            return result;
        }
    }));
}

void ImageLoader::deliver(const ImageResult& result)
{
    const auto generation = m_generation;
    if (result.status == ImageResult::Status::Interrupted) { emit accessInterrupted(); return; }
    if (result.status == ImageResult::Status::Ready) {
        m_currentSourceSize = result.sourceSize.isValid() ? result.sourceSize : result.originalSize;
        m_currentFullResolution = result.fullResolution;
    }
    emit loaded(result);
    if (generation != m_generation || result.status != ImageResult::Status::Ready) return;
    if (m_metadataDelivered) return;
    m_metadataDelivered = true;
    if (const auto* entry = m_metadataCache.object(m_requested)) {
        const QFileInfo current(m_requested);
        if (current.size() == entry->size && current.lastModified() == entry->modified
            && FileIdentity::read(m_requested) == entry->identity) {
            emit metadataLoaded(entry->entries);
            return;
        }
        m_metadataCache.remove(m_requested);
    }
    m_metadataPending = true;
    startMetadata();
}

void ImageLoader::startMetadata()
{
    if (m_metadataRunning || !m_metadataPending) return;
    m_metadataPending = false;
    m_metadataRunning = true;
    const QString path = m_requested;
    const QFileInfo stamp(path);
    const auto size = stamp.size();
    const auto modified = stamp.lastModified();
    const auto identity = FileIdentity::read(path);
    m_metadataCancelled = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_metadataCancelled;
    const auto generation = m_generation;
    // There is only one metadata worker; obsolete requests are coalesced rather
    // than queued for every file visited while scrolling.
    disconnect(&m_metadataWatcher, nullptr, this, nullptr);
    connect(&m_metadataWatcher, &QFutureWatcher<MetadataResult>::finished, this,
            [this, path, size, modified, generation, identity] {
        const auto result = m_metadataWatcher.future().takeResult();
        const auto& entries = result.entries;
        m_metadataRunning = false;
        if (generation == m_generation && result.interrupted) { emit accessInterrupted(); return; }
        const QFileInfo current(path);
        if (generation == m_generation && result.available && current.isFile()
            && current.size() == size && current.lastModified() == modified && FileIdentity::read(path) == identity) {
            qint64 bytes = 0;
            for (const auto& entry : entries) bytes += sizeof(ImageMetadataEntry) + (entry.name.size() + entry.value.size()) * 2;
            const qint64 cost = (bytes + 1023) / 1024 + 1;
            if (cost <= m_metadataCache.maxCost())
                m_metadataCache.insert(path, new CachedMetadata{entries, size, modified, identity}, int(cost));
            emit metadataLoaded(entries);
        } else if (generation == m_generation && !result.available && current.isFile()) {
            emit metadataLoaded({{tr("拍摄信息"), tr("文件正在变化或暂时无法读取拍摄信息。")}});
        }
        startMetadata();
    });
    m_metadataWatcher.setFuture(QtConcurrent::run(&m_metadataPool, [path, cancelled, read = m_readMetadata] {
        MetadataResult result;
        try { result.entries = read ? read(path) : ImageDecoder::photographicMetadata(path, cancelled, &result.interrupted, &result.available); }
        catch (const std::exception&) {
            result.entries = {{tr("拍摄信息"), tr("无法读取图片元数据。")}};
        }
        return result;
    }));
}
