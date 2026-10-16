#pragma once
#include "FileIdentity.h"
#include "app/UiState.h"
#include <QObject>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QTimer>
#include <atomic>
#include <memory>

class SelectedFileMonitor : public QObject
{
    Q_OBJECT
public:
    explicit SelectedFileMonitor(QObject* parent = nullptr);
    ~SelectedFileMonitor() override;
    void watch(const FileDetails& details);
    void clear();
signals:
    void relocated(const FileDetails& details);
    void changed(const FileDetails& details);
    void unavailable();
private:
    struct Result { FileDetails details; FileIdentity identity; bool relocated = false; bool changed = false; };
    void inspect();
    void arm();
    QFileSystemWatcher m_watcher;
    QTimer m_timer;
    QFutureWatcher<Result> m_result;
    FileDetails m_file;
    FileIdentity m_identity;
    quint64 m_generation = 0;
    quint64 m_inspectingGeneration = 0;
    bool m_dirty = false;
    bool m_fileNotification = false;
    bool m_inspecting = false;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};
