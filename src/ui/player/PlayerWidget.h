#pragma once
#include "app/UiState.h"
#include <QWidget>
#include <QStringList>
class QLabel;
class QStackedWidget;
class EmptyState;
class PlayerControls;
class ThemeManager;
class QPushButton;
class ImagePane;

class PlayerWidget : public QWidget
{
    Q_OBJECT
public:
    enum class PlaybackMedia { None, Video, Audio };
    explicit PlayerWidget(ThemeManager* theme, QWidget* parent = nullptr);
    void setDirectory(const QString& path);
    void setFile(const FileDetails& details);
    void refreshFileDetails(const FileDetails& details);
    void setNotice(const QString& message);
    // Set from a loaded playback session, not a selected file's extension.
    // Pausing keeps the current media type; closing playback resets it to None.
    void setPlaybackMedia(PlaybackMedia media);
    void setFullscreen(bool fullscreen);
    void setExplorerVisible(bool visible);
    QString currentPath() const { return m_currentFile.path; }
    void prefetchImages(const QStringList& paths);
signals:
    void openRequested();
    void fullscreenRequested();
    void showExplorerRequested();
    void imageNavigationRequested(int direction);
private:
    QLabel* m_title;
    QLabel* m_notice;
    QStackedWidget* m_stack;
    EmptyState* m_empty;
    QLabel* m_name;
    QLabel* m_path;
    QLabel* m_size;
    QLabel* m_type;
    QLabel* m_modified;
    QString m_directory;
    PlayerControls* m_controls;
    QPushButton* m_showExplorer;
    ImagePane* m_imagePane;
    QLabel* m_formatHint;
    FileDetails m_currentFile;
};
