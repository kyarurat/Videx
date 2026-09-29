#include "BrowserController.h"
#include "services/SettingsService.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QFileSystemWatcher>

BrowserController::BrowserController(SettingsService* settings, QObject* parent)
    : QObject(parent), m_settings(settings), m_watcher(new QFileSystemWatcher(this)),
      m_rootWatcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        if (path == m_selectedFile) refreshSelectedFile();
    });
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString&) {
        if (!m_selectedFile.isEmpty() && !QFileInfo(m_selectedFile).isFile())
            selectFile(m_selectedFile);
    });
    connect(m_rootWatcher, &QFileSystemWatcher::directoryChanged, this, &BrowserController::checkRootDirectory);
}

void BrowserController::checkRootDirectory()
{
    if (m_directory.isEmpty() || QFileInfo(m_directory).isDir()) return;
    const QString missingDirectory = m_directory;
    const auto paths = m_rootWatcher->directories();
    if (!paths.isEmpty()) m_rootWatcher->removePaths(paths);
    clearSelectedFile();
    m_directory.clear();
    emit directoryChanged({});
    emit fileChanged({});
    emit errorOccurred(tr("已打开的文件夹已被重命名、移动或删除：%1\n请重新选择文件夹。")
                           .arg(QDir::toNativeSeparators(missingDirectory)));
}

void BrowserController::refreshSelectedFile()
{
    checkRootDirectory();
    if (m_selectedFile.isEmpty()) return;
    const QFileInfo info(m_selectedFile);
    if (!info.isFile()) { selectFile(m_selectedFile); return; }
    // Atomic saves may replace the watched inode; re-arm without opening the file again.
    if (!m_watcher->files().contains(m_selectedFile) && !m_watcher->addPath(m_selectedFile))
        qWarning() << "Cannot watch selected file:" << m_selectedFile;
    emit fileDetailsRefreshed({info.absoluteFilePath(), info.fileName(), info.suffix(), info.size(), info.lastModified()});
}

void BrowserController::clearSelectedFile()
{
    const QStringList paths = m_watcher->files() + m_watcher->directories();
    if (!paths.isEmpty()) m_watcher->removePaths(paths);
    m_selectedFile.clear();
}

void BrowserController::restoreLastDirectory()
{
    if (m_settings->restoreDirectory() && !m_settings->lastDirectory().isEmpty())
        openDirectory(m_settings->lastDirectory());
}

bool BrowserController::openDirectory(const QString& path)
{
    return openLocation(path, {});
}

bool BrowserController::openLocation(const QString& path, const QString& file)
{
    if (path.isEmpty()) return false; // Cancelling the picker leaves the current session untouched.
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir() || !info.isReadable()) {
        qWarning() << "Cannot open directory:" << path;
        emit errorOccurred(tr("无法打开文件夹：%1\n目录可能已被移动、删除或无法访问，请重新选择。").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    m_directory = QDir::cleanPath(info.absoluteFilePath());
    const auto oldRoots = m_rootWatcher->directories();
    if (!oldRoots.isEmpty()) m_rootWatcher->removePaths(oldRoots);
    QStringList rootPaths{m_directory, info.absolutePath()};
    rootPaths.removeDuplicates();
    const auto failedRoots = m_rootWatcher->addPaths(rootPaths);
    if (!failedRoots.isEmpty()) qWarning() << "Cannot watch root directory paths:" << failedRoots;
    clearSelectedFile();
    emit directoryChanged(m_directory);
    if (file.isEmpty()) emit fileChanged({});
    else selectFile(file);
    if (!m_settings->saveLastDirectory(m_directory))
        emit errorOccurred(tr("文件夹已打开，但无法保存上次目录。请检查应用配置的写入权限。"));
    return true;
}

void BrowserController::selectFile(const QString& path)
{
    if (path.isEmpty() || m_directory.isEmpty()) { clearSelectedFile(); emit fileChanged({}); return; }
    const QFileInfo info(path);
    if (info.isDir() && path != m_selectedFile) return; // Folder navigation preserves the current media.
    if (!info.exists() || !info.isFile()) {
        const QString missingPath = path;
        clearSelectedFile();
        emit fileChanged({});
        emit errorOccurred(tr("无法读取文件信息：%1\n文件可能已被重命名、移动或删除，请重新选择。").arg(QDir::toNativeSeparators(missingPath)));
        return;
    }
    clearSelectedFile();
    m_selectedFile = info.absoluteFilePath();
    const auto failed = m_watcher->addPaths({m_selectedFile, info.absolutePath()});
    if (!failed.isEmpty()) qWarning() << "Cannot watch selected file paths:" << failed;
    emit fileChanged({info.absoluteFilePath(), info.fileName(), info.suffix(), info.size(), info.lastModified()});
}

bool BrowserController::openPath(const QString& path)
{
    if (path.isEmpty()) return false;
    const QFileInfo info(path);
    if (info.isDir()) return openDirectory(path);
    if (!info.exists() || !info.isFile()) {
        emit errorOccurred(tr("无法打开：%1\n文件或文件夹可能已被移动或删除，请重新选择。").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    return openLocation(info.absolutePath(), info.absoluteFilePath());
}
