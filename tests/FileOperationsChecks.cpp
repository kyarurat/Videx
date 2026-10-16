#include "app/BrowserController.h"
#include "app/ThemeManager.h"
#include "media/ImageDecoder.h"
#include "media/ImageReadSnapshot.h"
#include "media/ImageLoader.h"
#include "media/MpvPlayer.h"
#include "services/FileIdentity.h"
#include "services/SettingsService.h"
#include "services/SelectedFileMonitor.h"
#include "ui/MainWindow.h"
#include "ui/images/ImageView.h"
#include "ui/player/PlayerWidget.h"
#include <QApplication>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QOpenGLWidget>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <functional>
#include <algorithm>
#include <cstdio>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
int failures = 0;
#ifdef Q_OS_WIN
constexpr auto accessOperation = "exclusive";
#else
constexpr auto accessOperation = "reader";
#endif
void check(bool condition, const char* description)
{
    if (!condition) { ++failures; qCritical() << "FAIL:" << description; }
}
bool hasVideoColors(const QImage& frame)
{
    int colored = 0;
    for (int y = 0; y < frame.height(); y += 16) for (int x = 0; x < frame.width(); x += 16) {
        const auto color = frame.pixelColor(x, y);
        if (std::max({color.red(), color.green(), color.blue()}) - std::min({color.red(), color.green(), color.blue()}) > 40) ++colored;
    }
    return colored > 100;
}
bool waitFor(const std::function<bool()>& predicate, int timeout = 10000)
{
    QElapsedTimer time;
    time.start();
    while (!predicate() && time.elapsed() < timeout) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
    return predicate();
}
void settle(int milliseconds = 350)
{
    QElapsedTimer time;
    time.start();
    waitFor([&] { return time.elapsed() >= milliseconds; }, milliseconds + 1000);
}
void makeAudio(const QString& path)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "create WAV");
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    constexpr quint32 bytes = 48000 * 2 * 60;
    file.write("RIFF", 4); out << quint32(36 + bytes);
    file.write("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1)
        << quint32(48000) << quint32(96000) << quint16(2) << quint16(16);
    file.write("data", 4); out << bytes;
    check(file.write(QByteArray(bytes, '\0')) == bytes, "write WAV");
}
int externalOperation(const QStringList& args)
{
    if (args.size() < 4) return 2;
    const auto operation = args[2];
    const auto path = args[3];
    if (args.contains("--ready")) {
        QFile output;
        if (!output.open(stdout, QIODevice::WriteOnly) || output.write("ready\n") != 6 || !output.flush()) return 10;
    }
    if (operation == "rename") return args.size() > 4 && QFile::rename(path, args[4]) ? 0 : 3;
    if (operation == "delete") return QFile::remove(path) ? 0 : 4;
    if (operation == "replace") {
        QFile original(path);
        if (!original.open(QIODevice::ReadOnly)) return 5;
        auto bytes = original.readAll();
        original.close();
        QSaveFile replacement(path);
        if (!replacement.open(QIODevice::WriteOnly) || replacement.write(bytes) != bytes.size()) return 6;
        if (replacement.commit()) return 0;
        qWarning() << "Atomic replace failed:" << replacement.errorString();
        return 7;
    }
#ifdef Q_OS_WIN
    if (operation == "exclusive" || operation == "reader") {
        const auto name = QDir::toNativeSeparators(path);
        HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(name.utf16()),
            operation == "reader" ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE,
            operation == "reader" ? FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE : 0,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) return int(GetLastError()) + 20;
        QThread::msleep(150);
        CloseHandle(file);
        return 0;
    }
#endif
    QFile file(path);
    if (operation == "reader") return file.open(QIODevice::ReadOnly) ? 0 : 8;
    if (operation != "write") return 2;
    if (!file.open(QIODevice::ReadWrite)) return 8;
    if (operation == "write") {
        file.seek(file.size() - 1);
        return file.write("x", 1) == 1 && file.flush() ? 0 : 9;
    }
    return 0;
}
bool external(const QString& operation, const QString& path, const QString& destination = {})
{
    QProcess process;
    QStringList arguments{"--external-operation", operation, path};
    if (!destination.isEmpty()) arguments.append(destination);
    process.start(QCoreApplication::applicationFilePath(), arguments);
    if (!process.waitForStarted(3000)) return false;
    if (!waitFor([&] { return process.state() == QProcess::NotRunning; }, 4000)) {
        process.kill(); process.waitForFinished(); return false;
    }
    if (process.exitCode()) qWarning() << operation << "external exit:" << process.exitCode() << process.readAllStandardError();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
}

int main(int argc, char** argv)
{
    if (argc > 1 && QByteArray(argv[1]) == "--external-operation") {
        QCoreApplication app(argc, argv);
        return externalOperation(app.arguments());
    }
    QApplication app(argc, argv);
    // Keep subprocess fixtures on the same, explicitly writable local volume.
    QTemporaryDir directory(QDir::currentPath() + "/file-operations-XXXXXX");
    check(directory.isValid(), "temporary operation directory");
    if (!directory.isValid()) return 1;
    {
        auto path = directory.filePath("monitor-initial.wav");
        makeAudio(path);
        const QFileInfo info(path);
        SelectedFileMonitor monitor;
        QString observed;
        int changes = 0;
        int removals = 0;
        QObject::connect(&monitor, &SelectedFileMonitor::relocated, &app,
            [&](const FileDetails& file) { observed = file.path; });
        QObject::connect(&monitor, &SelectedFileMonitor::changed, &app,
            [&](const FileDetails&) { ++changes; });
        QObject::connect(&monitor, &SelectedFileMonitor::unavailable, &app, [&] { ++removals; });
        monitor.watch({path, info.fileName(), info.suffix(), info.size(), info.lastModified()});
        for (int i = 0; i < 8; ++i) {
            const auto next = directory.filePath(QString("monitor-%1.wav").arg(i));
            check(external("rename", path, next), "rapid consecutive rename succeeds");
            path = next;
        }
        check(waitFor([&] { return observed == path; }), "monitor follows final consecutive rename");
        check(removals == 0, "consecutive renames do not report deletion");
        check(external("replace", path), "replace file after consecutive renames");
        check(waitFor([&] { return changes > 0; }), "monitor retains content notifications after rename");
        check(external("delete", path), "delete file after replacement");
        check(waitFor([&] { return removals == 1; }), "monitor rearms replacement and sees deletion");
        const auto missing = directory.filePath("removed-before-watch.wav");
        makeAudio(missing);
        const QFileInfo selected(missing);
        const FileDetails stale{missing, selected.fileName(), selected.suffix(), selected.size(), selected.lastModified()};
        check(QFile::remove(missing), "remove file between selection and watcher installation");
        monitor.watch(stale);
        check(waitFor([&] { return removals == 2; }), "initial inspection detects removal before watcher installation");
    }
    SettingsService settings(directory.filePath("settings.ini"));
    ThemeManager theme;
    MainWindow window(&theme, &settings);
    window.show();
    auto* widget = window.findChild<PlayerWidget*>();
    auto* player = widget->findChild<MpvPlayer*>();
    int completions = 0;
    QObject::connect(player, &MpvPlayer::finished, &app, [&] { ++completions; });
    for (const bool paused : {false, true}) {
        const auto path = directory.filePath(paused ? "paused.wav" : "playing.wav");
        makeAudio(path);
        check(window.openPath(path) && waitFor([&] { return player->state().loaded; }), "open audio for external operation");
        player->setVolume(0);
        player->seek(10);
        check(waitFor([&] { return player->state().position >= 9.8; }), "seek before rename");
        if (paused) { player->togglePause(); check(waitFor([&] { return player->state().paused; }), "pause before rename"); }
        const auto originalIdentity = FileIdentity::read(path);
        const auto renamed = directory.filePath(paused ? "renamed-paused.wav" : "renamed-playing.wav");
        check(external("reader", path), "shared external reader succeeds");
        check(player->state().loaded, "shared reader does not stop playback");
        check(external("rename", path, renamed), "external same-directory rename succeeds");
        if (player->state().suspended) player->seek(player->state().position);
        check(waitFor([&] { return widget->currentPath() == renamed && !player->state().restoring; }), "rename follows current file");
        check(player->state().loaded && player->state().paused == paused && player->state().position >= 9.8,
            "rename preserves progress and pause state");
        check(FileIdentity::read(renamed) == originalIdentity, "rename retains file identity");
        const auto caseName = directory.filePath(paused ? "Renamed-Paused.wav" : "Renamed-Playing.wav");
        check(external("rename", renamed, caseName), "case-only rename succeeds");
        check(waitFor([&] { return widget->currentPath() == caseName && !player->state().restoring; }), "case-only rename follows actual name");
        check(external("delete", caseName), "external deletion succeeds");
        check(waitFor([&] { return widget->currentPath().isEmpty() && !player->state().loaded; }), "deletion returns to unopened page");
    }
    for (const auto* operation : {"write", "replace", "move"}) {
        const auto path = directory.filePath(QString::fromLatin1(operation) + ".wav");
        makeAudio(path);
        check(window.openPath(path) && waitFor([&] { return player->state().loaded; }), "load audio before content/move operation");
        player->setVolume(0);
        if (QByteArray(operation) == "move") {
            QDir().mkdir(directory.filePath("moved"));
            check(external("rename", path, directory.filePath("moved/file.wav")), "external move succeeds");
        } else check(external(QString::fromLatin1(operation), path), "external content change succeeds");
        check(waitFor([&] { return widget->currentPath().isEmpty(); }), "content change or move closes current media");
    }
    {
        const auto path = directory.filePath("hard-link.wav");
        const auto alias = directory.filePath("hard-link-alias.wav");
        makeAudio(path);
#ifdef Q_OS_WIN
        const bool linked = CreateHardLinkW(reinterpret_cast<LPCWSTR>(alias.utf16()),
            reinterpret_cast<LPCWSTR>(path.utf16()), nullptr);
#else
        const bool linked = ::link(QFile::encodeName(path).constData(), QFile::encodeName(alias).constData()) == 0;
#endif
        if (linked) {
            check(window.openPath(path) && waitFor([&] { return player->state().loaded; }), "open hard-linked media");
            check(external("delete", path), "delete one hard-link name");
            check(waitFor([&] { return widget->currentPath().isEmpty(); }), "deletion does not incorrectly follow a pre-existing hard link");
            check(QFileInfo::exists(alias), "other hard link remains untouched");
        } else qInfo() << "SKIP: filesystem does not support hard links";
    }
#ifdef Q_OS_WIN
    {
        const auto path = directory.filePath("delete-at-eof.wav");
        makeAudio(path);
        check(window.openPath(path) && waitFor([&] { return player->state().loaded; }), "load buffered EOF fixture");
        player->setVolume(0);
        player->seek(59.6);
        check(waitFor([&] { return player->state().position >= 59.5; }), "position buffered EOF fixture");
        QProcess remove;
        remove.start(QCoreApplication::applicationFilePath(), {"--external-operation", "delete", path, "--ready"});
        check(remove.waitForStarted(3000), "external EOF removal starts");
        if (!remove.bytesAvailable() && remove.state() != QProcess::NotRunning) remove.waitForReadyRead(3000);
        check(remove.readAllStandardOutput().contains("ready"), "EOF removal reaches OS request");
        QThread::msleep(500); // Queue both buffered EOF and the file-access notification.
        check(waitFor([&] { return remove.state() == QProcess::NotRunning && widget->currentPath().isEmpty(); }), "external removal wins over buffered EOF");
        check(remove.exitCode() == 0 && completions == 0, "removal at EOF does not trigger auto-next");
    }
    for (const bool paused : {false, true}) {
        const auto path = directory.filePath(paused ? "exclusive-paused.wav" : "exclusive-playing.wav");
        makeAudio(path);
        check(window.openPath(path) && waitFor([&] { return player->state().loaded; }), "load protected audio");
        player->setVolume(0);
        check(player->state().accessProtected, "local filesystem grants RH notification");
        if (paused) {
            player->togglePause();
            check(waitFor([&] { return player->state().paused; }), "pause protected audio");
            settle(5300);
            check(!player->state().suspended && player->state().loaded, "protected pause keeps its session beyond five seconds");
        }
        QElapsedTimer elapsed;
        elapsed.start();
        check(external("exclusive", path), "independent exclusive writer succeeds without retry");
        check(elapsed.elapsed() < 2000, "exclusive yield completes within two seconds");
        check(waitFor([&] { return widget->currentPath().isEmpty(); }), "exclusive request clears current media");
    }
#endif
    {
        const auto path = directory.filePath("fallback.wav");
        makeAudio(path);
        check(window.openPath(path) && waitFor([&] { return player->state().loaded; }), "load fallback media");
        player->setVolume(23);
        player->setRate(1.25);
        player->setMuted(true);
        player->setExternalProtectionEnabled(false);
        player->seek(12);
        check(waitFor([&] { return player->state().position >= 11.8; }), "fallback initial position");
        player->togglePause();
        check(waitFor([&] { return player->state().paused; }), "fallback pauses");
        int releases = 0;
        const auto connection = QObject::connect(player, &MpvPlayer::fileReleased, &app, [&](const QString&) { ++releases; });
        check(waitFor([&] { return player->state().suspended && releases > 0; }, 7000), "fallback releases five seconds after pausing");
        check(external(accessOperation, path), "suspended file permits external access");
        const auto renamed = directory.filePath("fallback-renamed.wav");
        check(external("rename", path, renamed), "rename released file");
        check(waitFor([&] { return widget->currentPath() == renamed; }), "released session follows rename");
        player->seek(20);
        check(waitFor([&] { return !player->state().restoring && !player->state().suspended && player->state().position >= 19.8; }), "seek restores a paused released session");
        check(player->state().paused && player->state().muted && std::abs(player->state().rate - 1.25) < .01
            && std::abs(player->state().volume - 23) < 1, "restore preserves settings and pause state");
        check(waitFor([&] { return player->state().suspended && releases > 1; }, 7000), "paused seek rearms fallback timer");
        player->togglePause();
        check(waitFor([&] { return !player->state().restoring && !player->state().paused && player->state().position >= 20; }), "play restores the released session");
        QObject::disconnect(connection);
        player->setExternalProtectionEnabled(true);
        check(external("delete", renamed), "delete restored file");
        check(waitFor([&] { return widget->currentPath().isEmpty(); }), "restored deletion closes media");
    }
    for (const auto* operation : {"delete", "replace", "restore-race"}) {
        const auto path = directory.filePath(QString("restore-%1.wav").arg(operation));
        makeAudio(path);
        MpvPlayer isolated;
        isolated.setExternalProtectionEnabled(false);
        int interruptions = 0;
        int released = 0;
        QObject::connect(&isolated, &MpvPlayer::interrupted, &app, [&](const QString& missing, const QString&) {
            check(missing == path, "restore failure reports its own path");
            ++interruptions;
        });
        QObject::connect(&isolated, &MpvPlayer::fileReleased, &app, [&](const QString&) { ++released; });
        isolated.open(path, 1, 0, MpvPlayer::RenderMode::Headless);
        check(waitFor([&] { return isolated.state().loaded; }), "isolated restore fixture loads");
        isolated.togglePause();
        check(waitFor([&] { return isolated.state().suspended && released; }, 7000), "isolated fixture releases on pause");
        const auto identity = FileIdentity::read(path);
        bool replaced = false;
        if (QByteArray(operation) == "restore-race") {
            QObject::connect(&isolated, &MpvPlayer::stateChanged, &isolated, [&, identity] {
                if (!isolated.state().restoring || replaced) return;
                replaced = true;
                // Keep GUI replies queued until the replacement is complete:
                // the original passed validation, but precise seek has not.
                check(QFile::rename(path, path + ".old") && QFile::copy(path + ".old", path),
                    "replace source while restoration is in flight");
                check(FileIdentity::read(path) != identity, "in-flight restore source identity changed");
            });
            isolated.togglePause();
            check(replaced, "in-flight restore replacement was exercised");
        } else {
            check(external(operation, path), "remove or replace released source without browser intervention");
            check(FileIdentity::read(path) != identity, "released source identity changed");
            isolated.togglePause();
        }
        check(waitFor([&] { return interruptions == 1 && !isolated.state().loaded; }), "restore rejects removed or replaced source before playback resumes");
    }
    {
        const auto path = directory.filePath("image.png");
        QImage picture(160, 100, QImage::Format_RGB32);
        picture.fill(Qt::red);
        picture.setText("Fixture note", "Keep metadata when renamed");
        check(picture.save(path), "create image fixture");
        check(window.openPath(path), "open image fixture");
        auto* view = widget->findChild<ImageView*>();
        check(waitFor([&] { return view->isVisible() && !view->scene()->items().isEmpty(); }), "image loads");
        view->actualSize(); view->zoom(2);
        const auto transform = view->transform();
        widget->findChild<QPushButton*>("imageDetailsButton")->click();
        auto* metadata = widget->findChild<QTableWidget*>("imageMetadataTable");
        check(metadata && waitFor([&] {
            return !metadata->findItems("Keep metadata when renamed", Qt::MatchExactly).isEmpty();
        }), "image text metadata is initially visible");
        int metadataReloads = 0;
        const auto metadataConnection = QObject::connect(widget->findChild<ImageLoader*>(), &ImageLoader::metadataLoaded,
            &app, [&](const QList<ImageMetadataEntry>&) { ++metadataReloads; });
        const auto renamed = directory.filePath("image-renamed.png");
        check(external("rename", path, renamed), "rename image externally");
        check(waitFor([&] { return widget->currentPath() == renamed; }), "image follows rename");
        check(view->transform() == transform, "image rename preserves zoom");
        check(waitFor([&] { return metadataReloads > 0; }), "renamed image metadata reload completes");
        check(metadata && !metadata->findItems("Keep metadata when renamed", Qt::MatchExactly).isEmpty(),
            "image rename preserves text metadata");
        QObject::disconnect(metadataConnection);
        if (metadata) metadata->window()->close();
        check(external(accessOperation, renamed), "displayed image permits external access");
        picture.fill(Qt::blue);
        check(picture.save(renamed), "replace displayed image pixels");
        check(waitFor([&] {
            for (auto* item : view->scene()->items()) {
                if (auto* pixels = qgraphicsitem_cast<QGraphicsPixmapItem*>(item))
                    return pixels->pixmap().toImage().pixelColor(1, 1) == QColor(Qt::blue);
            }
            return false;
        }), "image modification refreshes visible pixels");
        check(widget->currentPath() == renamed, "image modification keeps image selected");
        int refreshes = 0;
        const auto refreshConnection = QObject::connect(widget->findChild<ImageLoader*>(), &ImageLoader::loaded,
            &app, [&](const ImageResult&) { ++refreshes; });
        check(external("replace", renamed), "atomically replace image with identical bytes");
        check(waitFor([&] { return refreshes > 0; }), "identity replacement refreshes even same-size cached image");
        QObject::disconnect(refreshConnection);
        check(external("delete", renamed), "delete displayed image");
        check(waitFor([&] { return widget->currentPath().isEmpty(); }), "image deletion clears view");
        const auto raw = directory.filePath("synthetic.dng");
        check(QFile::copy(QStringLiteral(VIDEX_TEST_SOURCE_DIR "/tests/fixtures/synthetic.dng"), raw), "copy RAW fixture");
        ImageReadSnapshot snapshot;
        check(snapshot.read(raw, std::make_shared<std::atomic_bool>(false)), "immutable RAW input snapshot");
        check(external(accessOperation, raw), "snapshot permits external source access");
        check(external("delete", raw) && !snapshot.bytes().isEmpty(), "snapshot survives source deletion");
    }
    {
        const auto path = directory.filePath("large-snapshot.bin");
        QFile file(path);
        constexpr qint64 size = 33LL * 1024 * 1024;
        check(file.open(QIODevice::WriteOnly) && file.resize(size), "create disk-backed snapshot input");
        file.seek(size - 1); file.write("z", 1); file.close();
        {
            ImageReadSnapshot snapshot;
            check(snapshot.read(path, std::make_shared<std::atomic_bool>(false)), "snapshot spills large input to private temporary storage");
            check(snapshot.bytes().size() == size && snapshot.bytes().back() == 'z', "disk-backed snapshot retains complete data");
            check(external(accessOperation, path) && external("delete", path), "disk-backed snapshot survives external source access and deletion");
            check(snapshot.bytes().back() == 'z', "private mapping survives original deletion");
        }
        const auto cancelled = std::make_shared<std::atomic_bool>(true);
        ImageReadSnapshot cancelledRead;
        check(!cancelledRead.read(directory.filePath("missing"), cancelled), "cancelled source read does not open a file");
    }
    {
        const auto first = directory.filePath("switch-first.wav");
        const auto second = directory.filePath("switch-second.wav");
        makeAudio(first); makeAudio(second);
        check(window.openPath(first) && waitFor([&] { return player->state().loaded; }), "load before stale-monitor check");
        check(external("rename", first, directory.filePath("switch-renamed.wav")), "rename before switching selection");
        check(window.openPath(second), "switch before pending file inspection completes");
        check(waitFor([&] { return player->state().loaded && widget->currentPath() == second; }), "new selection loads");
        settle();
        check(widget->currentPath() == second && player->state().loaded, "old rename and restore events cannot change new selection");
        check(external("delete", second), "delete stale-monitor fixture");
        check(waitFor([&] { return widget->currentPath().isEmpty(); }), "new selection remains monitored");
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto first = directory.filePath(QStringLiteral("pending-%1.wav").arg(attempt));
        const auto second = directory.filePath(QStringLiteral("current-%1.wav").arg(attempt));
        makeAudio(first); makeAudio(second);
        check(window.openPath(first) && waitFor([&] { return player->state().loaded; }), "load before pending native break");
        QProcess rename;
        rename.start(QCoreApplication::applicationFilePath(),
            {"--external-operation", "rename", first, first + ".renamed", "--ready"});
        check(rename.waitForStarted(3000), "external rename process starts");
        if (!rename.bytesAvailable() && rename.state() != QProcess::NotRunning) rename.waitForReadyRead(3000);
        check(rename.readAllStandardOutput().contains("ready"), "external rename reaches pending break");
        QThread::msleep(50); // Let the child enter the OS request without draining Qt's notifier.
        check(window.openPath(second), "switch while old native notification is queued");
        check(waitFor([&] { return rename.state() == QProcess::NotRunning; }), "old rename completes after switching");
        check(rename.exitCode() == 0, "queued old rename succeeds");
        check(waitFor([&] { return player->state().loaded && widget->currentPath() == second; }), "old native break does not stop new selection");
        settle();
        check(widget->currentPath() == second && player->state().loaded, "new selection survives all stale native events");
        widget->setFile({});
    }
    {
        QStringList images;
        const auto jpeg = directory.filePath("sample.jpg");
        QImage picture(64, 48, QImage::Format_RGB32);
        picture.fill(Qt::green);
        check(picture.save(jpeg), "create JPEG operation fixture");
        images.append(jpeg);
        const auto svg = directory.filePath("sample.svg");
        QFile vector(svg);
        check(vector.open(QIODevice::WriteOnly), "create SVG operation fixture");
        vector.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"48\"><rect width=\"64\" height=\"48\" fill=\"red\"/></svg>");
        vector.close();
        images.append(svg);
        const auto raw = directory.filePath("sample.dng");
        check(QFile::copy(QStringLiteral(VIDEX_TEST_SOURCE_DIR "/tests/fixtures/synthetic.dng"), raw), "copy RAW operation fixture");
        images.append(raw);
#ifdef VIDEX_HEIF_TEST_FILES_DIR
        for (const auto* extension : {"heic", "avif"}) {
            const auto path = directory.filePath(QStringLiteral("sample.") + extension);
            check(QFile::copy(QStringLiteral(VIDEX_HEIF_TEST_FILES_DIR "/example.") + extension, path), "copy HEIF operation fixture");
            images.append(path);
        }
#endif
        for (const auto& path : images) {
            check(window.openPath(path), "open format-specific image");
            auto* view = widget->findChild<ImageView*>();
            check(waitFor([&] { return view->isVisible() && !view->scene()->items().isEmpty(); }), "decode format-specific image");
            const auto destination = QFileInfo(path).absolutePath() + "/renamed-" + QFileInfo(path).fileName();
            check(external("rename", path, destination), "format-specific image rename succeeds");
            check(waitFor([&] { return widget->currentPath() == destination; }), "format-specific image follows rename");
            check(external(accessOperation, destination), "format-specific image permits external access");
            check(external("delete", destination), "format-specific image deletion succeeds");
            check(waitFor([&] { return widget->currentPath().isEmpty(); }), "format-specific image deletion clears view");
        }
    }
    const auto video = qEnvironmentVariable("VIDEX_TEST_VIDEO");
    if (!video.isEmpty() && QGuiApplication::platformName() != "offscreen") {
        const auto path = directory.filePath("video.mp4");
        check(QFile::copy(video, path), "copy native video fixture");
        check(window.openPath(path) && waitFor([&] { return player->state().loaded && player->state().video; }), "load native video");
        auto* surface = widget->findChild<QOpenGLWidget*>("videoSurface");
        check(waitFor([&] { return surface && hasVideoColors(surface->grabFramebuffer()); }),
            "native video displays a decoded frame before pausing");
        player->setMuted(true);
        player->togglePause();
        check(waitFor([&] { return player->state().paused; }), "pause native video");
        player->setExternalProtectionEnabled(false);
        int releases = 0;
        const auto connection = QObject::connect(player, &MpvPlayer::fileReleased, &app, [&](const QString&) { ++releases; });
        check(waitFor([&] { return player->state().suspended && releases > 0; }, 7000), "native video releases source");
        QImage frozen;
        check(waitFor([&] {
            frozen = surface ? surface->grabFramebuffer() : QImage{};
            return hasVideoColors(frozen);
        }), "released native video retains a visible frozen frame after repaint");
        QDir().mkpath("artifacts/file-operations");
        if (!frozen.isNull()) frozen.save("artifacts/file-operations/frozen-frame.png");
        check(external(accessOperation, path), "frozen video permits external access");
        player->togglePause();
        check(waitFor([&] { return !player->state().restoring && !player->state().paused; }), "native video restores playback");
        player->setExternalProtectionEnabled(true);
#ifdef Q_OS_WIN
        check(external("exclusive", path), "active native video yields exclusive access");
        check(waitFor([&] { return widget->currentPath().isEmpty(); }), "native exclusive access clears video");
#else
        check(external("reader", path), "active native video allows a shared POSIX reader");
        check(widget->currentPath() == path && player->state().loaded, "POSIX reader preserves native playback");
        check(external("delete", path), "delete active native video using POSIX semantics");
        check(waitFor([&] { return widget->currentPath().isEmpty(); }), "native POSIX deletion clears video");
#endif
        QObject::disconnect(connection);
    }
    check(completions == 0, "external operations never trigger automatic-next completion");
    qInfo() << "File operation checks failures:" << failures;
    return failures ? 1 : 0;
}
