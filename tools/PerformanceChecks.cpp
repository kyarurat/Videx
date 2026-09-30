#include "app/ThemeManager.h"
#include "services/SettingsService.h"
#include "ui/MainWindow.h"
#include "ui/explorer/ExplorerWidget.h"
#include "ui/player/PlayerWidget.h"
#include "media/MediaNavigation.h"
#include "media/MpvPlayer.h"
#include "media/ImageDecoder.h"
#include <QApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemModel>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTreeView>
#include <QDebug>
#include <QDir>
#include <functional>
#include <algorithm>
#include <cmath>
#include <QFileInfo>

namespace {
bool waitFor(const std::function<bool()>& ready, int timeout = 60000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < timeout) {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    return ready();
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
    return file.resize(44 + bytes); // Valid silence, including sparse zero ranges.
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const int count = qEnvironmentVariableIntValue("VIDEX_PERF_FILES") > 0
        ? qEnvironmentVariableIntValue("VIDEX_PERF_FILES") : 10000;
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const auto audio = directory.filePath("000000.wav");
    const auto next = directory.filePath(QString::number(count + 1).rightJustified(6, '0') + ".wav");
    if (!makeAudio(audio) || !makeAudio(next)) return 1;
    QElapsedTimer elapsed;
    elapsed.start();
    for (int i = 1; i <= count; ++i) {
        QFile file(directory.filePath(QString::number(i).rightJustified(6, '0') + ".txt"));
        if (!file.open(QIODevice::WriteOnly)) return 1;
    }
    qInfo() << "Fixture files:" << count + 2 << "create_ms:" << elapsed.elapsed();
    ThemeManager theme;
    QTemporaryDir configuration;
    if (!configuration.isValid()) return 1;
    SettingsService settings(configuration.filePath("settings.ini"));
    MainWindow window(&theme, &settings);
    window.show();
    auto* tree = window.findChild<QTreeView*>("fileTree");
    auto* widget = window.findChild<PlayerWidget*>();
    auto* player = widget->findChild<MpvPlayer*>();
    QTimer heartbeat;
    heartbeat.setInterval(5);
    QElapsedTimer sinceBeat;
    sinceBeat.start();
    qint64 maximumGap = 0;
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
        maximumGap = std::max(maximumGap, sinceBeat.restart());
    });
    heartbeat.start();
    elapsed.restart();
    if (!window.openPath(audio)) return 1;
    bool directoryReady = false;
    auto* initialModel = qobject_cast<QFileSystemModel*>(tree->model());
    QObject::connect(initialModel, &QFileSystemModel::directoryLoaded, &app, [&](const QString& path) {
        if (QDir::cleanPath(path) == QDir::cleanPath(directory.path()))
            QTimer::singleShot(100, &app, [&] { directoryReady = true; });
    });
    if (!waitFor([&] {
        auto* model = qobject_cast<QFileSystemModel*>(tree->model());
        return directoryReady && player->state().loaded && model && model->rowCount(tree->rootIndex()) >= count + 2;
    })) return 1;
    qInfo() << "Directory populate_and_settle_ms:" << elapsed.elapsed() << "maximum_gui_gap_ms:" << maximumGap;
    auto* model = tree->model();
    elapsed.restart();
    if (!window.openPath(next) || tree->model() != model || !waitFor([&] { return player->state().loaded; })) return 1;
    qInfo() << "Same-directory reopen_ms:" << elapsed.elapsed() << "model_reused:" << (tree->model() == model);
    if (!window.openPath(audio) || !waitFor([&] { return player->state().loaded; })) return 1;
    maximumGap = 0;
    sinceBeat.restart();
    elapsed.restart();
    widget->mediaNavigationRequested(1);
    if (!waitFor([&] { return widget->currentPath() == next && player->state().loaded; })) return 1;
    qInfo() << "Navigate across nonmedia_ms:" << elapsed.elapsed() << "maximum_gui_gap_ms:" << maximumGap;

    // A caller may supply a real multi-GB media file, including valid sparse
    // PCM silence. This does not measure cold disk caches or compressed GOPs.
    const auto largeMedia = qEnvironmentVariable("VIDEX_PERF_MEDIA");
    if (!largeMedia.isEmpty()) {
        elapsed.restart();
        if (!window.openPath(largeMedia) || !waitFor([&] { return player->state().loaded; })) return 1;
        qInfo() << "Large media bytes:" << QFileInfo(largeMedia).size() << "load_ms:" << elapsed.elapsed()
                << "duration_s:" << player->state().duration;
        player->togglePause();
        if (!waitFor([&] { return player->state().paused; })) return 1;
        elapsed.restart();
        const double target = player->state().duration * 0.8;
        player->seek(target);
        if (!waitFor([&] { return std::abs(player->state().position - target) < 0.5; })) return 1;
        qInfo() << "Large media 80-percent exact seek_ms:" << elapsed.elapsed();
    }

    const auto largeImage = qEnvironmentVariable("VIDEX_PERF_IMAGE");
    if (!largeImage.isEmpty()) {
        for (const auto size : {QSize(1280, 720), QSize{}}) {
            elapsed.restart();
            const auto result = ImageDecoder::decode(largeImage, std::make_shared<std::atomic_bool>(false), false, size);
            if (result.status != ImageResult::Status::Ready) return 1;
            qInfo() << (size.isEmpty() ? "Original" : "Preview") << "decode_ms:" << elapsed.elapsed()
                    << "pixel_bytes:" << result.image.sizeInBytes() << "source:" << result.sourceSize;
        }
    }
    return 0;
}
