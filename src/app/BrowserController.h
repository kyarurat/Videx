#pragma once
#include "UiState.h"
#include <QObject>
class SettingsService;
class QFileSystemWatcher;
class SelectedFileMonitor;

class BrowserController : public QObject
{
    Q_OBJECT
public:
    explicit BrowserController(SettingsService* settings, QObject* parent = nullptr);
    void restoreLastDirectory();
    bool openDirectory(const QString& path);
    bool openPath(const QString& path);
    void selectFile(const QString& path);
    void dismissFile(const QString& path, const QString& message);
    const QString& directory() const { return m_directory; }
signals:
    void directoryChanged(const QString& path);
    void fileChanged(const FileDetails& details);
    void fileDetailsRefreshed(const FileDetails& details);
    void fileRelocated(const FileDetails& details);
    void fileContentsChanged(const FileDetails& details);
    void statusMessage(const QString& message);
    void errorOccurred(const QString& message);
private:
    bool openLocation(const QString& directory, const QString& file);
    void clearSelectedFile();
    void checkRootDirectory();
    SettingsService* m_settings;
    QString m_directory;
    QString m_selectedFile;
    SelectedFileMonitor* m_monitor;
    QFileSystemWatcher* m_rootWatcher;
};
