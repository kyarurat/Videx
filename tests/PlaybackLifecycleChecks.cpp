#include "media/MpvPlayer.h"
#include "ui/player/VideoView.h"
#include "ui/player/PlayerWidget.h"
#include "ui/MainWindow.h"
#include "app/ThemeManager.h"
#include "services/SettingsService.h"
#include <QApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>
#include <QDebug>
#include <functional>

namespace {
bool waitFor(const std::function<bool()>& predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 10000) {
        QApplication::processEvents();
        QThread::msleep(10);
    }
    return predicate();
}

bool makeAudio(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    constexpr quint32 bytes = 48000 * 2 * 12;
    file.write("RIFF", 4); stream << quint32(36 + bytes);
    file.write("WAVEfmt ", 8); stream << quint32(16) << quint16(1) << quint16(1)
        << quint32(48000) << quint32(96000) << quint16(2) << quint16(16);
    file.write("data", 4); stream << bytes;
    return stream.status() == QDataStream::Ok && file.write(QByteArray(bytes, '\0')) == bytes;
}

bool checkWidgetDestruction(ThemeManager& theme, const QString& path, bool accelerate)
{
    auto* widget = new PlayerWidget(&theme);
    widget->resize(900, 600);
    widget->show();
    QPointer<MpvPlayer> player = widget->findChild<MpvPlayer*>();
    QPointer<QWidget> surface = widget->findChild<QWidget*>("videoSurface");
    if (!path.isEmpty()) {
        const QFileInfo file(path);
        widget->setFile({file.absoluteFilePath(), file.fileName(), file.suffix(), file.size(), file.lastModified()});
        if (!waitFor([&] { return player->state().loaded; })) {
            qCritical() << "FAIL: media must load before widget destruction";
            delete widget;
            return false;
        }
        if (accelerate) {
            widget->handleSeekKey(1, true, false);
            if (!waitFor([&] { return player->state().rate == 2.0; })) {
                qCritical() << "FAIL: held acceleration must start before widget destruction";
                delete widget;
                return false;
            }
        }
    }
    // Keep the real state connection intact. QWidget deletes its children
    // after the PlayerWidget subclass and its members have been destroyed.
    delete widget;
    QApplication::processEvents();
    if (player || surface) {
        qCritical() << "FAIL: widget destruction must release its player and surface";
        return false;
    }
    return true;
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    const auto audio = temporary.filePath("lifecycle.wav");
    if (!temporary.isValid() || !makeAudio(audio)) return 1;
    ThemeManager theme;
    if (!checkWidgetDestruction(theme, {}, false)
        || !checkWidgetDestruction(theme, audio, false)
        || !checkWidgetDestruction(theme, audio, true)) return 1;

    SettingsService settings(temporary.filePath("settings.ini"));
    auto* window = new MainWindow(&theme, &settings);
    window->show();
    auto* widget = window->findChild<PlayerWidget*>();
    QPointer<MpvPlayer> windowPlayer = widget->findChild<MpvPlayer*>();
    if (!window->openPath(audio) || !waitFor([&] { return windowPlayer->state().loaded; })) {
        delete window;
        return 1;
    }
    window->close();
    delete window;
    QApplication::processEvents();
    if (windowPlayer) return 1;

    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        qInfo() << "Widget lifecycle checks passed with Qt assertions enabled; native OpenGL checks skipped";
        return 0;
    }
    const auto video = qEnvironmentVariable("VIDEX_TEST_VIDEO");
    if (!video.isEmpty() && (!checkWidgetDestruction(theme, video, false)
        || !checkWidgetDestruction(theme, video, true))) return 1;
    for (int iteration = 0; iteration < 3; ++iteration) {
        MpvPlayer player;
        auto* view = new VideoView(&player);
        view->resize(640, 360);
        view->show();
        if (!waitFor([&] { return view->isValid(); })) {
            qCritical() << "FAIL: native OpenGL context is required for lifecycle checks";
            delete view;
            return 1;
        }
        if (!video.isEmpty()) {
            player.open(video, 1.0, 0.0);
            if (!waitFor([&] { return player.state().loaded && player.state().video; })) {
                qCritical() << "FAIL: active video for lifecycle checks";
                delete view;
                return 1;
            }
        }
        // This target enables Qt assertions even in Release. A surviving
        // context callback into the destroyed subclass aborts here.
        delete view;
        player.stop();
    }
    auto* player = new MpvPlayer;
    auto* view = new VideoView(player);
    view->resize(640, 360);
    view->show();
    if (!waitFor([&] { return view->isValid(); })) {
        delete view;
        delete player;
        return 1;
    }
    delete player;
    delete view;
    qInfo() << "Native lifecycle checks passed with Qt assertions enabled";
    return 0;
}
