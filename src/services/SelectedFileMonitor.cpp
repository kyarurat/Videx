#include "SelectedFileMonitor.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QtConcurrentRun>
#include <QDebug>

namespace {
FileDetails details(const QFileInfo& info)
{
    return {info.absoluteFilePath(), info.fileName(), info.suffix(), info.size(), info.lastModified()};
}
}

SelectedFileMonitor::SelectedFileMonitor(QObject* parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(150);
    connect(&m_timer, &QTimer::timeout, this, &SelectedFileMonitor::inspect);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        if (path != m_file.path) return;
        m_fileNotification = true;
        m_dirty = true;
        m_timer.start();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
        if (m_file.path.isEmpty() || path != QFileInfo(m_file.path).absolutePath()) return;
        m_dirty = true;
        m_timer.start();
    });
    connect(&m_result, &QFutureWatcher<Result>::finished, this, [this] {
        m_inspecting = false;
        const auto result = m_result.result();
        if (m_inspectingGeneration == m_generation && !m_file.path.isEmpty()) {
            if (m_dirty) {
                // Coalescing a later directory event must not lose an earlier
                // file notification (same-size writes may retain timestamps).
                m_fileNotification = m_fileNotification || result.changed;
                m_timer.start();
                return;
            }
            if (result.details.path.isEmpty()) { clear(); emit unavailable(); return; }
            m_file = result.details;
            m_identity = result.identity;
            arm();
            if (result.relocated) emit relocated(m_file);
            if (result.changed) emit changed(m_file);
        }
        if (m_dirty) m_timer.start();
    });
}

SelectedFileMonitor::~SelectedFileMonitor() { clear(); }

void SelectedFileMonitor::clear()
{
    ++m_generation;
    if (m_cancelled) m_cancelled->store(true);
    m_timer.stop();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty()) m_watcher.removePaths(paths);
    m_file = {};
    m_identity = {};
    m_dirty = m_fileNotification = false;
}

void SelectedFileMonitor::watch(const FileDetails& file)
{
    clear();
    m_file = file;
    m_identity = FileIdentity::read(file.path);
    arm();
    // Selection and watcher installation are not atomic. Verify once after
    // arming so a removal in that gap cannot leave a stale selection forever.
    if (!file.path.isEmpty()) { m_dirty = true; m_timer.start(); }
}

void SelectedFileMonitor::arm()
{
    if (m_file.path.isEmpty()) return;
    const QStringList wanted{m_file.path, QFileInfo(m_file.path).absolutePath()};
    const auto paths = m_watcher.files() + m_watcher.directories();
    // Keep the parent-directory watch alive across inspections and renames.
    // Rebuilding it can discard notifications arriving after the worker's read.
    for (const auto& path : paths) {
        if (!wanted.contains(path)) m_watcher.removePath(path);
    }
    QStringList missing;
    for (const auto& path : wanted) {
        if (!paths.contains(path)) missing.append(path);
    }
    const auto failed = missing.isEmpty() ? QStringList{} : m_watcher.addPaths(missing);
    if (!failed.isEmpty()) qWarning() << "Cannot watch selected file paths:" << failed;
}

void SelectedFileMonitor::inspect()
{
    if (m_file.path.isEmpty() || m_inspecting) return;
    m_inspecting = true;
    const auto file = m_file;
    const auto identity = m_identity;
    const bool notified = m_fileNotification;
    m_dirty = m_fileNotification = false;
    m_inspectingGeneration = m_generation;
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_cancelled;
    m_result.setFuture(QtConcurrent::run([file, identity, notified, cancelled] {
        Result result;
        const QFileInfo current(file.path);
        QString actualName;
        const auto currentIdentity = FileIdentity::read(file.path, &actualName);
        if (current.isFile() && (!identity.valid() || currentIdentity == identity)) {
            result = {details(current), currentIdentity, false,
                notified || current.size() != file.size || current.lastModified() != file.modified};
            if (actualName != current.fileName()) {
                const QFileInfo renamed(QDir(current.absolutePath()).filePath(actualName));
                if (renamed.isFile()) return Result{details(renamed), currentIdentity, true,
                    renamed.size() != file.size || renamed.lastModified() != file.modified};
            }
            return result;
        } else if (current.isFile()) {
            return Result{details(current), currentIdentity, false, true};
        }
        // Qt reports a path change, not a rename destination. Search only this
        // one directory, off the GUI thread, and never infer identity from size.
        if (!identity.valid() || !identity.renameUnambiguous) return result;
        QDirIterator entries(QFileInfo(file.path).absolutePath(),
            QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        FileDetails match;
        while (entries.hasNext() && !cancelled->load()) {
            const auto path = entries.next();
            if (FileIdentity::read(path) != identity) continue;
            if (!match.path.isEmpty()) return result; // Ambiguous hard links.
            match = details(entries.fileInfo());
        }
        if (cancelled->load()) return Result{};
        if (!match.path.isEmpty() && match.path != file.path)
            return Result{match, identity, true, match.size != file.size || match.modified != file.modified};
        return result;
    }));
}
