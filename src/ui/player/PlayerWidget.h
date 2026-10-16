#pragma once
#include "app/UiState.h"
#include <QWidget>
#include <QStringList>
#include "media/MediaType.h"
#include "services/SettingsService.h"
#include <optional>
class QLabel;
class QStackedWidget;
class EmptyState;
class PlayerControls;
class ThemeManager;
class QPushButton;
class ImagePane;
class MpvPlayer;
class QTimer;

class PlayerWidget : public QWidget
{
    Q_OBJECT
public:
    enum class PlaybackMedia { None, Video, Audio };
    explicit PlayerWidget(ThemeManager* theme, QWidget* parent = nullptr);
    ~PlayerWidget() override;
    void captureSession(SavedSession& session) const;
    void restoreSession(const SavedSession& session);
    void setDirectory(const QString& path);
    void setFile(const FileDetails& details);
    void refreshFileDetails(const FileDetails& details);
    void relocateFile(const FileDetails& details);
    void fileContentsChanged(const FileDetails& details);
    void setNotice(const QString& message);
    // Set from a loaded playback session, not a selected file's extension.
    // Pausing keeps the current media type; closing playback resets it to None.
    void setPlaybackMedia(PlaybackMedia media);
    void setFullscreen(bool fullscreen);
    void setExplorerVisible(bool visible);
    QString currentPath() const { return m_currentFile.path; }
    void prefetchImages(const QStringList& paths);
    void applyPlaybackPreferences(double rate, bool autoNext);
    void togglePause();
    void toggleMute();
    MediaType playbackCategory() const;
    void seekFromKeyboard(int direction, bool autoRepeat);
    void handleSeekKey(int direction, bool pressed, bool autoRepeat);
    void cancelSeekHold();
signals:
    void openRequested();
    void fullscreenRequested();
    void showExplorerRequested();
    void imageNavigationRequested(int direction);
    void mediaNavigationRequested(int direction);
    void playbackSeekingEnabledChanged(bool enabled);
    void playbackResolved(const QString& path, MediaType category);
    void fileInterrupted(const QString& path, const QString& message);
    void statusMessage(const QString& message);
private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void syncPlayback();
    bool m_restoringSession = false;
    std::optional<SavedSession> m_pendingSession;
    MpvPlayer* m_playback;
    QWidget* m_playbackPane;
    QWidget* m_videoSurface;
    QWidget* m_audioPane;
    QLabel* m_audioTitle;
    QLabel* m_audioInfo;
    double m_defaultRate = 1;
    double m_volume = 70;
    bool m_autoNext = false;
    bool m_capabilityAnnounced = false;
    bool m_lastAccessProtected = false;
    bool m_wasSuspended = false;
    QTimer* m_seekHoldTimer;
    int m_seekKeyDirection = 0;
    bool m_seekHoldActive = false;
    double m_rateBeforeHold = 1;
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
    MediaType m_resolvedCategory = MediaType::Unknown;
    QString m_audioInfoPath;
    QString m_audioArtist;
    QString m_audioAlbum;
};
