#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
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
    QTimer m_seekTimer;
    QTimer m_rendererTimer;
    double m_relativeSeek = 0;
};
