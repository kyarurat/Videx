#include "app/ThemeManager.h"
#include "app/UiPreviewController.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/dialogs/AboutDialog.h"
#include "ui/common/UiComponents.h"
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QListWidget>
#include <QTreeView>
#include <QFileInfo>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <QDebug>
#include <QStyleHints>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QAction>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAbstractItemView>

namespace {
int failures = 0;
int checks = 0;
void check(bool condition, const char* label)
{
    ++checks;
    if (!condition) { qCritical() << "FAIL:" << label; ++failures; }
}
void settle()
{
    QElapsedTimer timer; timer.start();
    while (timer.elapsed()<160) { QApplication::processEvents(); QThread::msleep(5); }
}
void capture(QWidget* widget, const QString& path)
{
    settle(); check(widget->grab().save(path), "save screenshot");
}
}
int main(int argc, char** argv)
{
    QApplication app(argc,argv);
    app.setApplicationName("Videx"); app.setApplicationVersion("0.1.0");
    ThemeManager theme;
    for (auto scheme : {Qt::ColorScheme::Unknown,Qt::ColorScheme::Light,Qt::ColorScheme::Dark}) {
        check(ThemeManager::resolvesDark(ThemeMode::Dark,scheme),"forced dark");
        check(!ThemeManager::resolvesDark(ThemeMode::Light,scheme),"forced light");
        check(ThemeManager::resolvesDark(ThemeMode::System,scheme)==(scheme==Qt::ColorScheme::Dark),"system resolution");
    }
    check(formatTime(0)=="00:00" && formatTime(3661)=="1:01:01","time formatting");
    UiPreviewController preview;
    QString root;
    QObject::connect(&preview,&UiPreviewController::directoryReady,[&root](const QString& path){root=path;});
    check(preview.start(1.25),"create preview");
    check(QFileInfo::exists(preview.state().path),"fixture file exists");
    preview.setStage(PlaybackStage::Failed); preview.seek(500);
    check(!preview.state().canControl() && preview.state().position==222,"failed state ignores seek");
    preview.setStage(PlaybackStage::Playing); preview.seek(99999);
    check(preview.state().position==preview.state().duration,"seek clamps");
    preview.setStage(PlaybackStage::Finished); preview.togglePlayback();
    check(preview.state().stage==PlaybackStage::Playing && preview.state().position==0,"replay starts at zero");
    preview.stop(); check(!QFileInfo::exists(root),"fixture removed");
    const QString output=QDir::current().absoluteFilePath("artifacts/ui");
    check(QDir().mkpath(output),"artifact directory");
    MainWindow window(&theme); window.show(); settle();
    auto* player=window.findChild<QToolButton*>("playButton");
    check(player && !player->isEnabled(),"empty disables play");
    for (auto mode : {ThemeMode::Light,ThemeMode::Dark}) {
        const QString name=mode==ThemeMode::Dark?"dark":"light";
        theme.setMode(mode); capture(&window,output+'/'+name+"-empty.png");
        window.setDemoEnabled(true); settle();
        check(player->isEnabled(),"demo enables play");
        auto* tree=window.findChild<QTreeView*>("fileTree");
        check(tree && tree->model() && tree->currentIndex().isValid(),"tree highlights current file");
        capture(&window,output+'/'+name+"-playing.png");
        auto* menu=window.menuBar()->actions().at(1)->menu();
        menu->popup(window.menuBar()->mapToGlobal(QPoint(80,window.menuBar()->height())));
        capture(menu,output+'/'+name+"-menu.png"); menu->hide();
        auto* stages=window.findChild<QComboBox*>("previewStage");
        for (int index=0;index<stages->count();++index) {
            stages->setCurrentIndex(index); settle();
            const auto stage=static_cast<PlaybackStage>(stages->currentData().toInt());
            check(player->isEnabled()==(stage!=PlaybackStage::Loading && stage!=PlaybackStage::Failed),"stage control availability");
            capture(&window,output+'/'+name+QString("-stage-%1.png").arg(index));
        }
        window.showSettings(); settle();
        auto* settings=window.findChild<SettingsDialog*>();
        window.showSettings();
        check(window.findChildren<SettingsDialog*>().size()==1,"single settings instance");
        for (int page=0;page<2;++page) {
            settings->findChild<QListWidget*>("settingsNavigation")->setCurrentRow(page);
            capture(settings,output+'/'+name+QString("-settings-page-%1.png").arg(page));
        }
        settings->findChild<QListWidget*>("settingsNavigation")->setCurrentRow(2);
        capture(settings,output+'/'+name+"-settings.png"); settings->reject();
        window.showSettings();
        auto* themeChoice=settings->findChild<QComboBox*>("themeMode");
        themeChoice->showPopup();
        capture(themeChoice->view()->window(),output+'/'+name+"-dropdown.png");
        themeChoice->hidePopup(); settings->reject();
        window.showAbout(); settle(); auto* about=window.findChild<AboutDialog*>();
        capture(about,output+'/'+name+"-about.png"); about->reject();
        window.resize(900,600); capture(&window,output+'/'+name+"-minimum.png"); window.resize(1200,760);
        window.setDemoEnabled(false); check(!tree->model(),"tree releases model on exit");
    }
    window.showSettings(); settle();
    auto* settings=window.findChild<SettingsDialog*>();
    auto* combo=settings->findChild<QComboBox*>("themeMode");
    combo->setCurrentIndex(combo->findData(int(ThemeMode::Dark)));
    settings->findChild<QPushButton*>("applySettings")->click();
    check(theme.mode()==ThemeMode::Dark && theme.isDark(),"apply theme");
    combo->setCurrentIndex(combo->findData(int(ThemeMode::Light))); settings->reject();
    check(theme.mode()==ThemeMode::Dark,"cancel preserves theme");
    window.showSettings(); check(settings->draft().theme==ThemeMode::Dark,"reopen resets draft");
    settings->findChild<QPushButton*>("restoreDefaults")->click();
    check(theme.mode()==ThemeMode::Dark,"defaults remain draft");
    settings->findChild<QPushButton*>("applySettings")->click();
    check(theme.mode()==ThemeMode::System,"defaults apply system");
    settings->reject();
    window.showAbout();
    theme.setMode(ThemeMode::Light); settle();
    check(window.palette().color(QPalette::Window).lightness()>128,"light main window");
    check(window.findChild<AboutDialog*>()->palette().color(QPalette::Window).lightness()>128,"light open dialog");
    theme.setMode(ThemeMode::Dark); settle();
    check(window.findChild<AboutDialog*>()->palette().color(QPalette::Window).lightness()<128,"dark open dialog");
    window.findChild<AboutDialog*>()->reject();
    window.showMaximized(); settle();
    check(window.isMaximized(),"maximize");
    QAction* fullscreen=nullptr;
    for (auto* action : window.findChildren<QAction*>())
        if (action->shortcut()==QKeySequence("F")) fullscreen=action;
    check(fullscreen!=nullptr,"fullscreen action exists");
    if (fullscreen) {
        fullscreen->trigger(); settle(); check(window.isFullScreen(),"enter fullscreen");
        fullscreen->trigger(); settle(); check(window.isMaximized(),"restore maximized after fullscreen");
    }
    window.showNormal();
    for (int i=0;i<3;++i) { window.setDemoEnabled(true); settle(); window.setDemoEnabled(false); }
    if (app.arguments().contains("--inspect")) { window.setDemoEnabled(true); return app.exec(); }
    QFile report(output+"/checks.txt");
    if (report.open(QIODevice::WriteOnly|QIODevice::Text)) {
        QTextStream stream(&report);
        stream << "Platform: " << QGuiApplication::platformName() << "\nChecks: " << checks
               << "\nFailures: " << failures << "\nDevice pixel ratio: " << window.devicePixelRatioF() << '\n';
    }
    qInfo() << "UI checks finished; checks:" << checks << "failures:" << failures << "screenshots:" << output;
    return failures?1:0;
}
