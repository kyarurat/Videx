#include "MpvPlayer.h"
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <QDir>
#include <QMetaObject>
#include <QDebug>
#include <QTimer>
#include <algorithm>
#include <cmath>

MpvPlayer::MpvPlayer(QObject* parent) : QObject(parent)
{
    m_seekTimer.setSingleShot(true);
    m_seekTimer.setInterval(100);
    connect(&m_seekTimer, &QTimer::timeout, this, &MpvPlayer::flushRelativeSeek);
    m_rendererTimer.setSingleShot(true);
    m_rendererTimer.setInterval(5000);
    connect(&m_rendererTimer, &QTimer::timeout, this, [this] {
        if (m_mode != RenderMode::OpenGL || m_renderer) return;
        stop();
        emit failed(tr("无法创建视频显示画布。请检查显卡驱动和 OpenGL 支持。"));
    });
}
MpvPlayer::~MpvPlayer() { destroyEngine(); }

void MpvPlayer::destroyEngine()
{
    if (m_handle) {
        emit rendererReleaseRequested();
        releaseRenderer();
        mpv_set_wakeup_callback(m_handle, nullptr, nullptr);
        mpv_terminate_destroy(m_handle);
        m_handle = nullptr;
    }
}

void MpvPlayer::resetSession()
{
    ++m_generation;
    m_seekTimer.stop();
    m_rendererTimer.stop();
    m_relativeSeek = 0;
    m_stopAcknowledged = false;
    m_loadGeneration = 0;
    m_activeEntry = -1;
    m_state = {};
    m_pendingFile.clear();
    m_completionEmitted = false;
    emit stateChanged();
}

void MpvPlayer::stop()
{
    resetSession();
    if (m_handle) {
        const char* command[]{"stop", nullptr};
        mpv_command_async(m_handle, m_generation * 8 + 1, command);
        // A switch can happen while draining a nonempty mpv event queue. Its
        // wakeup callback is edge-triggered, so explicitly resume draining.
        QMetaObject::invokeMethod(this, &MpvPlayer::drainEvents, Qt::QueuedConnection);
    }
}

void MpvPlayer::open(const QString& path, double rate, double volume, RenderMode mode)
{
    // A loaded session has already delivered START_FILE/FILE_LOADED. Replace
    // it directly, allowing mpv to reuse a compatible audio output. Pending
    // loads still use the stop barrier so their START_FILE cannot be mistaken
    // for the newest request.
    const bool replaceLoaded = m_handle && m_state.loaded && (mode != RenderMode::OpenGL || m_renderer);
    if (replaceLoaded) { resetSession(); m_stopAcknowledged = true; }
    else stop();
    m_mode = mode;
    m_pendingFile = path;
    m_state.rate = rate;
    m_state.volume = std::clamp(volume, 0.0, 100.0);
    if (m_handle) {
        setNumber("speed", rate);
        setNumber("volume", m_state.volume);
        int pause = mode == RenderMode::Probe ? 1 : 0;
        mpv_set_property_async(m_handle, 0, "pause", MPV_FORMAT_FLAG, &pause);
        configureVideoOutput();
        if (mode == RenderMode::OpenGL && !m_renderer) requestRenderer();
        if (replaceLoaded) loadPendingFile();
        return; // The stop acknowledgement releases the newest pending load.
    }
    m_handle = mpv_create();
    if (!m_handle) { emit failed(tr("无法创建播放引擎。")); return; }
    const auto option = [this](const char* name, const char* value) {
        return mpv_set_option_string(m_handle, name, value) >= 0;
    };
    const bool probing = mode == RenderMode::Probe;
    const bool headless = mode != RenderMode::OpenGL;
    const bool configured = option("config", "no") && option("terminal", "no")
        && option("input-default-bindings", "no") && option("input-vo-keyboard", "no")
        && option("osc", "no") && option("idle", "yes") && option("keep-open", "yes") && option("volume-max", "100")
        && option("hwdec", "auto-safe") && option("audio-display", "no")
        && option("gapless-audio", "yes")
        && option("vo", headless ? "null" : "libmpv")
        && option("mute", probing || m_requestedMuted ? "yes" : "no")
        && (!probing || (option("ao", "null") && option("pause", "yes")));
    const int error = configured ? mpv_initialize(m_handle) : MPV_ERROR_OPTION_ERROR;
    if (error < 0) {
        const QString message = QString::fromUtf8(mpv_error_string(error));
        destroyEngine();
        stop();
        emit failed(tr("无法初始化播放引擎：%1").arg(message));
        return;
    }
    // Only enqueue onto Qt's thread from mpv's callback. The callback is removed
    // and the handle joined before QObject destruction; old queued work is gated.
    mpv_set_wakeup_callback(m_handle, [](void* context) {
        auto* player = static_cast<MpvPlayer*>(context);
        QMetaObject::invokeMethod(player, &MpvPlayer::drainEvents, Qt::QueuedConnection);
    }, this);
    // The GUI thread also renders video: never wait for the mpv core here.
    // Typed observations deliver owned event values without synchronous getters.
    for (const char* property : {"time-pos", "duration", "volume", "speed"})
        mpv_observe_property(m_handle, 0, property, MPV_FORMAT_DOUBLE);
    for (const char* property : {"pause", "seekable", "mute", "eof-reached"})
        mpv_observe_property(m_handle, 0, property, MPV_FORMAT_FLAG);
    for (const char* property : {"media-title", "metadata/by-key/artist", "metadata/by-key/album"})
        mpv_observe_property(m_handle, 0, property, MPV_FORMAT_STRING);
    mpv_observe_property(m_handle, 0, "track-list", MPV_FORMAT_NODE);
    setNumber("speed", rate);
    setNumber("volume", std::clamp(volume, 0.0, 100.0));
    m_stopAcknowledged = true; // A newly created idle core has no old session.
    if (headless) loadPendingFile();
    else {
        if (m_renderer) loadPendingFile();
        else requestRenderer();
    }
}

void MpvPlayer::loadPendingFile()
{
    if (!m_handle || !m_stopAcknowledged || m_pendingFile.isEmpty()
        || (m_mode == RenderMode::OpenGL && !m_renderer)) return;
    const auto encoded = QDir::toNativeSeparators(m_pendingFile).toUtf8();
    m_pendingFile.clear();
    const char* command[]{"loadfile", encoded.constData(), "replace", nullptr};
    m_loadGeneration = m_generation;
    const int result = mpv_command_async(m_handle, m_generation * 8 + 2, command);
    if (result < 0) {
        stop();
        emit failed(tr("无法打开媒体：%1").arg(QString::fromUtf8(mpv_error_string(result))));
    }
}

void MpvPlayer::configureVideoOutput()
{
    if (!m_handle) return;
    const char* output = m_mode == RenderMode::OpenGL ? "libmpv" : "null";
    if (m_mode == RenderMode::OpenGL && !m_renderer) return;
    mpv_set_property_async(m_handle, 0, "vo", MPV_FORMAT_STRING, &output);
}

void MpvPlayer::requestRenderer()
{
    // Arm before the signal: a valid canvas can initialize synchronously and
    // cancel this timer. Session reset cancels waits belonging to old files.
    m_rendererTimer.start();
    emit rendererRequested();
}

bool MpvPlayer::initializeRenderer(void* (*getProcAddress)(void*, const char*), void* context)
{
    if (!m_handle || m_renderer) return m_renderer != nullptr;
    mpv_opengl_init_params initialization{getProcAddress, context};
    mpv_render_param parameters[]{
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &initialization},
        {MPV_RENDER_PARAM_INVALID, nullptr}};
    const int result = mpv_render_context_create(&m_renderer, m_handle, parameters);
    if (result < 0) {
        const QString message = QString::fromUtf8(mpv_error_string(result));
        stop();
        qWarning() << "Video renderer initialization failed:" << message;
        emit failed(tr("无法初始化视频显示：%1").arg(message));
        return false;
    }
    m_rendererTimer.stop();
    mpv_render_context_set_update_callback(m_renderer, [](void* context) {
        auto* player = static_cast<MpvPlayer*>(context);
        QMetaObject::invokeMethod(player, [player] { emit player->renderRequested(); }, Qt::QueuedConnection);
    }, this);
    configureVideoOutput();
    loadPendingFile();
    return true;
}

void MpvPlayer::releaseRenderer()
{
    if (!m_renderer) return;
    mpv_render_context_set_update_callback(m_renderer, nullptr, nullptr);
    mpv_render_context_free(m_renderer);
    m_renderer = nullptr;
}

void MpvPlayer::renderVideo(int framebuffer, int width, int height)
{
    if (!m_renderer || width <= 0 || height <= 0) return;
    mpv_opengl_fbo target{framebuffer, width, height, 0};
    int flip = 1;
    int block = 0;
    mpv_render_param parameters[]{
        {MPV_RENDER_PARAM_OPENGL_FBO, &target},
        {MPV_RENDER_PARAM_FLIP_Y, &flip},
        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
        {MPV_RENDER_PARAM_INVALID, nullptr}};
    mpv_render_context_update(m_renderer);
    mpv_render_context_render(m_renderer, parameters);
}

void MpvPlayer::applyProperty(const mpv_event_property& property)
{
    const QByteArray name(property.name);
    if (property.format == MPV_FORMAT_DOUBLE && property.data) {
        const double value = *static_cast<const double*>(property.data);
        if (!std::isfinite(value)) return;
        if (name == "time-pos") m_state.position = value;
        else if (name == "duration") m_state.duration = value;
        else if (name == "volume") m_state.volume = value;
        else if (name == "speed") m_state.rate = value;
    } else if (property.format == MPV_FORMAT_FLAG && property.data) {
        const bool value = *static_cast<const int*>(property.data) != 0;
        if (name == "pause") m_state.paused = value;
        else if (name == "seekable") m_state.seekable = value;
        else if (name == "mute") m_state.muted = value;
        else if (name == "eof-reached") m_state.ended = value;
    } else if (property.format == MPV_FORMAT_STRING && property.data) {
        const QString value = QString::fromUtf8(*static_cast<char* const*>(property.data));
        if (name == "media-title") m_state.title = value;
        else if (name == "metadata/by-key/artist") m_state.artist = value;
        else if (name == "metadata/by-key/album") m_state.album = value;
    } else if (name == "track-list" && property.format == MPV_FORMAT_NODE && property.data) {
        const auto& tracks = *static_cast<const mpv_node*>(property.data);
        m_state.video = false;
        if (tracks.format == MPV_FORMAT_NODE_ARRAY) {
            for (int i = 0; i < tracks.u.list->num; ++i) {
                const auto& track = tracks.u.list->values[i];
                if (track.format != MPV_FORMAT_NODE_MAP) continue;
                bool video = false, selected = false, cover = false;
                for (int j = 0; j < track.u.list->num; ++j) {
                    const QByteArray key(track.u.list->keys[j]);
                    const auto& value = track.u.list->values[j];
                    if (key == "type" && value.format == MPV_FORMAT_STRING) video = QByteArray(value.u.string) == "video";
                    if (key == "selected" && value.format == MPV_FORMAT_FLAG) selected = value.u.flag;
                    if (key == "albumart" && value.format == MPV_FORMAT_FLAG) cover = value.u.flag;
                }
                m_state.video |= video && selected && !cover;
            }
        }
    } else if (property.format == MPV_FORMAT_NONE) {
        if (name == "time-pos") m_state.position = 0;
        else if (name == "duration") m_state.duration = 0;
        else if (name == "media-title") m_state.title.clear();
        else if (name == "metadata/by-key/artist") m_state.artist.clear();
        else if (name == "metadata/by-key/album") m_state.album.clear();
    }
}

void MpvPlayer::drainEvents()
{
    if (!m_handle) return;
    const auto generation = m_generation;
    bool changed = false;
    while (m_handle && generation == m_generation) {
        const auto* event = mpv_wait_event(m_handle, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        if (event->event_id == MPV_EVENT_COMMAND_REPLY && event->reply_userdata == generation * 8 + 1) {
            if (event->error < 0) { emit failed(tr("无法停止上一段媒体。")); continue; }
            m_stopAcknowledged = true;
            loadPendingFile();
        } else if (event->event_id == MPV_EVENT_START_FILE && m_loadGeneration == generation) {
            m_activeEntry = static_cast<mpv_event_start_file*>(event->data)->playlist_entry_id;
        } else if (event->event_id == MPV_EVENT_FILE_LOADED && m_activeEntry >= 0 && m_loadGeneration == generation) {
            // Publish the session after an asynchronous track snapshot so the
            // UI gets the actual media category, including audio-only MP4.
            if (mpv_get_property_async(m_handle, generation * 8 + 3, "track-list", MPV_FORMAT_NODE) < 0) {
                stop();
                emit failed(tr("无法读取媒体流信息。请重新打开文件。"));
            }
            // Observations may have been consumed while an old session was
            // draining. Refresh typed values for this load without GUI waits.
            for (const auto* name : {"time-pos", "duration", "volume", "speed"})
                mpv_get_property_async(m_handle, generation * 8 + 4, name, MPV_FORMAT_DOUBLE);
            for (const auto* name : {"pause", "seekable", "mute", "eof-reached"})
                mpv_get_property_async(m_handle, generation * 8 + 4, name, MPV_FORMAT_FLAG);
            for (const auto* name : {"media-title", "metadata/by-key/artist", "metadata/by-key/album"})
                mpv_get_property_async(m_handle, generation * 8 + 4, name, MPV_FORMAT_STRING);
        } else if ((event->event_id == MPV_EVENT_PROPERTY_CHANGE && m_state.loaded)
            || (event->event_id == MPV_EVENT_GET_PROPERTY_REPLY && m_activeEntry >= 0
                && (event->reply_userdata == generation * 8 + 3 || event->reply_userdata == generation * 8 + 4))) {
            const bool loaded = event->event_id == MPV_EVENT_GET_PROPERTY_REPLY && event->reply_userdata == generation * 8 + 3;
            if (loaded && event->error < 0) {
                stop();
                emit failed(tr("无法读取媒体流信息。请重新打开文件。"));
                continue;
            }
            const auto* property = static_cast<mpv_event_property*>(event->data);
            if (!property) continue;
            applyProperty(*property);
            if (loaded) m_state.loaded = true;
            changed = true;
            if (loaded && m_state.video && m_mode == RenderMode::Audio) {
                // A misleading audio extension can still contain real video.
                // Upgrade output after inspecting streams, keeping this session.
                m_mode = RenderMode::OpenGL;
                emit stateChanged(); // Show the GL widget before requesting it.
                if (!m_renderer) requestRenderer();
                else configureVideoOutput();
            }
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            const auto* end = static_cast<mpv_event_end_file*>(event->data);
            if (m_loadGeneration == generation && end->playlist_entry_id == m_activeEntry && end->reason == MPV_END_FILE_REASON_ERROR) {
                const auto message = QString::fromUtf8(mpv_error_string(end->error));
                qWarning() << "Media playback failed:" << message;
                stop();
                emit failed(tr("无法播放此文件：%1").arg(message));
            }
        } else if ((event->event_id == MPV_EVENT_COMMAND_REPLY || event->event_id == MPV_EVENT_SET_PROPERTY_REPLY) && event->error < 0) {
            const QString message = tr("播放操作失败：%1").arg(QString::fromUtf8(mpv_error_string(event->error)));
            if (event->reply_userdata == generation * 8 + 2) {
                stop();
                emit failed(message);
            } else if (event->reply_userdata == 0) emit operationFailed(message);
        }
    }
    if (generation != m_generation) {
        QMetaObject::invokeMethod(this, &MpvPlayer::drainEvents, Qt::QueuedConnection);
        return;
    }
    if (changed) emit stateChanged();
    if (generation != m_generation) return;
    if (!m_state.ended) m_completionEmitted = false;
    else if (m_state.loaded && !m_completionEmitted) {
        m_completionEmitted = true;
        emit finished();
    }
}

void MpvPlayer::setNumber(const char* name, double value)
{
    if (m_handle && std::isfinite(value)) mpv_set_property_async(m_handle, 0, name, MPV_FORMAT_DOUBLE, &value);
}
void MpvPlayer::togglePause()
{
    if (!m_handle || !m_state.loaded) return;
    if (m_state.ended) {
        seek(0);
        int pause = 0;
        mpv_set_property_async(m_handle, 0, "pause", MPV_FORMAT_FLAG, &pause);
    } else {
        const char* command[]{"cycle", "pause", nullptr};
        mpv_command_async(m_handle, 0, command);
    }
}
void MpvPlayer::seek(double seconds, bool relative)
{
    if (!m_handle || !m_state.loaded || !m_state.seekable || !std::isfinite(seconds)) return;
    if (relative) {
        // Preserve every requested second, while issuing at most ten expensive
        // exact seeks per second during keyboard autorepeat.
        m_relativeSeek += seconds;
        if (!m_seekTimer.isActive()) m_seekTimer.start();
        return;
    }
    m_relativeSeek = 0;
    m_seekTimer.stop();
    const auto value = QByteArray::number(seconds, 'f', 3);
    const char* command[]{"seek", value.constData(), relative ? "relative+exact" : "absolute+exact", nullptr};
    mpv_command_async(m_handle, 0, command);
}
void MpvPlayer::flushRelativeSeek()
{
    const double delta = m_relativeSeek;
    m_relativeSeek = 0;
    if (!m_handle || !m_state.loaded || !m_state.seekable || delta == 0) return;
    const auto value = QByteArray::number(delta, 'f', 3);
    const char* command[]{"seek", value.constData(), "relative+exact", nullptr};
    mpv_command_async(m_handle, 0, command);
}
void MpvPlayer::setVolume(double volume) { setNumber("volume", std::clamp(volume, 0.0, 100.0)); }
void MpvPlayer::adjustVolume(double delta)
{
    if (!m_handle || !m_state.loaded || !std::isfinite(delta)) return;
    const auto value = QByteArray::number(delta);
    const char* command[]{"add", "volume", value.constData(), nullptr};
    mpv_command_async(m_handle, 0, command);
}
void MpvPlayer::setRate(double rate) { setNumber("speed", std::clamp(rate, 0.5, 2.0)); }
void MpvPlayer::toggleMute()
{
    if (m_handle) setMuted(!m_requestedMuted);
}
void MpvPlayer::setMuted(bool muted)
{
    // Keep the user's choice across stop/open, including a switch before the
    // asynchronous property notification for the old session arrives.
    m_requestedMuted = muted;
    if (!m_handle) return;
    int value = muted ? 1 : 0;
    mpv_set_property_async(m_handle, 0, "mute", MPV_FORMAT_FLAG, &value);
}
