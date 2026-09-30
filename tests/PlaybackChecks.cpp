#include "media/MpvPlayer.h"
#include "media/MediaType.h"
#include "media/PlaybackTime.h"
#include "media/MediaNavigation.h"
#include "app/ThemeManager.h"
#include "services/SettingsService.h"
#include "ui/MainWindow.h"
#include "ui/player/PlayerWidget.h"
#include "ui/player/PlayerControls.h"
#include "ui/common/UiComponents.h"
#include "ui/explorer/ExplorerWidget.h"
#include <QApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QLabel>
#include <QSlider>
#include <QTemporaryDir>
#include <QThread>
#include <QDebug>
#include <QScreen>
#include <QOpenGLWidget>
#include <QImage>
#include <QTreeView>
#include <QFileSystemModel>
#include <QAction>
#include <functional>
#include <cmath>
#include <limits>

namespace {
int failures = 0;
void check(bool condition, const char* description)
{
    if (!condition) { ++failures; qCritical() << "FAIL:" << description; }
}
bool waitFor(const std::function<bool()>& predicate, int timeout = 10000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeout) {
        QApplication::processEvents();
        QThread::msleep(10);
    }
    return predicate();
}
void makeAudio(const QString& path)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "create WAV fixture");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    constexpr quint32 samples = 48000 * 12;
    file.write("RIFF", 4); stream << quint32(36 + samples * 2);
    file.write("WAVEfmt ", 8); stream << quint32(16) << quint16(1) << quint16(1)
        << quint32(48000) << quint32(96000) << quint16(2) << quint16(16);
    file.write("data", 4); stream << quint32(samples * 2);
    for (quint32 i = 0; i < samples; ++i)
        stream << qint16(120 * std::sin(double(i) * 440 * 6.283185307179586 / 48000));
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary directory");
    const auto audio = temporary.filePath("1.WAV");
    const auto otherAudio = temporary.filePath("2.wav");
    makeAudio(audio);
    makeAudio(otherAudio);
    check(classifyMedia(audio) == MediaType::Audio, "case insensitive audio classification");
    check(classifyMedia("test.MP4") == MediaType::Video, "case insensitive video classification");
    check(classifyMedia("notes.txt") == MediaType::Unknown, "unknown file classification");
    for (const auto* extension : {"PNG", "JpEg", "SVG", "HEIC", "AVIF", "NEF", "DNG"})
        check(classifyMedia(QStringLiteral("image.") + extension) == MediaType::Image, "image classification includes Qt, HEIF and RAW candidates");
    check(classifyMedia("recording.Mp3") == MediaType::Audio, "mixed case audio classification");
    check(classifyMedia("no-extension") == MediaType::Unknown, "classification leaves content probing to opening");
    check(classifyMedia("folder.mp4/image.png") == MediaType::Image, "classification uses file suffix instead of directory suffix");
    check(formatPlaybackTime(0) == "00:00" && formatPlaybackTime(-1) == "00:00", "time formatting clamps negative positions");
    check(formatPlaybackTime(59.9) == "00:59" && formatPlaybackTime(60) == "01:00", "time formatting crosses minute boundary without rounding");
    check(formatPlaybackTime(3599) == "59:59" && formatPlaybackTime(3600) == "1:00:00", "time formatting crosses hour boundary");
    check(formatPlaybackTime(360005) == "100:00:05", "time formatting supports long recordings");
    check(formatPlaybackTime(std::numeric_limits<double>::quiet_NaN()) == "--:--"
        && formatPlaybackTime(std::numeric_limits<double>::infinity()) == "--:--", "invalid timeline values display an unknown time");
    ThemeManager theme;
    {
        MpvPlayer noCanvas;
        int rendererFailures = 0;
        QObject::connect(&noCanvas, &MpvPlayer::failed, &app, [&](const QString&) { ++rendererFailures; });
        noCanvas.open(audio, 1.0, 0.0, MpvPlayer::RenderMode::Audio);
        check(waitFor([&] { return noCanvas.state().loaded; }), "initialize reusable audio core without canvas");
        noCanvas.open(audio, 1.0, 0.0, MpvPlayer::RenderMode::OpenGL);
        check(waitFor([&] { return rendererFailures > 0; }, 7000), "reused core times out when canvas cannot initialize");
        check(rendererFailures == 1 && !noCanvas.state().loaded, "renderer timeout stops the failed session once");
        noCanvas.open(audio, 1.0, 0.0, MpvPlayer::RenderMode::Audio);
        check(waitFor([&] { return noCanvas.state().loaded; }), "audio recovers after renderer timeout");
        noCanvas.open(audio, 1.0, 0.0, MpvPlayer::RenderMode::OpenGL);
        noCanvas.open(audio, 1.0, 0.0, MpvPlayer::RenderMode::Audio);
        check(waitFor([&] { return noCanvas.state().loaded; }), "switch away from pending canvas request");
        QElapsedTimer elapsed;
        elapsed.start();
        waitFor([&] { return rendererFailures > 1 || elapsed.elapsed() >= 5500; }, 6500);
        check(rendererFailures == 1 && noCanvas.state().loaded,
              "obsolete renderer timeout cannot interrupt the replacement audio session");
    }
    {
        PlayerControls controls(&theme);
        PlaybackSnapshot snapshot;
        snapshot.loaded = true;
        snapshot.volume = 40;
        controls.updateState(snapshot);
        auto* play = controls.findChild<QToolButton*>("playButton");
        const auto playIconKey = play->icon().cacheKey();
        const auto icons = controls.findChildren<QToolButton*>();
        QList<qint64> iconKeys;
        for (auto* icon : icons) iconKeys.append(icon->icon().cacheKey());
        snapshot.position = 1;
        controls.updateState(snapshot);
        for (int i = 0; i < icons.size(); ++i)
            check(icons[i]->icon().cacheKey() == iconKeys[i], "timeline updates reuse control icons");
        snapshot.paused = !snapshot.paused;
        controls.updateState(snapshot);
        check(play->icon().cacheKey() != playIconKey, "pause changes the play icon");
        const auto beforeTheme = play->icon().cacheKey();
        theme.setMode(ThemeMode::Dark);
        check(play->icon().cacheKey() != beforeTheme, "theme changes still redraw playback icons");
        auto* volume = controls.findChild<QSlider*>("volumeSlider");
        volume->setSliderDown(true);
        volume->setValue(80);
        controls.updateState(snapshot);
        check(volume->value() == 80, "background state updates preserve volume drag position");
        volume->setSliderDown(false);
        snapshot.volume = 80;
        controls.updateState(snapshot);
        check(volume->value() == 80, "volume state resumes after drag");
    }
    SettingsService settings(temporary.filePath("settings.ini"));
    MainWindow window(&theme, &settings);
    window.show();
    check(window.openPath(audio), "open WAV through browser");
    auto* widget = window.findChild<PlayerWidget*>();
    auto* player = widget ? widget->findChild<MpvPlayer*>() : nullptr;
    check(player && waitFor([&] { return player->state().loaded; }), "audio-only loads");
    if (!player || !player->state().loaded) return 1;
    check(!player->state().video && player->state().duration > 11 && player->state().seekable, "audio has seekable timeline without video");
    auto* initialSurface = window.findChild<QOpenGLWidget*>("videoSurface");
    check(!initialSurface || !initialSurface->isValid(), "first audio playback does not initialize an OpenGL canvas");
    auto* pane = window.findChild<QWidget*>("playbackPane");
    auto* slider = window.findChild<QSlider*>("seekSlider");
    int completions = 0;
    QObject::connect(player, &MpvPlayer::finished, &app, [&] { ++completions; });
    check(slider && slider->isVisible() && slider->isEnabled(), "loaded audio exposes playback controls");
    const auto press = [pane](int key, bool repeat = false) {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, {}, repeat);
        QApplication::sendEvent(pane, &event);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, {}, repeat);
        QApplication::sendEvent(pane, &release);
    };
    press(Qt::Key_Space);
    check(waitFor([&] { return player->state().paused; }), "Space pauses audio");
    press(Qt::Key_Space, true);
    QApplication::processEvents();
    check(player->state().paused, "holding Space does not repeatedly toggle");
    player->seek(1);
    check(waitFor([&] { return std::abs(player->state().position - 1) < 0.5; }), "absolute seek");
    press(Qt::Key_Right);
    check(waitFor([&] { return player->state().position > 5; }), "Right seeks forward");
    press(Qt::Key_Left, true);
    check(waitFor([&] { return std::abs(player->state().position - 5) < 0.3; }), "held Left seeks back one second");
    player->setVolume(40);
    check(waitFor([&] { return std::abs(player->state().volume - 40) < 1; }), "volume update");
    press(Qt::Key_Up);
    check(waitFor([&] { return player->state().volume > 44; }), "Up raises volume");
    press(Qt::Key_M);
    check(waitFor([&] { return player->state().muted; }), "M mutes audio");
    player->setRate(1.5);
    check(waitFor([&] { return player->state().rate == 1.5; }), "playback speed");
    widget->applyPlaybackPreferences(1.0, false);
    check(player->state().rate == 1.5, "appearance-only settings preserve current playback rate");
    auto* tree = window.findChild<QTreeView*>("fileTree");
    auto* model = qobject_cast<QFileSystemModel*>(tree->model());
    check(waitFor([&] { return model->rowCount(tree->rootIndex()) >= 2; }), "audio directory is populated");
    tree->setCurrentIndex(model->index(audio));
    tree->setFocus();
    QKeyEvent treeDown(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(tree, &treeDown);
    check(waitFor([&] { return widget->currentPath() == otherAudio && player->state().loaded; }), "file tree arrow opens adjacent audio");
    check(QApplication::focusWidget() == tree, "media opening preserves file tree focus");
    check(waitFor([&] { return player->state().muted; }), "manual audio switch preserves mute");
    player->togglePause();
    check(waitFor([&] { return player->state().paused; }), "pause before slider keyboard check");
    player->seek(1);
    check(waitFor([&] { return std::abs(player->state().position - 1) < 0.2; }), "prepare tree shortcut check");
    const auto treeKey = [tree](QEvent::Type type, int key, bool repeat = false, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QKeyEvent event(type, key, modifiers, {}, repeat);
        QApplication::sendEvent(tree, &event);
    };
    treeKey(QEvent::KeyPress, Qt::Key_Right);
    treeKey(QEvent::KeyRelease, Qt::Key_Right);
    check(waitFor([&] { return std::abs(player->state().position - 6) < 0.2; }), "tree short Right seeks five seconds");
    treeKey(QEvent::KeyPress, Qt::Key_Left, true);
    treeKey(QEvent::KeyRelease, Qt::Key_Left, true);
    check(waitFor([&] { return std::abs(player->state().position - 5) < 0.2; }), "tree repeated Left seeks one second");
    treeKey(QEvent::KeyPress, Qt::Key_Left);
    treeKey(QEvent::KeyRelease, Qt::Key_Left);
    check(waitFor([&] { return player->state().position < 0.3; }), "tree short Left seeks five seconds");
    player->seek(9);
    check(waitFor([&] { return std::abs(player->state().position - 9) < 0.2; }), "prepare complete Left hold sequence");
    treeKey(QEvent::KeyPress, Qt::Key_Left);
    check(waitFor([&] { return std::abs(player->state().position - 8) < 0.2; }, 1000), "initial held Left step moves one second");
    treeKey(QEvent::KeyRelease, Qt::Key_Left, true);
    treeKey(QEvent::KeyPress, Qt::Key_Left, true);
    check(waitFor([&] { return std::abs(player->state().position - 7) < 0.2; }), "subsequent held Left step moves one second");
    treeKey(QEvent::KeyRelease, Qt::Key_Left);
    check(player->state().position > 6.8, "Left hold release adds no short-press seek");
    player->seek(0);
    check(waitFor([&] { return player->state().position < 0.3; }), "reset after Left hold sequence");
    player->setRate(1.5);
    check(waitFor([&] { return player->state().rate == 1.5; }), "prepare non-default hold rate");
    treeKey(QEvent::KeyPress, Qt::Key_Right);
    check(waitFor([&] { return player->state().rate == 2.0; }), "held tree Right temporarily sets 2x");
    treeKey(QEvent::KeyRelease, Qt::Key_Right, true);
    treeKey(QEvent::KeyPress, Qt::Key_Right, true);
    check(player->state().rate == 2.0, "auto-repeat release does not end hold");
    treeKey(QEvent::KeyRelease, Qt::Key_Right);
    check(waitFor([&] { return player->state().rate == 1.5; }), "release restores original 1.5x");
    check(player->state().position < 0.3, "held Right does not add a five-second seek");
    treeKey(QEvent::KeyPress, Qt::Key_Right);
    check(waitFor([&] { return player->state().rate == 2.0; }), "second hold enters 2x");
    QFocusEvent focusOut(QEvent::FocusOut);
    QApplication::sendEvent(tree, &focusOut);
    check(waitFor([&] { return player->state().rate == 1.5; }), "focus loss restores playback rate");
    auto* explorer = window.findChild<ExplorerWidget*>();
    treeKey(QEvent::KeyPress, Qt::Key_Right);
    check(waitFor([&] { return player->state().rate == 2.0; }), "prepare hold before disabling tree seeking");
    explorer->setPlaybackSeekingEnabled(false);
    check(waitFor([&] { return player->state().rate == 1.5; }), "disabling tree seeking cancels an active speed hold");
    int disabledReleases = 0;
    const auto releaseConnection = QObject::connect(explorer, &ExplorerWidget::playbackSeekRequested, &window,
        [&](int, bool pressed, bool) { if (!pressed) ++disabledReleases; });
    treeKey(QEvent::KeyRelease, Qt::Key_Right);
    check(disabledReleases == 0 && player->state().position < 0.3, "release after disabling does not forward a stale seek request");
    QObject::disconnect(releaseConnection);
    explorer->setPlaybackSeekingEnabled(true);
    player->seek(2.1);
    check(waitFor([&] { return std::abs(player->state().position - 2.1) < 0.2; }), "prepare keyboard slider seek");
    QKeyEvent sliderRight(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(slider, &sliderRight);
    check(waitFor([&] { return player->state().position > 2.7; }), "focused seek slider keyboard changes timeline");
    check(completions == 0, "seeking does not emit completion");
    player->seek(9);
    check(waitFor([&] { return std::abs(player->state().position - 9) < 0.2; }), "prepare cumulative seek burst");
    for (int i = 0; i < 6; ++i) player->seek(-1, true);
    check(waitFor([&] { return std::abs(player->state().position - 3) < 0.2; }),
          "coalesced seek burst preserves all six requested seconds");
    player->seek(-1, true);
    player->seek(5);
    check(waitFor([&] { return std::abs(player->state().position - 5) < 0.2; }), "absolute seek cancels pending relative delta");
    check(window.openPath(otherAudio), "switch audio session");
    check(waitFor([&] { return player->state().loaded && player->state().title.contains("2"); }), "replacement session loads");
    check(completions == 0, "manual switching does not complete old media");
    player->seek(11.8);
    check(waitFor([&] { return player->state().ended; }), "natural end keeps loaded session");
    check(completions == 1, "natural end emits once");
    player->togglePause();
    check(waitFor([&] { return !player->state().ended && player->state().position < 2 && !player->state().paused; }), "play at end restarts media");
    check(window.openPath(audio), "open first audio for auto-next");
    check(waitFor([&] { return player->state().loaded; }), "first auto-next session loads");
    widget->applyPlaybackPreferences(1.0, true);
    player->seek(11.8);
    check(waitFor([&] { return widget->currentPath() == otherAudio && player->state().loaded; }), "natural end advances to same-category next file");
    check(waitFor([&] { return player->state().muted; }), "auto-next preserves mute");
    player->seek(11.8);
    check(waitFor([&] { return player->state().ended; }), "sequence end stops without wrapping");
    check(widget->currentPath() == otherAudio, "end retains last file");
    widget->applyPlaybackPreferences(1.0, false);
    QImage fixture(32, 32, QImage::Format_RGB32);
    fixture.fill(Qt::green);
    const auto imagePath = temporary.filePath("image.png");
    check(fixture.save(imagePath), "create image fixture");
    check(window.openPath(imagePath), "switch audio to image");
    check(!player->state().loaded && !slider->isVisible(), "image switching releases playback and hides controls");
    for (int i = 0; i < 3; ++i) {
        window.openPath(audio);
        window.openPath(otherAudio);
        window.openPath(imagePath);
    }
    QApplication::processEvents();
    check(!player->state().loaded && !slider->isVisible(), "rapid switches cannot resurrect stale playback");
    const auto coverAudio = qEnvironmentVariable("VIDEX_TEST_COVER_AUDIO");
    if (!coverAudio.isEmpty()) {
        check(window.openPath(coverAudio), "open cover-art audio");
        check(waitFor([&] { return player->state().loaded; }), "cover-art audio loads");
        check(!player->state().video, "attached cover is not video");
        check(player->state().artist == QStringLiteral("Videx Artist") && player->state().album == QStringLiteral("Videx Album"), "audio metadata is read");
        player->togglePause();
        check(waitFor([&] { return player->state().paused; }), "pause for audio theme screenshots");
        theme.setMode(ThemeMode::Dark);
        QApplication::processEvents();
        window.grab().save("artifacts/playback-audio-dark.png");
        theme.setMode(ThemeMode::Light);
        QApplication::processEvents();
        window.grab().save("artifacts/playback-audio-light.png");
    }
    const auto audioContainer = qEnvironmentVariable("VIDEX_TEST_AUDIO_CONTAINER");
    if (!audioContainer.isEmpty()) {
        check(window.openPath(audioContainer), "open audio-only MP4 container");
        check(waitFor([&] { return player->state().loaded; }), "audio-only MP4 loads");
        check(!player->state().video && widget->playbackCategory() == MediaType::Audio, "actual streams determine MP4 category");
    }
    const auto video = qEnvironmentVariable("VIDEX_TEST_VIDEO");
    if (!video.isEmpty()) {
        {
            MpvPlayer noCanvas;
            int rendererFailures = 0;
            QObject::connect(&noCanvas, &MpvPlayer::failed, &app, [&](const QString&) { ++rendererFailures; });
            noCanvas.open(video, 1.0, 0.0, MpvPlayer::RenderMode::Audio);
            check(waitFor([&] { return noCanvas.state().loaded && noCanvas.state().video; }),
                  "audio entry discovers video before creating a canvas");
            check(waitFor([&] { return rendererFailures > 0; }, 7000),
                  "audio-to-video upgrade times out without a canvas");
            check(rendererFailures == 1 && !noCanvas.state().loaded,
                  "upgrade timeout stops video instead of continuing invisible playback");
        }
        const auto disguisedVideo = temporary.filePath("video-in-audio.ogg");
        check(QFile::copy(video, disguisedVideo), "create video with an audio extension");
        {
            SettingsService independent(temporary.filePath("independent.ini"));
            MainWindow firstVideo(&theme, &independent);
            firstVideo.show();
            auto* backend = firstVideo.findChild<PlayerWidget*>()->findChild<MpvPlayer*>();
            check(firstVideo.openPath(disguisedVideo), "open actual video through audio entry");
            check(waitFor([&] { return backend->state().loaded && backend->state().video && backend->state().position > 0.5; }),
                  "actual video upgrades audio output without reopening the file");
            if (QApplication::platformName() != QStringLiteral("offscreen")) {
                auto* canvas = firstVideo.findChild<QOpenGLWidget*>("videoSurface");
                check(canvas && canvas->isValid() && !canvas->grabFramebuffer().isNull(),
                      "audio entry initializes video canvas only after finding a video stream");
            }
            firstVideo.close();
        }
        check(window.openPath(video), "open supplied video");
        check(waitFor([&] { return player->state().loaded && player->state().video; }), "actual video stream loads");
        check(waitFor([&] { return player->state().position > 2; }), "video timeline advances");
        theme.setMode(ThemeMode::Dark);
        QApplication::processEvents();
        waitFor([&] { return player->state().position > 2.5; });
        auto* view = window.findChild<QOpenGLWidget*>("videoSurface");
        const QImage frame = view ? view->grabFramebuffer() : QImage{};
        int colorful = 0;
        for (int y = 0; y < frame.height(); y += 8)
            for (int x = 0; x < frame.width(); x += 8) {
                const auto color = frame.pixelColor(x, y);
                if (qMax(color.red(), qMax(color.green(), color.blue())) - qMin(color.red(), qMin(color.green(), color.blue())) > 80) ++colorful;
            }
        check(colorful > 100, "video framebuffer contains rendered test colors");
        if (view) view->grabFramebuffer().save("artifacts/playback-frame.png");
        window.grab().save("artifacts/playback-dark.png");
        for (auto* action : window.findChildren<QAction*>()) {
            if (action->shortcut() == QKeySequence(QStringLiteral("F"))) {
                action->trigger();
                check(waitFor([&] { return window.isFullScreen(); }), "video fullscreen enters");
                check(view && !view->grabFramebuffer().isNull(), "fullscreen keeps video framebuffer");
                action->trigger();
                check(waitFor([&] { return !window.isFullScreen(); }), "video fullscreen exits");
                break;
            }
        }
        theme.setMode(ThemeMode::Light);
        QApplication::processEvents();
        waitFor([&] { return player->state().position > 3; });
        window.grab().save("artifacts/playback-light.png");
        player->togglePause();
        check(waitFor([&] { return player->state().paused; }), "pause video for exact keyboard seeks");
        player->seek(6.2);
        check(waitFor([&] { return std::abs(player->state().position - 6.2) < 0.2; }), "video exact absolute seek");
        press(Qt::Key_Left, true);
        check(waitFor([&] { return std::abs(player->state().position - 5.2) < 0.2; }), "video held Left moves one second instead of a keyframe interval");
        press(Qt::Key_Right);
        check(waitFor([&] { return std::abs(player->state().position - 10.2) < 0.2; }), "video short Right moves five seconds");
        QKeyEvent holdPress(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QApplication::sendEvent(pane, &holdPress);
        check(waitFor([&] { return player->state().rate == 2.0; }), "video viewer hold enables 2x");
        QKeyEvent holdRelease(QEvent::KeyRelease, Qt::Key_Right, Qt::NoModifier);
        QApplication::sendEvent(pane, &holdRelease);
        check(waitFor([&] { return player->state().rate == 1.0; }), "video viewer release restores 1x");
        QApplication::sendEvent(pane, &holdPress);
        check(waitFor([&] { return player->state().rate == 2.0; }), "prepare hold during media switch");
        check(window.openPath(imagePath), "switch away during held acceleration");
        check(!player->state().loaded, "switch cancels held acceleration session");
    }
    const auto corrupt = temporary.filePath("broken.mp4");
    QFile broken(corrupt);
    check(broken.open(QIODevice::WriteOnly), "create corrupt fixture");
    broken.write("invalid media"); broken.close();
    check(window.openPath(corrupt), "open corrupt media");
    auto* error = window.findChild<QLabel*>("unsupportedFormatMessage");
    check(waitFor([&] { return error->isVisible(); }), "corrupt media shows error");
    check(!player->state().loaded && !slider->isVisible(), "failure resets loaded state and controls");
    check(window.openPath(audio), "return to audio after error");
    check(waitFor([&] { return player->state().loaded; }), "audio recovers after error");
    check(window.openDirectory(temporary.path()), "clear current playback");
    check(!player->state().loaded && !slider->isVisible(), "directory switch stops playback");
    const auto containerDirectory = temporary.filePath("containers");
    check(QDir().mkpath(containerDirectory), "create ambiguous-container directory");
    const auto firstContainer = QDir(containerDirectory).filePath("1.mp4");
    const auto secondContainer = QDir(containerDirectory).filePath("2.mp4");
    // Extension hints must not override the backend's stream classification.
    if (audioContainer.isEmpty()) {
        makeAudio(firstContainer);
        makeAudio(secondContainer);
    } else {
        check(QFile::copy(audioContainer, firstContainer) && QFile::copy(audioContainer, secondContainer),
            "create real audio-only MP4 navigation fixtures");
    }
    check(window.openPath(firstContainer), "open audio in video-extension container");
    check(waitFor([&] { return player->state().loaded && widget->playbackCategory() == MediaType::Audio; }), "container resolves to audio");
    model = qobject_cast<QFileSystemModel*>(tree->model());
    check(waitFor([&] { return model->rowCount(tree->rootIndex()) == 2; }), "container directory is populated");
    widget->mediaNavigationRequested(1);
    check(waitFor([&] { return widget->currentPath() == secondContainer && player->state().loaded; }), "manual navigation reaches next actual audio container");
    check(waitFor([&] { return player->state().muted; }), "container navigation preserves mute");
    check(window.openPath(firstContainer), "restart container sequence");
    check(waitFor([&] { return player->state().loaded; }), "container auto-next starts");
    model = qobject_cast<QFileSystemModel*>(tree->model());
    check(waitFor([&] { return model->rowCount(tree->rootIndex()) == 2; }), "container sequence listing is ready");
    widget->applyPlaybackPreferences(1.0, true);
    player->seek(player->state().duration - 0.2);
    check(waitFor([&] { return widget->currentPath() == secondContainer && player->state().loaded; }), "auto-next reaches next actual audio container");
    check(waitFor([&] { return player->state().muted; }), "container auto-next stays muted");
    widget->applyPlaybackPreferences(1.0, false);
    player->setMuted(false);
    check(window.openPath(firstContainer), "switch immediately after unmute request");
    check(waitFor([&] { return player->state().loaded && !player->state().muted; }), "unmute survives switch before property reply");
    player->setMuted(true);
    check(window.openPath(secondContainer), "switch immediately after mute request");
    check(waitFor([&] { return player->state().loaded && player->state().muted; }), "mute survives switch before property reply");
    {
        MpvPlayer probe;
        probe.open(firstContainer, 1.0, 0.0, MpvPlayer::RenderMode::Probe);
        check(waitFor([&] { return probe.state().loaded && probe.state().paused && probe.state().muted; }), "navigation probe is paused and muted");
        check(probe.state().volume == 0 && probe.state().position < 0.1, "probe never advances playback");
        MediaNavigation cancelled;
        bool selected = false;
        QObject::connect(&cancelled, &MediaNavigation::candidateSelected, &app, [&] { selected = true; });
        cancelled.navigate({firstContainer, secondContainer}, MediaType::Audio);
        QTimer::singleShot(0, &cancelled, &MediaNavigation::cancel);
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 150) { QApplication::processEvents(); QThread::msleep(5); }
        check(!selected, "cancelled in-flight navigation cannot select a stale file");
    }
    {
        MpvPlayer switching;
        int errors = 0;
        QObject::connect(&switching, &MpvPlayer::failed, &app, [&] { ++errors; });
        for (int i = 0; i < 30; ++i)
            switching.open(i % 2 ? audio : corrupt, 1, 20, MpvPlayer::RenderMode::Headless);
        switching.open(otherAudio, 1, 20, MpvPlayer::RenderMode::Headless);
        check(waitFor([&] { return switching.state().loaded && switching.state().duration > 11; }),
              "latest media loads after a burst of thirty pending replacements");
        check(errors == 0 && !switching.state().video, "obsolete load failures do not affect the latest session");
    }
    {
        MediaNavigation cached;
        cached.remember(firstContainer, MediaType::Audio);
        cached.remember(secondContainer, MediaType::Audio);
        QString selected;
        QObject::connect(&cached, &MediaNavigation::candidateSelected, &app, [&](const QString& path) { selected = path; });
        cached.navigate({firstContainer, secondContainer}, MediaType::Audio);
        check(waitFor([&] { return selected == firstContainer; }), "navigation reuses verified media categories");
        check(!cached.findChild<MpvPlayer*>()->state().loaded, "cached selection requires no new probe load");
        cached.navigate({firstContainer, secondContainer}, MediaType::Video);
        selected.clear();
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 100) { app.processEvents(); QThread::msleep(5); }
        check(selected.isEmpty() && !cached.findChild<MpvPlayer*>()->state().loaded,
              "cached mismatching categories skip without opening media");
    }
    if (!video.isEmpty()) {
        const auto mixedDirectory = temporary.filePath("mixed");
        check(QDir().mkpath(mixedDirectory), "create mixed container directory");
        const auto firstVideo = QDir(mixedDirectory).filePath("1.mp4");
        const auto middleAudio = QDir(mixedDirectory).filePath("2.mp4");
        const auto nextVideo = QDir(mixedDirectory).filePath("3.mp4");
        check(QFile::copy(video, firstVideo) && QFile::copy(video, nextVideo), "create real video navigation fixtures");
        if (audioContainer.isEmpty()) makeAudio(middleAudio);
        else check(QFile::copy(audioContainer, middleAudio), "create real audio-only MP4 between videos");
        check(window.openPath(firstVideo), "open video in mixed sequence");
        check(waitFor([&] { return player->state().loaded && player->state().video; }), "mixed sequence video loads");
        model = qobject_cast<QFileSystemModel*>(tree->model());
        check(waitFor([&] { return model->rowCount(tree->rootIndex()) == 3; }), "mixed sequence listing is ready");
        widget->applyPlaybackPreferences(1.0, true);
        player->seek(player->state().duration - 0.2);
        check(waitFor([&] { return widget->currentPath() == nextVideo && player->state().loaded && player->state().video; }), "video auto-next skips actual audio in MP4");
        check(waitFor([&] { return player->state().muted; }), "video auto-next preserves mute");
        widget->applyPlaybackPreferences(1.0, false);
        widget->mediaNavigationRequested(-1);
        check(waitFor([&] { return widget->currentPath() == firstVideo && player->state().loaded && player->state().video; }), "previous video skips actual audio in MP4");
    }
    check(window.openDirectory(temporary.path()), "stop all navigation test playback");
    qInfo() << "Playback failures:" << failures;
    return failures ? 1 : 0;
}
