#include "media/ImageDecoder.h"
#include "media/ImageLoader.h"
#include "ui/images/ImagePane.h"
#include "ui/images/ImageView.h"
#include "app/ThemeManager.h"
#include "services/SettingsService.h"
#include "ui/MainWindow.h"
#include <exiv2/exiv2.hpp>
#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImageWriter>
#include <QLabel>
#include <QPainter>
#include <QKeyEvent>
#include <QFileSystemModel>
#include <QGraphicsScene>
#include <QTreeView>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QSemaphore>
#include <functional>

namespace {
class ScreenScaleView : public ImageView
{
public:
    void changeScreenScale(qreal ratio)
    {
        m_ratio = ratio;
        QEvent event(QEvent::ScreenChangeInternal);
        QApplication::sendEvent(this, &event);
    }
protected:
    int metric(PaintDeviceMetric value) const override
    {
        if (value == PdmDevicePixelRatio) return int(m_ratio);
        if (value == PdmDevicePixelRatioScaled) return qRound(m_ratio * devicePixelRatioFScale());
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        if (value == PdmDevicePixelRatioF_EncodedA || value == PdmDevicePixelRatioF_EncodedB)
            return encodeMetricF(value, m_ratio);
#endif
        return ImageView::metric(value);
    }
private:
    qreal m_ratio = 1.0;
};

int failures = 0;
void check(bool condition, const char* message)
{
    if (!condition) { ++failures; qCritical() << "FAIL:" << message; }
}
bool waitFor(const std::function<bool()>& predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000) {
        QApplication::processEvents();
        if (predicate()) return true;
        QThread::msleep(10);
    }
    return false;
}
void writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "write image fixture");
}
ImageResult decode(const QString& path)
{
    return ImageDecoder::decode(path, std::make_shared<std::atomic_bool>(false));
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    check(directory.isValid(), "isolated image fixture directory");
    QImage fixture(180, 120, QImage::Format_RGB32);
    fixture.fill(QColor(230, 70, 30));
    for (int y = 40; y < 100; ++y)
        for (int x = 70; x < 150; ++x) fixture.setPixelColor(x, y, QColor(20, 100, 220));
    const QStringList formats{"png", "jpg", "bmp", "ppm", "xbm", "xpm", "ico", "webp", "tiff"};
    for (const auto& format : formats) {
        const auto path = directory.filePath(QStringLiteral("图片 01.") + format);
        check(fixture.save(path, format.toLatin1()), qPrintable("encode fixture " + format));
        const auto result = decode(path);
        check(result.status == ImageResult::Status::Ready, qPrintable("decode " + format));
        check(result.image.size() == fixture.size(), "image dimensions preserved");
    }
    const auto png = directory.filePath(QStringLiteral("图片 01.png"));
    const auto svg = directory.filePath("vector.svg");
    writeFile(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"180\" height=\"120\"><rect width=\"180\" height=\"120\" fill=\"#ff0000\"/></svg>");
    const auto vector = decode(svg);
    check(vector.status == ImageResult::Status::Ready && vector.image.size() == QSize(180,120), "SVG decode");
    check(vector.image.pixelColor(50,50).red() == 255, "SVG visible pixels");
    const auto gif = directory.filePath("one.gif");
    writeFile(gif, QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"));
    check(decode(gif).status == ImageResult::Status::Ready, "GIF first frame");
    const auto raw = decode(QStringLiteral(VIDEX_TEST_SOURCE_DIR "/tests/fixtures/synthetic.dng"));
    check(raw.status == ImageResult::Status::Ready && !raw.image.isNull(), "full RAW Bayer DNG decoding without embedded preview");
    for (const auto* name : {"raw-no-extension", "raw-disguised.tif"}) {
        const auto path = directory.filePath(name);
        check(QFile::copy(QStringLiteral(VIDEX_TEST_SOURCE_DIR "/tests/fixtures/synthetic.dng"), path), "copy renamed RAW fixture");
        const auto renamedRaw = decode(path);
        check(renamedRaw.status == ImageResult::Status::Ready && renamedRaw.image == raw.image,
              "TIFF-container RAW identified by contents preserves full decoded pixels");
    }
    const auto ordinaryTiff = directory.filePath("ordinary-tiff.raw");
    check(fixture.save(ordinaryTiff, "TIFF"), "encode TIFF with RAW suffix");
    const auto tiffResult = decode(ordinaryTiff);
    check(tiffResult.status == ImageResult::Status::Ready && tiffResult.image.size() == fixture.size()
          && tiffResult.format == "TIFF", "ordinary TIFF retains Qt decoding despite RAW suffix");
#ifdef VIDEX_HEIF_TEST_FILES_DIR
    for (const auto& extension : {QStringLiteral("heic"), QStringLiteral("avif")}) {
        const auto result = decode(QStringLiteral(VIDEX_HEIF_TEST_FILES_DIR "/example.") + extension);
        check(result.status == ImageResult::Status::Ready && !result.image.isNull(), qPrintable("real " + extension + " decode"));
        qInfo() << extension << result.image.size() << result.message;
    }
#endif
    const auto renamed = directory.filePath("no-extension");
    check(QFile::copy(png, renamed), "copy extensionless image");
    check(decode(renamed).status == ImageResult::Status::Ready, "content detection without extension");
    const auto disguised = directory.filePath("renamed.raw");
    check(QFile::copy(png, disguised), "copy image with misleading extension");
    check(decode(disguised).status == ImageResult::Status::Ready, "extension is a hint rather than a decoder guarantee");
    const auto video = directory.filePath("video.mp4");
    writeFile(video, QByteArray::fromHex("000000186674797069736f6d0000000069736f326d703431"));
    check(decode(video).status == ImageResult::Status::Unsupported, "MP4 is not misclassified as HEIF");
    const auto unsupported = directory.filePath("notes.txt");
    writeFile(unsupported, "Not an image");
    check(decode(unsupported).status == ImageResult::Status::Unsupported, "unknown format rejected");
    const auto broken = directory.filePath("broken.png");
    writeFile(broken, QByteArray::fromHex("89504e470d0a1a0a0000"));
    check(decode(broken).status != ImageResult::Status::Ready, "truncated PNG rejected");
    check(decode(directory.filePath("gone.png")).status == ImageResult::Status::Failed, "missing file rejected");
    const auto huge = directory.filePath("huge.svg");
    writeFile(huge, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100000\" height=\"100000\"/>");
    check(decode(huge).status == ImageResult::Status::Failed, "oversized image rejected before allocation");

    const auto photo = directory.filePath("metadata.jpg");
    check(fixture.save(photo), "JPEG metadata fixture");
    try {
        auto image = Exiv2::ImageFactory::open(photo.toStdString());
        image->readMetadata();
        auto& exif = image->exifData();
        exif["Exif.Image.Make"] = "Videx Test Camera";
        exif["Exif.Photo.FNumber"] = Exiv2::Rational(28, 10);
        exif["Exif.Photo.ExposureTime"] = Exiv2::Rational(1, 125);
        exif["Exif.Photo.ISOSpeedRatings"] = uint16_t(400);
        exif["Exif.Image.Orientation"] = uint16_t(6);
        exif["Exif.GPSInfo.GPSLatitude"] = "1/1 18/1 0/1";
        exif["Exif.GPSInfo.GPSLatitudeRef"] = "N";
        exif["Exif.GPSInfo.GPSLongitude"] = "103/1 48/1 0/1";
        exif["Exif.GPSInfo.GPSLongitudeRef"] = "E";
        image->xmpData()["Xmp.xmp.Rating"] = "4";
        image->xmpData()["Xmp.dc.description"] = "lang=\"x-default\" Videx XMP description";
        image->writeMetadata();
    } catch (const Exiv2::Error& error) { qCritical() << error.what(); ++failures; }
    const auto metadata = decode(photo);
    check(metadata.image.size() == QSize(120,180), "EXIF orientation applied");
    const auto pixelsOnly = ImageDecoder::decode(photo, std::make_shared<std::atomic_bool>(false), false);
    check(pixelsOnly.image == metadata.image, "deferring metadata preserves orientation and decoded pixels");
    const auto hasValue = [&metadata](const QString& value) {
        for (const auto& entry : metadata.metadata) if (entry.value.contains(value)) return true;
        return false;
    };
    check(hasValue("Videx Test Camera"), "camera metadata read");
    check(hasValue("2.8"), "aperture metadata read");
    check(hasValue("1.300000, 103.800000"), "GPS converted to decimal coordinates");
    check(hasValue("Videx XMP description"), "embedded XMP description read");
    bool hasRating = false;
    for (const auto& entry : metadata.metadata)
        if (entry.name == "Xmp.xmp.Rating" && entry.value == "4") hasRating = true;
    check(hasRating, "embedded XMP rating read");

    ImageLoader loader;
    int results = 0;
    ImageResult last;
    QObject::connect(&loader, &ImageLoader::loaded, &app, [&](const ImageResult& result) { ++results; last = result; });
    loader.open(png);
    loader.open(svg);
    loader.open(unsupported);
    check(waitFor([&] { return results == 1; }), "latest image request finishes");
    check(last.status == ImageResult::Status::Unsupported, "stale image result discarded");
    results = 0;
    loader.open(png);
    loader.clear();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 150) { app.processEvents(); QThread::msleep(5); }
    check(results == 0, "cleared view cannot be repopulated by worker");

    ImageLoader preload;
    int visibleLoads = 0;
    QSize loadedSize;
    QObject::connect(&preload, &ImageLoader::loaded, &app, [&](const ImageResult& result) {
        ++visibleLoads;
        loadedSize = result.image.size();
    });
    // Highly compressed files still have a large decoded footprint.
    QImage large(4096, 3072, QImage::Format_RGB32);
    large.fill(QColor(40, 90, 150));
    const auto largeJpg = directory.filePath("large.jpg");
    const auto largePng = directory.filePath("large.png");
    check(large.save(largeJpg) && large.save(largePng), "large JPG and PNG fixtures");
    preload.prefetch({largeJpg, largePng});
    check(waitFor([&] { return preload.isCached(largeJpg) && preload.isCached(largePng); }), "preload large JPG and PNG");
    check(visibleLoads == 0, "background preload never changes the visible image");
    for (const auto& path : {largeJpg, largePng}) {
        const int before = visibleLoads;
        preload.open(path);
        check(visibleLoads == before + 1 && loadedSize == large.size(), "preloaded large image opens without waiting for decoding");
    }
    QImage changed(71, 53, QImage::Format_RGB32);
    changed.fill(Qt::red);
    check(changed.save(largePng), "replace cached file");
    check(!preload.isCached(largePng), "changed file invalidates cached pixels");
    preload.open(largePng);
    check(waitFor([&] { return loadedSize == changed.size(); }), "modified image decodes fresh pixels");
    preload.clear();
    check(!preload.isCached(largeJpg), "closing directory releases image cache");
    const int beforePromotion = visibleLoads;
    preload.prefetch({largeJpg});
    preload.open(largeJpg);
    check(waitFor([&] { return visibleLoads == beforePromotion + 1; }), "in-flight preload becomes the selected image");
    check(loadedSize == large.size(), "promoted preload preserves full resolution");
    preload.clear();
    preload.prefetch({largeJpg});
    preload.open(svg);
    preload.open(png);
    preload.open(unsupported);
    const int beforeSwitch = visibleLoads;
    check(waitFor([&] { return visibleLoads == beforeSwitch + 1; }), "rapid switch cancels obsolete preloads");
    check(loadedSize.isEmpty(), "obsolete image cannot replace the unsupported view");
    preload.clear();

    // Deterministically hold a non-interruptible background decode. A new
    // foreground image must complete before that background task is released.
    {
        QSemaphore started, release;
        ImageLoader concurrent(nullptr, [&](const QString& path, const auto&) {
            if (path == largeJpg) { started.release(); release.acquire(); }
            ImageResult result;
            result.status = ImageResult::Status::Ready;
            result.image = QImage(path == largeJpg ? QSize(7, 7) : QSize(9, 9), QImage::Format_RGB32);
            return result;
        }, [](const QString&) { return QList<ImageMetadataEntry>{}; });
        QSize visible;
        QObject::connect(&concurrent, &ImageLoader::loaded, &app, [&](const ImageResult& result) { visible = result.image.size(); });
        concurrent.prefetch({largeJpg});
        check(started.tryAcquire(1, 5000), "background decode starts");
        concurrent.open(png);
        check(waitFor([&] { return visible == QSize(9, 9); }), "foreground bypasses blocked background decode");
        release.release();
        concurrent.clear();
    }
    {
        const auto changingPath = directory.filePath("changing.png");
        writeFile(changingPath, "old");
        QSemaphore started, release;
        std::atomic_int decodes{0};
        ImageLoader changing(nullptr, [&](const QString&, const auto&) {
            const int attempt = ++decodes;
            if (attempt == 1) { started.release(); release.acquire(); }
            ImageResult result;
            result.status = ImageResult::Status::Ready;
            result.image = QImage(attempt == 1 ? QSize(7, 7) : QSize(9, 9), QImage::Format_RGB32);
            return result;
        }, [](const QString&) { return QList<ImageMetadataEntry>{}; });
        int delivered = 0;
        QSize visible;
        QObject::connect(&changing, &ImageLoader::loaded, &app, [&](const ImageResult& result) {
            ++delivered; visible = result.image.size();
        });
        changing.prefetch({changingPath});
        check(started.tryAcquire(1, 5000), "mutable preload starts");
        writeFile(changingPath, "new contents with different size");
        changing.open(changingPath);
        release.release();
        check(waitFor([&] { return delivered == 1; }), "changed preload delivers a fresh result");
        check(visible == QSize(9, 9) && decodes == 2, "changed preload is decoded again without displaying stale pixels");
        check(changing.isCached(changingPath), "fresh replacement is cached");
    }
    {
        QSemaphore metadataStarted, releaseMetadata;
        ImageLoader deferred(nullptr, [](const QString&, const auto&) {
            ImageResult result;
            result.status = ImageResult::Status::Ready;
            result.image = QImage(5, 5, QImage::Format_RGB32);
            return result;
        }, [&](const QString& path) {
            if (path == png) { metadataStarted.release(); releaseMetadata.acquire(); }
            return QList<ImageMetadataEntry>{{"path", path}};
        });
        int pictures = 0, metadataResults = 0;
        QString metadataPath;
        QObject::connect(&deferred, &ImageLoader::loaded, &app, [&](const ImageResult&) { ++pictures; });
        QObject::connect(&deferred, &ImageLoader::metadataLoaded, &app, [&](const auto& entries) {
            ++metadataResults; metadataPath = entries.first().value;
        });
        deferred.open(png);
        check(waitFor([&] { return pictures == 1; }), "pixels arrive before slow metadata");
        check(metadataStarted.tryAcquire(1, 5000), "deferred metadata starts");
        deferred.open(svg);
        check(waitFor([&] { return pictures == 2; }), "switching images does not wait for old metadata");
        check(metadataResults == 0, "blocked metadata has not been delivered");
        releaseMetadata.release();
        check(waitFor([&] { return metadataResults == 1; }), "latest metadata eventually arrives");
        check(metadataPath == svg, "old metadata cannot overwrite the selected image");
        deferred.open(png);
        check(metadataStarted.tryAcquire(1, 5000), "cached image also schedules deferred metadata");
        deferred.clear();
        deferred.open(svg);
        releaseMetadata.release();
        check(waitFor([&] { return metadataResults == 2; }), "metadata worker remains usable after clear");
        check(metadataPath == svg, "clear discards metadata from the old session");
    }

    ImagePane pane;
    pane.resize(900,600);
    pane.show();
    pane.open(photo);
    auto* details = pane.findChild<QPushButton*>("imageDetailsButton");
    check(waitFor([&] { return details->isVisible(); }), "image view loads asynchronously");
    auto* view = pane.findChild<ImageView*>();
    view->actualSize();
    check(qFuzzyCompare(view->transform().m11() * view->devicePixelRatioF(), 1.0), "actual pixel size at desktop scale");
    view->zoom(2);
    check(qFuzzyCompare(view->transform().m11() * view->devicePixelRatioF(), 2.0), "zoom");
    view->fitImage();
    check(view->transform().m11() <= 1.0, "fit avoids enlarging small image");
    details->click();
    auto* dialog = pane.findChild<QDialog*>("imageDetailsDialog");
    check(dialog && dialog->isVisible(), "details button opens dialog");
    auto* table = pane.findChild<QTableWidget*>("imageMetadataTable");
    check(waitFor([&] { return table && table->rowCount() > 8; }), "details receives photographic metadata asynchronously");
    QDir().mkpath("artifacts/images");
    app.processEvents();
    pane.grab().save("artifacts/images/viewer.png");
    if (dialog) dialog->grab().save("artifacts/images/details.png");
    pane.clear();
    check(!dialog || !dialog->isVisible(), "switching files closes stale details");
    pane.hide();
    // Asset filenames can carry a DPR hint, but a media viewer's 100% must
    // still map each source pixel to one physical screen pixel.
    const auto retinaPath = directory.filePath("asset@2x.png");
    check(fixture.save(retinaPath), "save high-DPI filename fixture");
    const auto retina = decode(retinaPath);
    check(retina.status == ImageResult::Status::Ready, "decode high-DPI filename fixture");
    ImageView scalingView;
    scalingView.resize(600, 400);
    scalingView.setImage(retina.image);
    scalingView.actualSize();
    const auto physicalWidth = [&scalingView] {
        return scalingView.transform().mapRect(scalingView.sceneRect()).width()
            * scalingView.devicePixelRatioF();
    };
    check(qFuzzyCompare(physicalWidth(), double(fixture.width())), "@2x actual size preserves source pixels");
    scalingView.zoom(2);
    check(qFuzzyCompare(physicalWidth(), double(fixture.width() * 2)), "@2x zoom preserves source pixel ratio");

    ScreenScaleView screens;
    screens.resize(600, 400);
    screens.setImage(retina.image);
    double reportedScale = 0;
    QObject::connect(&screens, &ImageView::scaleChanged, &app, [&](double scale) { reportedScale = scale; });
    screens.actualSize();
    for (const qreal ratio : {2.0, 1.5, 1.0}) {
        screens.changeScreenScale(ratio);
        check(qFuzzyCompare(screens.devicePixelRatioF(), ratio), "simulated screen DPR is applied");
        check(qFuzzyCompare(screens.transform().m11() * ratio, 1.0) && qFuzzyCompare(reportedScale, 1.0),
              "actual size and label survive a screen scale change");
    }
    screens.zoom(2.0);
    screens.changeScreenScale(2.0);
    check(qFuzzyCompare(screens.transform().m11() * 2.0, 2.0) && qFuzzyCompare(reportedScale, 2.0),
          "manual zoom survives a screen scale change");
    screens.fitImage();
    screens.changeScreenScale(1.5);
    check(screens.transform().m11() * 1.5 <= 1.000001, "fit mode preserves small image size on a different screen");

    // Render through the scene, so the test catches the pixmap item's paint
    // method overriding the view's smooth-rendering hint.
    QImage edge(2, 2, QImage::Format_RGB32);
    edge.fill(Qt::black);
    edge.setPixelColor(1, 0, Qt::white);
    edge.setPixelColor(1, 1, Qt::white);
    scalingView.setImage(edge);
    QImage interpolated(31, 31, QImage::Format_RGB32);
    interpolated.fill(Qt::black);
    {
        QPainter painter(&interpolated);
        scalingView.scene()->render(&painter, QRectF(0, 0, 31, 31), scalingView.sceneRect());
    }
    const int midpoint = interpolated.pixelColor(15, 15).red();
    check(midpoint > 20 && midpoint < 235, "scaled image interpolates the pixel boundary");
    ThemeManager theme;
    SettingsService settings(directory.filePath("settings.ini"));
    MainWindow window(&theme, &settings);
    window.show();
    check(window.openPath(png), "open actual image through browser controller");
    auto* windowDetails = window.findChild<QPushButton*>("imageDetailsButton");
    check(waitFor([&] { return windowDetails->isVisible(); }), "browser opens image content");
    auto* unsupportedLabel = window.findChild<QLabel*>("unsupportedFormatMessage");
    check(window.openPath(unsupported), "open unsupported file through browser");
    check(waitFor([&] { return unsupportedLabel->isVisible(); }), "unsupported view appears");
    check(unsupportedLabel->text() == QString::fromUtf8("格式不支持"), "unsupported format message is explicit");
    check(!windowDetails->isVisible(), "unsupported view hides image tools");
    window.openPath(png);
    check(waitFor([&] { return windowDetails->isVisible(); }), "return from unsupported file to image");
    const auto navigationDirectory = directory.filePath("navigation");
    QDir().mkpath(navigationDirectory);
    for (const auto* name : {"1.png", "2.png", "10.png"})
        check(fixture.save(navigationDirectory + '/' + name), "navigation image fixture");
    writeFile(navigationDirectory + "/3.txt", "unsupported");
    window.openPath(navigationDirectory + "/2.png");
    auto* tree = window.findChild<QTreeView*>();
    check(waitFor([&] { return windowDetails->isVisible() && tree->model()->rowCount(tree->rootIndex()) == 4; }), "navigation directory loaded");
    auto* windowLoader = window.findChild<ImageLoader*>();
    check(waitFor([&] { return windowLoader->isCached(navigationDirectory + "/10.png")
        && windowLoader->isCached(navigationDirectory + "/1.png"); }), "browser preloads both neighbors in tree order");
    auto* next = window.findChild<QPushButton*>("nextImageButton");
    auto* previous = window.findChild<QPushButton*>("previousImageButton");
    auto* selectedName = window.findChild<QLabel*>("selectedFileName");
    next->click();
    check(waitFor([&] { return selectedName->text() == "10.png" && windowDetails->isVisible(); }), "next image follows natural tree order and skips text");
    next->click();
    check(selectedName->text() == "10.png", "last image does not wrap");
    previous->click();
    check(waitFor([&] { return selectedName->text() == "2.png" && windowDetails->isVisible(); }), "previous image follows tree order");
    window.activateWindow();
    auto* keyboardView = window.findChild<ImageView*>();
    keyboardView->setFocus();
    check(waitFor([&] { return keyboardView->hasFocus(); }), "viewer receives keyboard focus");
    const auto pressPage = [&](int key) {
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
        if (auto* focused = QApplication::focusWidget()) {
            QApplication::sendEvent(focused, &press);
            QApplication::sendEvent(focused, &release);
        }
    };
    pressPage(Qt::Key_PageUp);
    check(waitFor([&] { return selectedName->text() == "1.png" && windowDetails->isVisible(); }), "PageUp navigates from focused viewer");
    check(keyboardView->hasFocus(), "viewer retains focus after previous image");
    pressPage(Qt::Key_PageDown);
    check(waitFor([&] { return selectedName->text() == "2.png" && windowDetails->isVisible(); }), "PageDown works without refocusing");
    pressPage(Qt::Key_PageDown);
    check(waitFor([&] { return selectedName->text() == "10.png" && windowDetails->isVisible(); }), "consecutive PageDown works without refocusing");
    pressPage(Qt::Key_PageUp);
    check(waitFor([&] { return selectedName->text() == "2.png" && windowDetails->isVisible(); }), "return to middle image by keyboard");
    tree->setFocus();
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(tree, &down);
    check(selectedName->text() == "3.txt", "down arrow opens unsupported file immediately");
    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(tree, &up);
    check(waitFor([&] { return selectedName->text() == "2.png" && windowDetails->isVisible(); }), "up arrow opens image without Enter");
    check(tree->hasFocus(), "image completion does not steal file tree focus");
    QImage replacement(63, 45, QImage::Format_RGB32);
    replacement.fill(Qt::green);
    check(replacement.save(navigationDirectory + "/2.png"), "overwrite viewed image");
    auto* currentView = window.findChild<ImageView*>();
    check(waitFor([&] { return windowDetails->isVisible() && currentView->sceneRect().width() == 63; }), "external file modification reloads image");
    window.resize(900, 600);
    theme.setMode(ThemeMode::Dark);
    app.processEvents();
    window.grab().save("artifacts/images/window-dark.png");
    theme.setMode(ThemeMode::Light);
    app.processEvents();
    window.grab().save("artifacts/images/window-light.png");
    window.close();
    // Optional real camera corpus, kept outside the repository for licensing/size.
    const auto corpus = qEnvironmentVariable("VIDEX_RAW_FIXTURES");
    if (!corpus.isEmpty()) {
        for (const auto& file : QDir(corpus).entryInfoList(QDir::Files)) {
            QElapsedTimer cold;
            cold.start();
            const auto result = decode(file.absoluteFilePath());
            const auto coldMs = cold.elapsed();
            check(result.status == ImageResult::Status::Ready, qPrintable("camera RAW: " + file.fileName()));
            qInfo() << file.fileName() << result.image.size() << result.format << result.message;
            preload.prefetch({file.absoluteFilePath()});
            check(waitFor([&] { return preload.isCached(file.absoluteFilePath()); }), "camera RAW preload completes");
            const int before = visibleLoads;
            cold.restart();
            preload.open(file.absoluteFilePath());
            qInfo() << "Cold decode ms:" << coldMs << "cached open ms:" << cold.elapsed();
            check(visibleLoads == before + 1, "camera RAW cache hit avoids decoding");
        }
    }
    qInfo() << "Image checks failures:" << failures;
    return failures == 0 ? 0 : 1;
}
