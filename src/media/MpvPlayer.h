#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <optional>
#include <QFuture>
#include "services/ExternalAccessGuard.h"
#include "services/FileIdentity.h"
struct mpv_handle;
struct mpv_render_context;
struct mpv_event_property;

struct PlaybackSnapshot
{
    bool loaded = false;
    bool video = false;
    bool paused = false;
    bool seekable = false;
    bool muted = false;
    double position = 0;
    double duration = 0;
    double volume = 70;
    double rate = 1;
    QString title;
    QString artist;
    QString album;
    bool ended = false;
    bool suspended = false;
    bool restoring = false;
    bool accessProtected = false;
};

class MpvPlayer : public QObject
{
    Q_OBJECT
public:
    enum class RenderMode { OpenGL, Headless, Probe, Audio };
    explicit MpvPlayer(QObject* parent = nullptr);
    ~MpvPlayer() override;
    void open(const QString& path, double rate, double volume, RenderMode mode = RenderMode::OpenGL);
    void stop();
    void relocate(const QString& path);
    // Useful for unsupported filesystems and deterministic fallback checks.
    void setExternalProtectionEnabled(bool enabled);
    void togglePause();
    void seek(double seconds, bool relative = false);
    void setVolume(double volume);
    void adjustVolume(double delta);
    void toggleMute();
    void setMuted(bool muted);
    void setRate(double rate);
    bool initializeRenderer(void* (*getProcAddress)(void*, const char*), void* context);
    void releaseRenderer();
    void renderVideo(int framebuffer, int width, int height);
    const PlaybackSnapshot& state() const { return m_state; }
signals:
    void stateChanged();
    void failed(const QString& message);
    void operationFailed(const QString& message);
    void finished();
    void rendererRequested();
    void renderRequested();
    void rendererReleaseRequested();
    void freezeFrameRequested();
    void frameRestored();
    void fileReleased(const QString& path);
    void interrupted(const QString& path, const QString& message);
    void statusMessage(const QString& message);
private:
    void drainEvents();
    void applyProperty(const mpv_event_property& property);
    void setNumber(const char* name, double value);
    void loadPendingFile();
    void destroyEngine();
    void resetSession();
    void configureVideoOutput();
    void requestRenderer();
    void flushRelativeSeek();
    void updateIdleRelease();
    void suspendFile();
    void restoreFile(bool play, double position);
    bool validateRestoreSource();
    void sendRestoreSeek();
    void guardBroken(ExternalAccessGuard* guard, ExternalAccessGuard::BreakReason reason);
    void releaseGuard(qint64 entry);
    void finishGuard(ExternalAccessGuard* guard, bool sourceReleased = true);
    void openDeferredFile();
    void destroyEngineAsync();
    mpv_handle* m_handle = nullptr;
    quint64 m_generation = 0;
    PlaybackSnapshot m_state;
    bool m_completionEmitted = false;
    bool m_requestedMuted = false;
    mpv_render_context* m_renderer = nullptr;
    QString m_pendingFile;
    RenderMode m_mode = RenderMode::Headless;
    bool m_stopAcknowledged = false;
    quint64 m_loadGeneration = 0;
    qint64 m_activeEntry = -1;
    int m_initialReplies = 0;
    QTimer m_seekTimer;
    QTimer m_rendererTimer;
    double m_relativeSeek = 0;
    QString m_path;
    FileIdentity m_fileIdentity;
    QTimer m_idleReleaseTimer;
    QTimer m_restoreTimer;
    QTimer m_suspendTimer;
    QHash<qint64, ExternalAccessGuard*> m_guards;
    QHash<ExternalAccessGuard*, QString> m_guardPaths;
    QSet<ExternalAccessGuard*> m_yielding;
    ExternalAccessGuard* m_loadingGuard = nullptr;
    ExternalAccessGuard* m_activeGuard = nullptr;
    bool m_protectionEnabled = true;
    bool m_suspending = false;
    bool m_pathYielding = false;
    bool m_pathYieldPlay = false;
    qint64 m_suspendEntry = -1;
    bool m_restorePending = false;
    bool m_restoreOpening = false;
    bool m_restoring = false;
    bool m_restoreSeekSent = false;
    bool m_restoreWantPlay = false;
    bool m_restoreEnded = false;
    double m_restorePosition = 0;
    double m_restoreIssuedPosition = 0;
    struct OpenRequest { QString path; double rate; double volume; RenderMode mode; };
    std::optional<OpenRequest> m_deferredOpen;
    bool m_destroying = false;
    QFuture<void> m_destroyFuture;
};
