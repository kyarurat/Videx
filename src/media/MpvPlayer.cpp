#include "MpvPlayer.h"
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QDebug>
#include <QTimer>
#include <QPointer>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <clocale>

MpvPlayer::MpvPlayer(QObject* parent) : QObject(parent)
{
    // QApplication adopts the desktop locale. libmpv requires C numeric
    // syntax; set it once before any mpv core can start its worker threads.
    static const auto numericLocale = std::setlocale(LC_NUMERIC, "C");
    Q_UNUSED(numericLocale);
    m_seekTimer.setSingleShot(true);
    m_seekTimer.setInterval(100);
    connect(&m_seekTimer, &QTimer::timeout, this, &MpvPlayer::flushRelativeSeek);
    m_idleReleaseTimer.setSingleShot(true);
    m_idleReleaseTimer.setInterval(5000);
    connect(&m_idleReleaseTimer, &QTimer::timeout, this, &MpvPlayer::suspendFile);
    m_restoreTimer.setSingleShot(true);
    m_restoreTimer.setInterval(10000);
    connect(&m_restoreTimer, &QTimer::timeout, this, [this] {
        const auto path = m_path;
        stop();
        emit interrupted(path, tr("恢复播放超时，已关闭媒体，请重新打开。"));
    });
    m_suspendTimer.setSingleShot(true);
    m_suspendTimer.setInterval(2000);
    connect(&m_suspendTimer, &QTimer::timeout, this, [this] {
        if (m_suspending) destroyEngineAsync();
    });
    m_rendererTimer.setSingleShot(true);
    m_rendererTimer.setInterval(5000);
    connect(&m_rendererTimer, &QTimer::timeout, this, [this] {
        if (m_mode != RenderMode::OpenGL || m_renderer) return;
        stop();
        emit failed(tr("无法创建视频显示画布。请检查显卡驱动和 OpenGL 支持。"));
    });
}
MpvPlayer::~MpvPlayer() { m_destroyFuture.waitForFinished(); destroyEngine(); }

void MpvPlayer::destroyEngine()
{
    if (m_handle) {
        emit rendererReleaseRequested();
        releaseRenderer();
        mpv_set_wakeup_callback(m_handle, nullptr, nullptr);
        mpv_terminate_destroy(m_handle);
        m_handle = nullptr;
    }
    const auto guards = m_guardPaths.keys();
    for (auto* guard : guards) { guard->acknowledge(); delete guard; }
    m_guardPaths.clear();
    m_guards.clear();
    m_yielding.clear();
    m_loadingGuard = m_activeGuard = nullptr;
}

void MpvPlayer::resetSession()
{
    ++m_generation;
    m_seekTimer.stop();
    m_rendererTimer.stop();
    m_idleReleaseTimer.stop();
    m_restoreTimer.stop();
    m_suspendTimer.stop();
    m_suspending = m_restoring = m_restorePending = m_restoreSeekSent = false;
    m_pathYielding = false;
    m_suspendEntry = -1;
    m_relativeSeek = 0;
    m_stopAcknowledged = false;
    m_loadGeneration = 0;
    m_activeEntry = -1;
    m_initialReplies = 0;
    m_state = {};
    if (!m_restoreOpening) emit frameRestored();
    m_pendingFile.clear();
    m_path.clear();
    m_fileIdentity = {};
    m_completionEmitted = false;
    emit stateChanged();
}

void MpvPlayer::stop()
{
    m_deferredOpen.reset();
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
    if (!m_yielding.isEmpty() || m_destroying) {
        m_deferredOpen = OpenRequest{path, rate, volume, mode};
        return;
    }
    // A loaded session has already delivered START_FILE/FILE_LOADED. Replace
    // it directly, allowing mpv to reuse a compatible audio output. Pending
    // loads still use the stop barrier so their START_FILE cannot be mistaken
    // for the newest request.
    const bool replaceLoaded = m_handle && m_state.loaded && (mode != RenderMode::OpenGL || m_renderer);
    if (replaceLoaded) { resetSession(); m_stopAcknowledged = true; }
    else stop();
    m_mode = mode;
    m_pendingFile = path;
    m_path = path;
    m_fileIdentity = FileIdentity::read(path);
    m_state.rate = rate;
    m_state.volume = std::clamp(volume, 0.0, 100.0);
    if (m_handle) {
        setNumber("speed", rate);
        setNumber("volume", m_state.volume);
        int pause = mode == RenderMode::Probe || m_restoreOpening ? 1 : 0;
        mpv_set_property_async(m_handle, m_generation * 8 + 7, "pause", MPV_FORMAT_FLAG, &pause);
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
        && (!(probing || m_restoreOpening) || option("pause", "yes"))
        && (!probing || option("ao", "null"));
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
    auto* guard = new ExternalAccessGuard(this);
    m_guardPaths.insert(guard, m_pendingFile);
    m_loadingGuard = guard;
    m_state.accessProtected = m_protectionEnabled && guard->arm(m_pendingFile);
    connect(guard, &ExternalAccessGuard::broken, this, [this, guard](auto reason) { guardBroken(guard, reason); });
    const auto encoded = QDir::toNativeSeparators(m_pendingFile).toUtf8();
    m_pendingFile.clear();
    const char* command[]{"loadfile", encoded.constData(), "replace", nullptr};
    m_loadGeneration = m_generation;
    const int result = mpv_command_async(m_handle, m_generation * 8 + 2, command);
    if (result < 0) {
        finishGuard(guard);
        stop();
        emit failed(tr("无法打开媒体：%1").arg(QString::fromUtf8(mpv_error_string(result))));
    }
}

void MpvPlayer::configureVideoOutput()
{
    if (!m_handle) return;
    const char* output = m_mode == RenderMode::OpenGL ? "libmpv" : "null";
    if (m_mode == RenderMode::OpenGL && !m_renderer) return;
    mpv_set_property_async(m_handle, m_generation * 8 + 7, "vo", MPV_FORMAT_STRING, &output);
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
        if (event->event_id == MPV_EVENT_START_FILE && m_loadingGuard) {
            const auto entry = static_cast<mpv_event_start_file*>(event->data)->playlist_entry_id;
            m_guards.insert(entry, m_loadingGuard);
            m_activeGuard = m_loadingGuard;
            m_loadingGuard = nullptr;
        }
        if (event->event_id == MPV_EVENT_END_FILE) {
            const auto entry = static_cast<mpv_event_end_file*>(event->data)->playlist_entry_id;
            releaseGuard(entry);
            if (m_suspending && entry == m_suspendEntry) {
                m_suspendTimer.stop();
                m_suspending = false;
                m_activeEntry = -1;
                changed = true;
                if (m_restorePending) {
                    const auto position = m_restorePosition;
                    const auto play = m_restoreWantPlay;
                    m_restorePending = false;
                    restoreFile(play, position);
                }
            }
        }
        if (generation != m_generation) break;
        if (event->event_id == MPV_EVENT_COMMAND_REPLY && event->reply_userdata == generation * 8 + 1) {
            if (event->error < 0) { emit failed(tr("无法停止上一段媒体。")); continue; }
            // A load cancelled before START_FILE never gets an END_FILE.
            // The stop barrier also retires its not-yet-assigned guard.
            if (m_loadingGuard) finishGuard(m_loadingGuard);
            if (generation != m_generation) break;
            m_stopAcknowledged = true;
            loadPendingFile();
        } else if (event->event_id == MPV_EVENT_START_FILE && m_loadGeneration == generation) {
            m_activeEntry = static_cast<mpv_event_start_file*>(event->data)->playlist_entry_id;
        } else if (event->event_id == MPV_EVENT_FILE_LOADED && m_activeEntry >= 0 && m_loadGeneration == generation) {
            // keep-open can re-pause an exhausted old entry after the pre-load
            // unpause was queued. Apply the new entry's intent after loading.
            int pause = m_mode == RenderMode::Probe || m_restoring ? 1 : 0;
            mpv_set_property_async(m_handle, m_generation * 8 + 7, "pause", MPV_FORMAT_FLAG, &pause);
            m_initialReplies = 1;
            // Publish the session after an asynchronous track snapshot so the
            // UI gets the actual media category, including audio-only MP4.
            if (mpv_get_property_async(m_handle, generation * 8 + 3, "track-list", MPV_FORMAT_NODE) < 0) {
                stop();
                emit failed(tr("无法读取媒体流信息。请重新打开文件。"));
            }
            // Observations may have been consumed while an old session was
            // draining. Refresh typed values for this load without GUI waits.
            const auto snapshot = [this, generation](const char* name, mpv_format format) {
                if (mpv_get_property_async(m_handle, generation * 8 + 4, name, format) >= 0) ++m_initialReplies;
            };
            for (const auto* name : {"time-pos", "duration", "volume", "speed"}) snapshot(name, MPV_FORMAT_DOUBLE);
            for (const auto* name : {"pause", "seekable", "mute", "eof-reached"}) snapshot(name, MPV_FORMAT_FLAG);
            for (const auto* name : {"media-title", "metadata/by-key/artist", "metadata/by-key/album"}) snapshot(name, MPV_FORMAT_STRING);
        } else if ((event->event_id == MPV_EVENT_PROPERTY_CHANGE && m_state.loaded && !m_state.suspended && !m_restoring)
            || (event->event_id == MPV_EVENT_GET_PROPERTY_REPLY && m_activeEntry >= 0
                && (event->reply_userdata == generation * 8 + 3 || event->reply_userdata == generation * 8 + 4))) {
            const bool trackReply = event->event_id == MPV_EVENT_GET_PROPERTY_REPLY && event->reply_userdata == generation * 8 + 3;
            if (trackReply && event->error < 0) {
                stop();
                emit failed(tr("无法读取媒体流信息。请重新打开文件。"));
                continue;
            }
            const bool initialReply = event->event_id == MPV_EVENT_GET_PROPERTY_REPLY && m_initialReplies > 0;
            if (initialReply) --m_initialReplies;
            // loaded means the initial timeline/seekability snapshot is usable,
            // not merely that track-list arrived before the other async replies.
            const bool loaded = initialReply && m_initialReplies == 0;
            const auto* property = static_cast<mpv_event_property*>(event->data);
            if (property) applyProperty(*property);
            if (loaded) m_state.loaded = true;
            changed = true;
            if (loaded && m_restoring) sendRestoreSeek();
            if (loaded && m_state.video && m_mode == RenderMode::Audio) {
                // A misleading audio extension can still contain real video.
                // Upgrade output after inspecting streams, keeping this session.
                m_mode = RenderMode::OpenGL;
                emit stateChanged(); // Show the GL widget before requesting it.
                if (!m_renderer) requestRenderer();
                else configureVideoOutput();
            }
        } else if (event->event_id == MPV_EVENT_PLAYBACK_RESTART && m_restoring && m_restoreSeekSent) {
            if (!validateRestoreSource()) continue;
            if (std::abs(m_restoreIssuedPosition - m_restorePosition) > .001) { sendRestoreSeek(); continue; }
            m_restoring = false;
            m_restoreTimer.stop();
            m_state.restoring = false;
            m_state.paused = !m_restoreWantPlay;
            m_state.position = m_restorePosition;
            m_state.ended = m_restoreEnded;
            if (m_restoreEnded) m_completionEmitted = true;
            int pause = m_state.paused ? 1 : 0;
            mpv_set_property_async(m_handle, m_generation * 8 + 7, "pause", MPV_FORMAT_FLAG, &pause);
            emit frameRestored();
            changed = true;
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            const auto* end = static_cast<mpv_event_end_file*>(event->data);
            if (m_loadGeneration == generation && end->playlist_entry_id == m_activeEntry && end->reason == MPV_END_FILE_REASON_ERROR) {
                const auto message = QString::fromUtf8(mpv_error_string(end->error));
                qWarning() << "Media playback failed:" << message;
                const auto path = m_path;
                const bool restoring = m_restoring;
                stop();
                if (restoring) emit interrupted(path, tr("无法恢复播放：%1").arg(message));
                else emit failed(tr("无法播放此文件：%1").arg(message));
            }
        } else if ((event->event_id == MPV_EVENT_COMMAND_REPLY || event->event_id == MPV_EVENT_SET_PROPERTY_REPLY) && event->error < 0) {
            const QString message = tr("播放操作失败：%1").arg(QString::fromUtf8(mpv_error_string(event->error)));
            if (event->reply_userdata == generation * 8 + 2 || event->reply_userdata == generation * 8 + 6) {
                const auto path = m_path;
                const bool restoring = m_restoring;
                stop();
                if (restoring) emit interrupted(path, message); else emit failed(message);
            } else if (event->reply_userdata == generation * 8 + 7) emit operationFailed(message);
        }
    }
    if (generation != m_generation) {
        QMetaObject::invokeMethod(this, &MpvPlayer::drainEvents, Qt::QueuedConnection);
        return;
    }
    updateIdleRelease();
    if (changed) emit stateChanged();
    if (generation != m_generation) return;
    if (!m_state.ended) m_completionEmitted = false;
    else if (m_state.loaded && !m_state.suspended && !m_restoring && !m_completionEmitted) {
        // A buffered EOF can arrive before the GUI dispatches a pending file
        // notification. Do not auto-advance a file already being removed.
        if (m_activeGuard) {
            const auto reason = m_activeGuard->poll();
            if (reason != ExternalAccessGuard::BreakReason::None) { guardBroken(m_activeGuard, reason); return; }
        }
        if (!QFileInfo(m_path).isFile() || (m_fileIdentity.valid() && FileIdentity::read(m_path) != m_fileIdentity)) {
            m_completionEmitted = true;
            // Let the identity monitor distinguish rename from removal. Release
            // seekable input now, but retain the session until that decision.
            if (m_state.seekable) {
                m_pathYielding = true;
                m_pathYieldPlay = false;
                suspendFile();
            }
            return;
        }
        m_completionEmitted = true;
        if (qEnvironmentVariableIsSet("VIDEX_TRACE_FILE_ACCESS")) qInfo() << "Playback finished:" << m_path;
        emit finished();
    }
}

void MpvPlayer::setNumber(const char* name, double value)
{
    if (m_handle && std::isfinite(value)) mpv_set_property_async(m_handle, m_generation * 8 + 7, name, MPV_FORMAT_DOUBLE, &value);
}
void MpvPlayer::togglePause()
{
    if (m_pathYielding) {
        m_pathYieldPlay = !m_pathYieldPlay;
        m_state.paused = !m_pathYieldPlay;
        if (m_state.ended && m_pathYieldPlay) { m_state.position = 0; m_state.ended = false; }
        emit stateChanged();
        return;
    }
    if (m_state.suspended) { restoreFile(true, m_state.ended ? 0 : m_state.position); return; }
    if (m_restoring) { m_restoreWantPlay = !m_restoreWantPlay; return; }
    if (!m_handle || !m_state.loaded) return;
    if (m_state.ended) {
        seek(0);
        int pause = 0;
        mpv_set_property_async(m_handle, m_generation * 8 + 7, "pause", MPV_FORMAT_FLAG, &pause);
    } else {
        const char* command[]{"cycle", "pause", nullptr};
        mpv_command_async(m_handle, m_generation * 8 + 7, command);
    }
}
void MpvPlayer::seek(double seconds, bool relative)
{
    if (m_pathYielding && m_state.seekable && std::isfinite(seconds)) {
        m_state.position = std::clamp(relative ? m_state.position + seconds : seconds, 0.0, m_state.duration);
        m_state.ended = false;
        emit stateChanged();
        return;
    }
    if (m_state.suspended && m_state.seekable && std::isfinite(seconds)) {
        restoreFile(false, std::clamp(relative ? m_state.position + seconds : seconds, 0.0, m_state.duration));
        return;
    }
    if (m_restoring && std::isfinite(seconds)) {
        m_restorePosition = std::clamp(relative ? m_restorePosition + seconds : seconds, 0.0, m_state.duration);
        return;
    }
    if (!m_handle || !m_state.loaded || !m_state.seekable || !std::isfinite(seconds)) return;
    m_idleReleaseTimer.stop();
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
    mpv_command_async(m_handle, m_generation * 8 + 7, command);
}
void MpvPlayer::flushRelativeSeek()
{
    const double delta = m_relativeSeek;
    m_relativeSeek = 0;
    if (!m_handle || !m_state.loaded || !m_state.seekable || delta == 0) return;
    const auto value = QByteArray::number(delta, 'f', 3);
    const char* command[]{"seek", value.constData(), "relative+exact", nullptr};
    mpv_command_async(m_handle, m_generation * 8 + 7, command);
    updateIdleRelease();
}
void MpvPlayer::setVolume(double volume) {
    if (!std::isfinite(volume)) return;
    m_state.volume = std::clamp(volume, 0.0, 100.0);
    setNumber("volume", m_state.volume);
    emit stateChanged();
}
void MpvPlayer::adjustVolume(double delta)
{
    if (m_state.suspended || m_restoring) { setVolume(m_state.volume + delta); return; }
    if (!m_handle || !m_state.loaded || !std::isfinite(delta)) return;
    const auto value = QByteArray::number(delta);
    const char* command[]{"add", "volume", value.constData(), nullptr};
    mpv_command_async(m_handle, m_generation * 8 + 7, command);
}
void MpvPlayer::setRate(double rate) {
    if (!std::isfinite(rate)) return;
    m_state.rate = std::clamp(rate, 0.5, 2.0);
    setNumber("speed", m_state.rate);
    emit stateChanged();
}
void MpvPlayer::toggleMute()
{
    if (m_handle) setMuted(!m_requestedMuted);
}
void MpvPlayer::setMuted(bool muted)
{
    // Keep the user's choice across stop/open, including a switch before the
    // asynchronous property notification for the old session arrives.
    m_requestedMuted = muted;
    m_state.muted = muted;
    emit stateChanged();
    if (!m_handle) return;
    int value = muted ? 1 : 0;
    mpv_set_property_async(m_handle, m_generation * 8 + 7, "mute", MPV_FORMAT_FLAG, &value);
}

void MpvPlayer::relocate(const QString& path)
{
    m_path = path;
    if (!m_pendingFile.isEmpty()) m_pendingFile = path;
    if (m_pathYielding && m_state.suspended) {
        const bool play = m_pathYieldPlay;
        m_pathYielding = false;
        restoreFile(play, m_state.position);
        return;
    }
    if (m_activeGuard && !m_yielding.contains(m_activeGuard)) {
        m_guardPaths[m_activeGuard] = path;
        m_state.accessProtected = m_protectionEnabled && m_activeGuard->arm(path);
        updateIdleRelease();
    }
}

void MpvPlayer::setExternalProtectionEnabled(bool enabled)
{
    m_protectionEnabled = enabled;
    if (m_activeGuard && !m_yielding.contains(m_activeGuard)) {
        m_activeGuard->acknowledge();
        m_state.accessProtected = enabled && m_activeGuard->arm(m_path);
    }
    updateIdleRelease();
    emit stateChanged();
}

void MpvPlayer::updateIdleRelease()
{
    if (m_mode != RenderMode::Probe && m_state.loaded && (m_state.paused || m_state.ended)
        && m_state.seekable && !m_state.accessProtected && !m_state.suspended && !m_restoring) {
        if (!m_idleReleaseTimer.isActive()) m_idleReleaseTimer.start();
    } else m_idleReleaseTimer.stop();
}

void MpvPlayer::suspendFile()
{
    if (!m_state.loaded || !m_state.seekable || m_restoring
        || (!m_pathYielding && (m_state.accessProtected || !(m_state.paused || m_state.ended)))) return;
    if (m_seekTimer.isActive()) {
        if (!m_pathYielding) { m_idleReleaseTimer.start(); return; }
        m_state.position = std::clamp(m_state.position + m_relativeSeek, 0.0, m_state.duration);
        m_relativeSeek = 0;
        m_seekTimer.stop();
    }
    emit freezeFrameRequested();
    m_suspending = true;
    m_suspendEntry = m_activeEntry;
    m_state.suspended = true;
    m_suspendTimer.start();
    ++m_generation;
    m_loadGeneration = 0;
    const char* command[]{"stop", nullptr};
    mpv_command_async(m_handle, m_generation * 8 + 5, command);
    emit stateChanged();
    QMetaObject::invokeMethod(this, &MpvPlayer::drainEvents, Qt::QueuedConnection);
}

bool MpvPlayer::validateRestoreSource()
{
    const auto identity = FileIdentity::read(m_path);
    if (QFileInfo(m_path).isFile() && (!m_fileIdentity.valid() || identity == m_fileIdentity)) return true;
    const auto path = m_path;
    stop();
    emit interrupted(path, tr("原文件已被删除、替换或无法访问，无法恢复播放。"));
    return false;
}

void MpvPlayer::restoreFile(bool play, double position)
{
    if (m_suspending) {
        m_restorePending = true;
        m_restoreWantPlay = play;
        m_restorePosition = position;
        return;
    }
    // The path monitor is debounced. A user can resume before it reports an
    // atomic replacement, so never apply the saved position to another file.
    if (!validateRestoreSource()) return;
    const auto saved = m_state;
    const auto path = m_path;
    const auto mode = m_mode;
    const auto identity = m_fileIdentity;
    m_restoreOpening = true;
    open(path, saved.rate, saved.volume, mode);
    m_restoreOpening = false;
    if (!m_handle) {
        stop();
        emit interrupted(path, tr("无法重新创建播放引擎，已关闭媒体，请重新打开。"));
        return;
    }
    // Keep the expected identity until precise seeking finishes. The file can
    // change while mpv loads; validate again before enabling playback.
    m_fileIdentity = identity;
    const bool protectedAccess = m_state.accessProtected;
    m_state = saved;
    m_state.accessProtected = protectedAccess;
    m_state.suspended = false;
    m_state.restoring = true;
    m_state.ended = false;
    m_restoring = true;
    m_restoreSeekSent = false;
    m_restoreWantPlay = play;
    m_restoreEnded = saved.ended && !play && position >= saved.duration - .01;
    m_restorePosition = position;
    m_restoreTimer.start();
    emit stateChanged();
}

void MpvPlayer::sendRestoreSeek()
{
    m_restoreSeekSent = true;
    m_restoreIssuedPosition = m_restorePosition;
    const auto position = QByteArray::number(m_restorePosition, 'f', 3);
    const char* seek[]{"seek", position.constData(), "absolute+exact", nullptr};
    mpv_command_async(m_handle, m_generation * 8 + 6, seek);
}

void MpvPlayer::guardBroken(ExternalAccessGuard* guard, ExternalAccessGuard::BreakReason reason)
{
    const auto path = m_guardPaths.value(guard);
    const bool current = (guard == m_activeGuard || guard == m_loadingGuard) && path == m_path;
    const bool pathRelease = reason == ExternalAccessGuard::BreakReason::PathChange
        && current && guard == m_activeGuard && m_state.loaded && m_state.seekable;
    if (reason == ExternalAccessGuard::BreakReason::PathChange && !pathRelease && current) {
        guard->acknowledge(); // Do not hold up rename; the path monitor follows it.
        QPointer<ExternalAccessGuard> alive(guard);
        QTimer::singleShot(250, this, [this, alive] {
            if (!alive || alive != m_activeGuard || m_yielding.contains(alive)
                || m_guardPaths.value(alive) != m_path) return;
            m_state.accessProtected = m_protectionEnabled && alive->arm(m_path);
            updateIdleRelease();
            emit stateChanged();
        });
        return;
    }
    m_yielding.insert(guard);
    QPointer<ExternalAccessGuard> alive(guard);
    QTimer::singleShot(1000, this, [this, alive] {
        if (alive && m_yielding.contains(alive)) destroyEngineAsync();
    });
    QTimer::singleShot(2000, this, [this, alive] {
        if (!alive || !m_yielding.contains(alive)) return;
        const auto path = m_guardPaths.value(alive);
        const bool report = (m_path.isEmpty() || m_path == path)
            && (!m_deferredOpen || m_deferredOpen->path == path);
        finishGuard(alive, false);
        if (report) emit statusMessage(tr("文件释放超时，无法保证外部操作成功，请稍后重试。"));
    });
    if (pathRelease) {
        // Windows gives the same handle-cache break for rename and atomic
        // replacement. Legacy replacement APIs can reject an open destination
        // even with delete sharing. Unload before acknowledging, then only
        // resume if the path monitor confirms the original file's new name.
        m_pathYielding = true;
        m_pathYieldPlay = m_restoring ? m_restoreWantPlay : !m_state.paused && !m_state.ended;
        if (m_restoring) {
            m_restoreTimer.stop();
            m_restoring = false;
            m_state.restoring = false;
            m_state.position = m_restorePosition;
            m_state.paused = !m_restoreWantPlay;
        }
        suspendFile();
        return;
    }
    if (current) {
        stop();
        emit interrupted(path, reason == ExternalAccessGuard::BreakReason::SharingConflict
            ? tr("已停止读取文件，以允许外部程序独占访问。") : tr("文件内容已被外部修改，已停止播放。"));
    }
}

void MpvPlayer::releaseGuard(qint64 entry)
{
    if (auto* guard = m_guards.take(entry)) finishGuard(guard);
}

void MpvPlayer::finishGuard(ExternalAccessGuard* guard, bool sourceReleased)
{
    const auto path = m_guardPaths.take(guard);
    m_yielding.remove(guard);
    if (guard == m_loadingGuard) m_loadingGuard = nullptr;
    if (guard == m_activeGuard) m_activeGuard = nullptr;
    for (auto it = m_guards.begin(); it != m_guards.end();) {
        if (it.value() == guard) it = m_guards.erase(it); else ++it;
    }
    guard->acknowledge(); // END_FILE guarantees that mpv closed the data source.
    guard->deleteLater();
    if (sourceReleased) emit fileReleased(path);
    openDeferredFile();
}

void MpvPlayer::openDeferredFile()
{
    if (m_yielding.isEmpty() && !m_destroying && m_deferredOpen) {
        const auto request = *m_deferredOpen;
        m_deferredOpen.reset();
        open(request.path, request.rate, request.volume, request.mode);
    }
}

void MpvPlayer::destroyEngineAsync()
{
    if (!m_handle || m_destroying) return;
    const bool currentYield = std::any_of(m_yielding.cbegin(), m_yielding.cend(), [this](auto* guard) {
        return m_guardPaths.value(guard) == m_path;
    });
    if (!currentYield && !m_state.suspended && !m_path.isEmpty() && !m_deferredOpen)
        m_deferredOpen = OpenRequest{m_path, m_state.rate, m_state.volume, m_mode};
    m_destroying = true;
    ++m_generation;
    emit rendererReleaseRequested();
    releaseRenderer();
    auto* handle = m_handle;
    m_handle = nullptr;
    mpv_set_wakeup_callback(handle, nullptr, nullptr);
    auto* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher] {
        m_destroying = false;
        m_suspending = false;
        m_suspendTimer.stop();
        const auto guards = m_guardPaths.keys();
        for (auto* guard : guards) finishGuard(guard);
        openDeferredFile();
        if (m_restorePending && m_state.suspended) {
            m_restorePending = false;
            restoreFile(m_restoreWantPlay, m_restorePosition);
        }
        watcher->deleteLater();
    });
    m_destroyFuture = QtConcurrent::run([handle] { mpv_terminate_destroy(handle); });
    watcher->setFuture(m_destroyFuture);
}
