#include "MediaNavigation.h"
#include "MpvPlayer.h"
#include <QTimer>
#include <QFileInfo>

MediaNavigation::MediaNavigation(QObject* parent)
    : QObject(parent), m_probe(new MpvPlayer(this)), m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(10000);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        const auto path = m_probePath;
        cancel();
        emit failed(tr("无法及时读取媒体类型：%1。请直接打开文件重试。").arg(path));
    });
    connect(m_probe, &MpvPlayer::stateChanged, this, [this] {
        if (m_probePath.isEmpty() || !m_probe->state().loaded) return;
        const auto category = m_probe->state().video ? MediaType::Video : MediaType::Audio;
        remember(m_probePath, category);
        if (category == m_category) {
            selectCandidate();
        } else {
            m_probePath.clear();
            m_timeout->stop();
            m_probe->stop();
            queueNext();
        }
    });
    // Retain the existing error path: select an unreadable candidate so the
    // viewer displays its file information and playback error. Never skip it.
    connect(m_probe, &MpvPlayer::failed, this, [this] { selectCandidate(); });
}

void MediaNavigation::cancel()
{
    ++m_generation;
    m_candidates = {};
    m_probePath.clear();
    m_category = MediaType::Unknown;
    m_timeout->stop();
    m_probe->stop();
}

void MediaNavigation::navigate(const QStringList& candidates, MediaType category)
{
    navigate([paths = candidates, index = qsizetype(0)]() mutable -> std::optional<QString> {
        if (index == paths.size()) return std::nullopt;
        return paths[index++];
    }, category);
}

void MediaNavigation::navigate(CandidateSource candidates, MediaType category)
{
    cancel();
    if (category == MediaType::Unknown) return;
    m_candidates = std::move(candidates);
    m_category = category;
    queueNext();
}

void MediaNavigation::remember(const QString& path, MediaType category)
{
    const QFileInfo info(path);
    if (info.isFile()) m_categories.insert(path, new CategoryEntry{info.size(), info.lastModified(), category});
}

void MediaNavigation::queueNext()
{
    const auto generation = m_generation;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation == m_generation) probeNext();
    });
}

void MediaNavigation::probeNext()
{
    if (!m_candidates) return;
    const auto candidate = m_candidates();
    if (!candidate) { m_candidates = {}; return; }
    if (candidate->isEmpty()) { queueNext(); return; }
    m_probePath = *candidate;
    if (m_category == MediaType::Image) { selectCandidate(); return; }
    if (const auto* entry = m_categories.object(m_probePath)) {
        const QFileInfo info(m_probePath);
        if (info.isFile() && info.size() == entry->size && info.lastModified() == entry->modified) {
            if (entry->category == m_category) selectCandidate();
            else { m_probePath.clear(); queueNext(); }
            return;
        }
        m_categories.remove(m_probePath);
    }
    m_timeout->start();
    m_probe->open(m_probePath, 1.0, 0.0, MpvPlayer::RenderMode::Probe);
}

void MediaNavigation::selectCandidate()
{
    if (m_probePath.isEmpty()) return;
    const auto path = m_probePath;
    cancel();
    emit candidateSelected(path);
}
