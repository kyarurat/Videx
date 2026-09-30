#include "ImageLoader.h"
#include <QtConcurrentRun>
#include <QFileInfo>

ImageLoader::ImageLoader(QObject* parent, Decode decode, ReadMetadata metadata)
    : QObject(parent),
      m_decode(decode ? std::move(decode) : Decode{[](const QString& path, const auto& token) {
          return ImageDecoder::decode(path, token, false);
      }}),
      m_readMetadata(metadata ? std::move(metadata) : ReadMetadata{ImageDecoder::photographicMetadata})
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
    const QFileInfo info(job.path);
    const bool unchanged = info.isFile() && info.size() == job.size
        && info.lastModified() == job.modified;
    if (!cancelled && unchanged && result.status == ImageResult::Status::Ready) {
        qint64 bytes = result.image.sizeInBytes();
        for (const auto& entry : result.metadata)
            bytes += (entry.name.size() + entry.value.size()) * 2 + sizeof(ImageMetadataEntry);
        const qint64 cost = (bytes + 1023) / 1024 + 1;
        const auto* priority = m_cache.object(m_prefetchPriority);
        const bool protectNext = job.path != m_requested && job.path != m_prefetchPriority
            && priority && cost + priority->cost > m_cache.maxCost();
        if (cost <= m_cache.maxCost() && !protectNext)
            m_cache.insert(job.path, new CachedImage{result, job.size, job.modified, int(cost)}, int(cost));
    }
    if (!cancelled && m_waiting && m_requested == job.path) {
        // Missing files can deliver their error. Changed files must be decoded
        // again, including a preload promoted after an external replacement.
        if (unchanged || !info.isFile()) {
            if (unchanged || result.status != ImageResult::Status::Ready) {
                m_waiting = false;
                deliver(result);
            }
        }
    }
    startPending();
}

void ImageLoader::clear()
{
    ++m_generation;
    m_requested.clear();
    m_prefetch.clear();
    m_prefetchPriority.clear();
    m_waiting = false;
    m_metadataPending = false;
    m_cache.clear();
    for (auto* job : {&m_foreground, &m_background})
        if (job->cancelled) job->cancelled->store(true);
}

ImageLoader::CachedImage* ImageLoader::cached(const QString& path)
{
    auto* entry = m_cache.object(path);
    if (!entry) return nullptr;
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable() || info.size() != entry->fileSize
        || info.lastModified() != entry->modified) {
        m_cache.remove(path);
        return nullptr;
    }
    return entry;
}

bool ImageLoader::isCached(const QString& path) { return cached(path) != nullptr; }

void ImageLoader::open(const QString& path, bool reload)
{
    ++m_generation;
    m_metadataPending = false;
    m_requested = path;
    m_waiting = !path.isEmpty();
    m_prefetch.clear();
    m_prefetchPriority.clear();
    if (reload) m_cache.remove(path);
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
        if (!m_foreground.running) start(m_foreground, m_requested);
        return;
    }
    // Do not add speculative work while cancelled foreground work drains.
    if (m_foreground.running || m_background.running) return;
    while (!m_prefetch.isEmpty()) {
        const auto path = m_prefetch.takeFirst();
        if (!cached(path)) { start(m_background, path); break; }
    }
}

void ImageLoader::start(Job& job, const QString& path)
{
    const QFileInfo info(path);
    job.path = path;
    job.size = info.size();
    job.modified = info.lastModified();
    job.running = true;
    job.cancelled = std::make_shared<std::atomic_bool>(false);
    job.watcher.setFuture(QtConcurrent::run(&m_pool, [path, token = job.cancelled, decode = m_decode] {
        try { return decode(path, token); }
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
    emit loaded(result);
    if (generation != m_generation || result.status != ImageResult::Status::Ready) return;
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
    const auto generation = m_generation;
    // There is only one metadata worker; obsolete requests are coalesced rather
    // than queued for every file visited while scrolling.
    disconnect(&m_metadataWatcher, nullptr, this, nullptr);
    connect(&m_metadataWatcher, &QFutureWatcher<QList<ImageMetadataEntry>>::finished, this,
            [this, path, size, modified, generation] {
        const auto entries = m_metadataWatcher.future().takeResult();
        m_metadataRunning = false;
        const QFileInfo current(path);
        if (generation == m_generation && current.isFile()
            && current.size() == size && current.lastModified() == modified)
            emit metadataLoaded(entries);
        startMetadata();
    });
    m_metadataWatcher.setFuture(QtConcurrent::run(&m_metadataPool, [path, read = m_readMetadata] {
        try { return read(path); }
        catch (const std::exception&) {
            return QList<ImageMetadataEntry>{{tr("拍摄信息"), tr("无法读取图片元数据。")}};
        }
    }));
}
