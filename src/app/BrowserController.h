#pragma once
#include "UiState.h"
#include <QObject>
class SettingsService;
class QFileSystemWatcher;

class BrowserController : public QObject
{
    Q_OBJECT
public:
    explicit BrowserController(SettingsService* settings, QObject* parent = nullptr);
    void restoreLastDirectory();
    bool openDirectory(const QString& path);
    bool openPath(const QString& path);
    void selectFile(const QString& path);
    const QString& directory() const { return m_directory; }
signals:
    void directoryChanged(const QString& path);
    void fileChanged(const FileDetails& details);
    void fileDetailsRefreshed(const FileDetails& details);
    void errorOccurred(const QString& message);
private:
    bool openLocation(const QString& directory, const QString& file);
    void clearSelectedFile();
    void refreshSelectedFile();
    void checkRootDirectory();
    SettingsService* m_settings;
    QString m_directory;
    QString m_selectedFile;
    QFileSystemWatcher* m_watcher;
    QFileSystemWatcher* m_rootWatcher;
};
