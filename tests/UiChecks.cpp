#include "app/ThemeManager.h"
#include "app/BrowserController.h"
#include "services/SettingsService.h"
#include "ui/MainWindow.h"
#include "ui/explorer/ExplorerWidget.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/dialogs/OpenPathDialog.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QDir>
#include <QTreeView>
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QFileDialog>
#include <QLineEdit>
#include <QAction>
#include <QTimer>
#include <QProcess>
#include <QElapsedTimer>
#include <QThread>
#include <QTextStream>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDebug>
#include <QSettings>
#include <QStatusBar>
#include <QSplitter>
#include <functional>

namespace {
int failures = 0;
int checks = 0;
QStringList failedChecks;
void check(bool condition, const char* label)
{
    ++checks;
    if (!condition) { qCritical() << "FAIL:" << label; failedChecks.append(QString::fromUtf8(label)); ++failures; }
}
bool waitFor(const std::function<bool()>& predicate)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QApplication::processEvents();
        if (predicate()) return true;
        QThread::msleep(10);
    } while (timer.elapsed() < 5000);
    return false;
}
void writeFile(const QString& path)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "create isolated file fixture");
    if (file.isOpen()) check(file.write("metadata only\n") == 14, "write file fixture");
}
QFileSystemModel* model(QTreeView* tree)
{
    return qobject_cast<QFileSystemModel*>(tree->model());
}
void capture(QWidget* widget, const QString& path)
{
    QApplication::processEvents();
    check(widget->grab().save(path), "save visual artifact");
}
void doubleClick(QTreeView* tree, const QModelIndex& index)
{
    tree->scrollTo(index);
    QApplication::processEvents();
    const QPointF position=tree->visualRect(index).center();
    const QPointF global=tree->viewport()->mapToGlobal(position.toPoint());
    for (auto type : {QEvent::MouseButtonPress,QEvent::MouseButtonRelease,QEvent::MouseButtonDblClick,QEvent::MouseButtonRelease}) {
        QMouseEvent event(type,position,global,Qt::LeftButton,
                          type==QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(),&event);
    }
    QApplication::processEvents();
}
bool runChild(const QStringList& arguments)
{
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), arguments);
    if (!child.waitForStarted(5000)) return false;
    if (!child.waitForFinished(15000)) { child.kill(); child.waitForFinished(); return false; }
    if (child.exitCode() != 0) qWarning().noquote() << child.readAllStandardError();
    return child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0;
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("VidexChecks");
    app.setApplicationName("VidexChecks");
    app.setApplicationVersion("0.1.0");
    ThemeManager theme;
    const auto args = app.arguments();
    for (const auto& operation : {QStringLiteral("--write-layout"), QStringLiteral("--read-layout"),
                                  QStringLiteral("--write-hidden-fullscreen-layout"), QStringLiteral("--read-hidden-fullscreen-layout")}) {
        const int position = args.indexOf(operation);
        if (position < 0) continue;
        if (position + 1 >= args.size()) return 2;
        SettingsService settings(args[position+1]);
        MainWindow window(&theme, &settings);
        window.show();
        QApplication::processEvents();
        auto* splitter = window.findChild<QSplitter*>();
        auto* explorer = window.findChild<ExplorerWidget*>();
        QAction* sidebar = nullptr;
        QAction* fullscreen = nullptr;
        for (auto* action : window.findChildren<QAction*>()) {
            if (action->shortcut() == QKeySequence("Ctrl+B")) sidebar = action;
            if (action->shortcut() == QKeySequence("F")) fullscreen = action;
        }
        if (!sidebar || !fullscreen) return 2;
        const bool hiddenFullscreen = operation.contains("hidden-fullscreen");
        if (operation.startsWith("--write")) {
            window.resize(900, 600);
            window.move(50, 70);
            QApplication::processEvents();
            splitter->setSizes({330, splitter->width() - 331});
            QApplication::processEvents();
            check(qAbs(splitter->sizes().first() - 330) <= 1, "resize sidebar before saving layout");
            if (hiddenFullscreen) {
                sidebar->setChecked(false);
                fullscreen->trigger();
                QApplication::processEvents();
                check(window.isFullScreen(), "close layout fixture from fullscreen");
            }
            window.close();
            check(!settings.windowGeometry().isEmpty() && !settings.splitterState().isEmpty(), "close persists native window layout");
        } else {
            check(window.size() == QSize(900, 600), "restart restores normal window dimensions");
            check(!window.isFullScreen(), "restart after fullscreen opens a normal window");
            check(explorer->isHidden() == hiddenFullscreen, "layout restore preserves sidebar visibility preference");
            if (hiddenFullscreen) {
                sidebar->setChecked(true);
                QApplication::processEvents();
            }
            check(qAbs(splitter->sizes().first() - 330) <= 1, "restart and unhide restore resized sidebar width");
        }
        return failures == 0 ? 0 : 1;
    }
    const int sidebarRead = args.indexOf("--read-sidebar");
    if (sidebarRead >= 0) {
        if (sidebarRead + 2 >= args.size()) return 2;
        SettingsService settings(args[sidebarRead+1]);
        MainWindow window(&theme, &settings);
        const bool expected = args[sidebarRead+2] == "visible";
        return settings.loadPreferences().explorerVisible == expected
            && window.findChild<ExplorerWidget*>()->isHidden() != expected ? 0 : 1;
    }
    for (const auto& operation : {QStringLiteral("--write-preferences"), QStringLiteral("--read-preferences")}) {
        const int position = args.indexOf(operation);
        if (position < 0) continue;
        if (position + 1 >= args.size()) return 2;
        SettingsService settings(args[position+1]);
        MainWindow window(&theme, &settings);
        window.showSettings();
        auto* dialog=window.findChild<SettingsDialog*>();
        if (operation == "--write-preferences") {
            SessionSettings preferences;
            preferences.restoreDirectory=false;
            preferences.resumePlayback=false;
            preferences.autoPlayNext=true;
            preferences.explorerVisible=false;
            preferences.statusBarVisible=false;
            preferences.defaultRate=1.75;
            preferences.theme=ThemeMode::Dark;
            dialog->setSettings(preferences);
            dialog->findChild<QPushButton*>("applySettings")->click();
            dialog->setSettings({});
            dialog->reject(); // Unapplied edits must not overwrite the saved values.
            return 0;
        }
        const auto preferences=settings.loadPreferences();
        return !preferences.restoreDirectory && !preferences.resumePlayback && preferences.autoPlayNext
            && !preferences.explorerVisible && !preferences.statusBarVisible && preferences.defaultRate==1.75
            && preferences.theme==ThemeMode::Dark && theme.mode()==ThemeMode::Dark
            && window.findChild<ExplorerWidget*>()->isHidden() && window.statusBar()->isHidden()
            && dialog->findChild<QComboBox*>("themeMode")->currentData().toInt()==int(ThemeMode::Dark) ? 0 : 1;
    }
    for (const auto& operation : {QStringLiteral("--write-settings"), QStringLiteral("--read-settings")}) {
        const int position = args.indexOf(operation);
        if (position < 0) continue;
        if (position + 2 >= args.size()) return 2;
        SettingsService settings(args[position+1]);
        MainWindow window(&theme, &settings);
        if (operation == "--write-settings") return window.openPath(args[position+2]) ? 0 : 1;
        const auto expected = QDir::cleanPath(QFileInfo(args[position+2]).absoluteFilePath());
        return window.findChild<ExplorerWidget*>()->directory() == expected ? 0 : 1;
    }

    QTemporaryDir fixture;
    check(fixture.isValid(), "temporary test directory");
    if (!fixture.isValid()) return 1;
    const auto layoutConfig = fixture.filePath("layout.ini");
    check(runChild({"--write-layout", layoutConfig}), "save resized window layout in separate process");
    check(runChild({"--read-layout", layoutConfig}), "restart restores window and sidebar dimensions");
    const auto fullscreenLayoutConfig = fixture.filePath("fullscreen-layout.ini");
    check(runChild({"--write-hidden-fullscreen-layout", fullscreenLayoutConfig}), "save layout with sidebar hidden during fullscreen");
    check(runChild({"--read-hidden-fullscreen-layout", fullscreenLayoutConfig}), "restart restores normal geometry and hidden sidebar width");
    const QString library = fixture.path()+"/媒体与图片";
    const QString empty = fixture.path()+"/empty";
    check(QDir().mkpath(library+"/子文件夹/更深目录"), "create nested folders");
    check(QDir().mkpath(empty), "create empty folder");
    const QStringList files = {"01.mp4", "旅行 & 照片.PNG", "02.webp", "notes.txt", ".hidden", "no-extension"};
    for (const auto& file : files) writeFile(library+'/'+file);
    const QString nestedFile = library+"/子文件夹/更深目录/image.avif";
    writeFile(nestedFile);
    const QString config = fixture.path()+"/settings.ini";
    const QString preferencesConfig=fixture.path()+"/preferences.ini";
    check(runChild({"--write-preferences",preferencesConfig}), "apply settings in separate process");
    check(runChild({"--read-preferences",preferencesConfig}), "restart restores all preferences and cancel preserves saved values");
    {
        SettingsService preferences(preferencesConfig);
        check(preferences.savePreferences({}), "persist restored defaults");
        const auto defaults=SettingsService(preferencesConfig).loadPreferences();
        check(defaults.theme==ThemeMode::System && defaults.explorerVisible && defaults.statusBarVisible
              && defaults.restoreDirectory && defaults.defaultRate==1.0, "defaults survive reloading");
        QSettings invalid(preferencesConfig,QSettings::IniFormat);
        invalid.setValue("appearance/theme","unknown");
        invalid.setValue("playback/defaultRate", "invalid");
        invalid.sync();
        const auto recovered=SettingsService(preferencesConfig).loadPreferences();
        check(recovered.theme==ThemeMode::System && recovered.defaultRate==1.0, "invalid theme and rate use defaults");
    }

    SettingsService settings(config);
    check(settings.lastDirectory().isEmpty() && settings.restoreDirectory(), "first-run preference defaults");
    {
        BrowserController browser(&settings);
        int errors = 0;
        FileDetails selected;
        QObject::connect(&browser, &BrowserController::errorOccurred, [&errors] { ++errors; });
        QObject::connect(&browser, &BrowserController::fileChanged, [&selected](const FileDetails& info) { selected=info; });
        browser.restoreLastDirectory();
        check(browser.directory().isEmpty(), "no remembered directory stays empty");
        check(!browser.openDirectory({}) && errors==0, "cancel does not error");
        check(browser.openDirectory(library), "open real directory");
        check(settings.lastDirectory()==library, "remember successful directory");
        browser.selectFile(library+'/'+files[1]);
        check(selected.name==files[1] && selected.size==14 && selected.suffix=="PNG", "real file metadata without decoding");
        browser.selectFile(library+"/子文件夹");
        check(selected.name==files[1], "folder navigation preserves current file");
        check(!browser.openDirectory(library+"/missing") && browser.directory()==library, "bad folder preserves current folder");
        check(!browser.openDirectory(library+"/notes.txt"), "file rejected as folder");
        const QString removed = library+"/removed.txt";
        writeFile(removed);
        browser.selectFile(removed);
        check(QFile::remove(removed), "remove selected fixture");
        browser.selectFile(removed);
        check(selected.path.isEmpty() && errors==3, "deleted file produces actionable error");
        check(settings.lastDirectory()==library, "invalid requests do not overwrite remembered folder");
        check(browser.openPath(nestedFile) && browser.directory()==QFileInfo(nestedFile).absolutePath(), "opening file uses its parent as root");
        check(selected.path==nestedFile, "opening file immediately supplies metadata");
        check(!browser.openPath(library+"/absent.png") && selected.path==nestedFile, "invalid path does not replace active file");
        check(browser.openPath(library) && browser.directory()==library && selected.path.isEmpty(), "opening folder uses folder as root and clears file");
    }
    {
        SettingsService unwritable(fixture.path()); // A directory cannot be an INI file.
        BrowserController browser(&unwritable);
        bool reported = false;
        QObject::connect(&browser, &BrowserController::errorOccurred, [&reported] { reported=true; });
        check(browser.openDirectory(library) && reported, "storage failure reported while browsing remains usable");
    }
    const QString processConfig=fixture.path()+"/process.ini";
    const QStringList platform={"-platform", QGuiApplication::platformName()};
    check(runChild(platform+QStringList{"--write-settings",processConfig,library}), "separate process saves folder");
    check(runChild(platform+QStringList{"--read-settings",processConfig,library}), "new application process restores folder");
    check(runChild(platform+QStringList{"--write-settings",processConfig,nestedFile}), "separate process opens a file");
    check(runChild(platform+QStringList{"--read-settings",processConfig,QFileInfo(nestedFile).absolutePath()}), "restart restores parent folder of opened file");
    {
        SettingsService restored(config);
        check(restored.saveRestoreDirectory(false), "persist disabled restoration");
    }
    {
        SettingsService restored(config);
        BrowserController browser(&restored);
        browser.restoreLastDirectory();
        check(browser.directory().isEmpty(), "disabled restoration honored on restart");
        check(restored.saveRestoreDirectory(true), "reenable restoration");
    }
    const QString artifacts=QDir::current().absoluteFilePath("artifacts/browser");
    check(QDir().mkpath(artifacts), "create screenshot directory");
    SettingsService fresh(fixture.path()+"/fresh.ini");
    MainWindow window(&theme, &fresh);
    window.show();
    auto* tree=window.findChild<QTreeView*>("fileTree");
    auto* name=window.findChild<QLabel*>("selectedFileName");
    auto* location=window.findChild<QLabel*>("selectedFilePath");
    auto* notice=window.findChild<QLabel*>("browserNotice");
    check(tree && !tree->model(), "first-run tree empty");
    check(window.findChild<QPushButton*>("welcomeOpenDirectory")->isVisible(), "first-run prompt to choose folder");
    capture(&window,artifacts+"/first-run.png");
    // Accepting the picker exercises the menu entry and production open-directory flow.
    bool acceptedPicker=false;
    QTimer pickerTimer;
    QObject::connect(&pickerTimer,&QTimer::timeout,[&] {
        if (auto* picker=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            auto* entry=picker->findChild<QLineEdit*>("fileNameEdit");
            if (!entry) return;
            entry->setText(QDir::toNativeSeparators(library));
            auto* folder=picker->findChild<QPushButton*>("openPathButton");
            if (!folder) return;
            folder->click();
            acceptedPicker=picker->result()==QDialog::Accepted;
            pickerTimer.stop();
        }
    });
    pickerTimer.start(50);
    QTimer pickerTimeout;
    pickerTimeout.setSingleShot(true);
    QObject::connect(&pickerTimeout,&QTimer::timeout,[&] {
        if (auto* picker=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) picker->reject();
        pickerTimer.stop();
    });
    pickerTimeout.start(5000);
    window.findChild<QAction*>("openDirectoryAction")->trigger();
    pickerTimeout.stop();
    check(acceptedPicker, "folder picker accepted");
    check(waitFor([&] { return model(tree) && model(tree)->rowCount(tree->rootIndex())==files.size()+1; }),
          "file tree shows all files and child folder, including images and hidden files");
    if (!model(tree)) return 1;
    check(!tree->isExpanded(model(tree)->index(library+"/子文件夹")), "subfolders remain collapsed until requested");
    const auto picture=model(tree)->index(library+'/'+files[1]);
    tree->setCurrentIndex(picture);
    check(name->text().isEmpty(), "single selection does not open a file");
    doubleClick(tree,picture);
    check(name->text()==files[1] && location->text()==QDir::toNativeSeparators(library+'/'+files[1]), "double click opens file in right panel");
    QKeyEvent key(QEvent::KeyPress,Qt::Key_Up,Qt::NoModifier);
    QApplication::sendEvent(tree,&key);
    check(model(tree)->isDir(tree->currentIndex()) ? name->text()==files[1]
          : name->text()==model(tree)->fileName(tree->currentIndex()), "arrow navigation immediately opens files but preserves media on folders");
    const auto beforeFolderArrow = name->text();
    tree->setCurrentIndex(model(tree)->index(1, 0, tree->rootIndex()));
    QApplication::sendEvent(tree, &key);
    check(model(tree)->isDir(tree->currentIndex()) && !tree->isExpanded(tree->currentIndex())
          && name->text() == beforeFolderArrow, "arrow selection of a folder does not open or expand it");
    tree->setCurrentIndex(model(tree)->index(library+'/'+files[0]));
    QKeyEvent enterFile(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
    QApplication::sendEvent(tree,&enterFile);
    check(name->text()==files[0], "return opens selected tree file");
    tree->setCurrentIndex(picture);
    QKeyEvent keypadEnter(QEvent::KeyPress,Qt::Key_Enter,Qt::KeypadModifier);
    QApplication::sendEvent(tree,&keypadEnter);
    check(name->text()==files[1], "keypad enter opens selected tree file");
    const auto childFolder=model(tree)->index(library+"/子文件夹");
    tree->setCurrentIndex(childFolder);
    QApplication::sendEvent(tree,&enterFile);
    check(name->text()==files[1], "return on folder preserves current media");
    check(tree->isExpanded(childFolder), "return expands selected folder");
    QKeyEvent heldEnter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier,QString{},true);
    QApplication::sendEvent(tree,&heldEnter);
    check(tree->isExpanded(childFolder), "held return does not repeatedly toggle folder");
    QApplication::sendEvent(tree,&keypadEnter);
    check(!tree->isExpanded(childFolder) && name->text()==files[1], "keypad enter collapses folder without changing media");
    QApplication::sendEvent(tree,&keypadEnter);
    check(tree->isExpanded(childFolder), "keypad enter expands folder");
    QKeyEvent modifiedEnter(QEvent::KeyPress,Qt::Key_Enter,Qt::KeypadModifier | Qt::ControlModifier);
    QApplication::sendEvent(tree,&modifiedEnter);
    check(tree->isExpanded(childFolder) && name->text()==files[1], "modified enter does not activate folder");
    QApplication::sendEvent(tree,&enterFile);
    check(!tree->isExpanded(childFolder), "return collapses selected folder");
    tree->setCurrentIndex({});
    QApplication::sendEvent(tree,&enterFile);
    QApplication::sendEvent(tree,&keypadEnter);
    check(!tree->currentIndex().isValid() && name->text()==files[1], "enter on empty selection does nothing");
    tree->setCurrentIndex(childFolder);
    doubleClick(tree,childFolder);
    check(tree->isExpanded(childFolder) && name->text()==files[1], "double click folder expands without changing media");
    check(waitFor([&] { return model(tree)->rowCount(model(tree)->index(library+"/子文件夹"))==1; }), "expand child lazily");
    tree->expand(model(tree)->index(library+"/子文件夹/更深目录"));
    check(waitFor([&] { return model(tree)->rowCount(model(tree)->index(library+"/子文件夹/更深目录"))==1; }), "expand deeper folder lazily");
    doubleClick(tree,model(tree)->index(nestedFile));
    check(name->text()=="image.avif", "double click nested file switches media");
    tree->setCurrentIndex(model(tree)->index(library+"/子文件夹"));
    check(name->text()=="image.avif", "folder selection preserves prior file");
    doubleClick(tree,childFolder);
    check(!tree->isExpanded(childFolder) && name->text()=="image.avif", "double click folder collapses without changing media");
    doubleClick(tree,childFolder);
    auto* explorer=window.findChild<ExplorerWidget*>();
    const int sidebarWidth=explorer->width();
    auto* media=window.findChild<QLabel*>("browserNotice")->parentWidget();
    const int mediaWidth=media->width();
    window.findChild<QToolButton*>("hideExplorerButton")->click();
    QApplication::processEvents();
    check(!explorer->isVisible() && media->width()>=mediaWidth+sidebarWidth, "hide sidebar gives its width to media panel");
    check(runChild({"--read-sidebar",fixture.path()+"/fresh.ini","hidden"}), "sidebar hide survives process restart");
    capture(&window,artifacts+"/sidebar-hidden.png");
    QAction* sidebarAction=nullptr;
    for (auto* action : window.findChildren<QAction*>())
        if (action->shortcut()==QKeySequence("Ctrl+B")) sidebarAction=action;
    check(sidebarAction && !sidebarAction->isChecked(), "sidebar visibility action stays in sync");
    auto* restoreSidebar=window.findChild<QPushButton*>("showExplorerButton");
    restoreSidebar->click();
    QApplication::processEvents();
    check(explorer->isVisible() && !restoreSidebar->isVisible() && tree->isExpanded(childFolder) && name->text()=="image.avif", "restore button reopens sidebar and retains expansion and current media");
    check(runChild({"--read-sidebar",fixture.path()+"/fresh.ini","visible"}), "sidebar restore survives process restart");
    sidebarAction->trigger();
    check(!fresh.loadPreferences().explorerVisible, "sidebar menu action persists visibility");
    sidebarAction->trigger();
    {
        window.showSettings();
        auto* settingsDialog=window.findChild<SettingsDialog*>();
        auto* themeChoice=settingsDialog->findChild<QComboBox*>("themeMode");
        themeChoice->setCurrentIndex(themeChoice->findData(int(ThemeMode::Dark)));
        window.findChild<QToolButton*>("hideExplorerButton")->click();
        check(!settingsDialog->draft().explorerVisible, "unedited settings visibility follows sidebar actions");
        settingsDialog->findChild<QPushButton*>("applySettings")->click();
        check(!explorer->isVisible() && !fresh.loadPreferences().explorerVisible
              && fresh.loadPreferences().theme==ThemeMode::Dark, "theme apply preserves newer sidebar state");
        restoreSidebar->click();
        settingsDialog->findChild<QCheckBox*>("explorerVisible")->setChecked(false);
        sidebarAction->trigger();
        sidebarAction->trigger();
        check(!settingsDialog->draft().explorerVisible, "explicit visibility draft survives external changes");
        settingsDialog->reject();
        check(fresh.loadPreferences().explorerVisible, "cancel draft preserves external saved state");
    }
    {
        const QString changed=library+"/external-change.txt";
        const QString renamed=library+"/external-renamed.txt";
        writeFile(changed);
        check(window.openPath(changed), "open file before external rename");
        check(waitFor([&] { return model(tree)->rowCount(tree->rootIndex())>1; }), "tree loads before background refresh check");
        const auto other=model(tree)->index(library+'/'+files[0]);
        tree->setCurrentIndex(other);
        int refreshes=0;
        const auto refreshConnection=QObject::connect(window.findChild<BrowserController*>(),
            &BrowserController::fileDetailsRefreshed,[&](const FileDetails&) { ++refreshes; });
        {
            QFile file(changed);
            check(file.open(QIODevice::Append) && file.write("updated") == 7, "modify open file externally");
        }
        check(waitFor([&] { return refreshes>0; }), "background metadata refresh delivered");
        check(tree->currentIndex()==other && name->text()=="external-change.txt", "background refresh preserves tree selection and opened file");
        QObject::disconnect(refreshConnection);
        check(QFile::rename(changed,renamed), "rename file externally");
        check(waitFor([&] { return name->text().isEmpty() && notice->isVisible(); }), "external rename invalidates metadata without another selection");
        check(window.openPath(renamed), "select renamed file");
        check(QFile::remove(renamed), "delete file externally");
        check(waitFor([&] { return name->text().isEmpty() && notice->isVisible(); }), "external deletion invalidates metadata without another selection");
        check(window.openPath(nestedFile), "switch to another watched file");
        check(window.openDirectory(library), "changing directory clears old watcher");
        writeFile(nestedFile);
        QApplication::processEvents();
        check(name->text().isEmpty() && !notice->isVisible(), "old file changes do not restore cleared metadata");
    }
    bool cancelledPicker=false;
    QTimer::singleShot(100,[&] {
        if (auto* picker=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) { picker->reject(); cancelledPicker=true; }
    });
    window.findChild<QAction*>("openDirectoryAction")->trigger();
    check(cancelledPicker && fresh.lastDirectory()==library, "cancel picker preserves active and saved folder");
    for (auto mode : {ThemeMode::Light,ThemeMode::Dark}) {
        theme.setMode(mode);
        doubleClick(tree,model(tree)->index(library+'/'+files[1]));
        const QString prefix=mode==ThemeMode::Dark?"dark":"light";
        capture(&window,artifacts+'/'+prefix+"-selected.png");
        window.resize(900,600);
        capture(&window,artifacts+'/'+prefix+"-minimum.png");
        window.resize(1200,760);
        window.showSettings();
        auto* settings=window.findChild<SettingsDialog*>();
        settings->findChild<QListWidget*>()->setCurrentRow(2);
        auto* combo=settings->findChild<QComboBox*>("themeMode");
        combo->showPopup();
        capture(combo->view()->window(),artifacts+'/'+prefix+"-theme-popup.png");
        combo->hidePopup();
        settings->reject();
    }
    check(!window.findChild<QToolButton*>("playButton")->isEnabled(), "selection does not enable unimplemented playback");
    check(window.openDirectory(empty), "switch to empty directory");
    check(waitFor([&] { return model(tree)->rootPath()==empty && model(tree)->rowCount(tree->rootIndex())==0; }), "empty folder renders empty tree");
    check(name->text().isEmpty(), "switching folder clears file information");
    capture(&window,artifacts+"/empty-folder.png");
    check(!window.openDirectory(fixture.path()+"/missing") && notice->isVisible(), "invalid folder shown inline without blocking");
    check(window.findChild<ExplorerWidget*>()->directory()==empty, "failed open retains visible root");
    window.showSettings();
    auto* dialog=window.findChild<SettingsDialog*>();
    dialog->findChild<QCheckBox*>("restoreDirectory")->setChecked(false);
    dialog->findChild<QPushButton*>("applySettings")->click();
    check(!fresh.restoreDirectory(), "settings UI persists restore preference");
    dialog->findChild<QCheckBox*>("restoreDirectory")->setChecked(true);
    dialog->reject();
    check(!fresh.restoreDirectory(), "settings cancel leaves persisted preference unchanged");
    window.showSettings();
    dialog->findChild<QPushButton*>("restoreDefaults")->click();
    dialog->findChild<QPushButton*>("applySettings")->click();
    check(fresh.restoreDirectory(), "defaults reenable restoration");
    dialog->reject();
    {
        SettingsService rootSettings(fixture.path()+"/root-change.ini");
        MainWindow rootWindow(&theme,&rootSettings);
        const QString root=fixture.path()+"/watched-root";
        const QString moved=fixture.path()+"/moved-root";
        check(QDir().mkpath(root) && rootWindow.openDirectory(root), "open empty root without selecting file");
        check(QDir().rename(root,moved), "rename open root externally");
        auto* rootExplorer=rootWindow.findChild<ExplorerWidget*>();
        auto* rootTree=rootWindow.findChild<QTreeView*>("fileTree");
        auto* rootNotice=rootWindow.findChild<QLabel*>("browserNotice");
        check(waitFor([&] { return rootExplorer->directory().isEmpty() && !rootTree->model()
                                && !rootNotice->text().isEmpty(); }), "root rename clears stale browser and reports failure");
        check(rootWindow.openDirectory(moved) && QDir().rmdir(moved), "delete reopened empty root externally");
        check(waitFor([&] { return rootExplorer->directory().isEmpty() && !rootTree->model()
                                && !rootNotice->text().isEmpty(); }), "root deletion detected without a selected file");
        check(QDir().mkpath(root) && rootWindow.openDirectory(root) && rootWindow.openDirectory(library), "switch watched root");
        check(QDir().rmdir(root), "remove old root after switching");
        QApplication::processEvents();
        check(rootExplorer->directory()==library && rootNotice->text().isEmpty(), "old root removal does not clear current directory");
    }
    {
        SettingsService stale(fixture.path()+"/stale.ini");
        const QString gone=fixture.path()+"/gone";
        check(QDir().mkpath(gone) && stale.saveLastDirectory(gone) && QDir().rmdir(gone), "remember then delete folder");
        MainWindow missing(&theme,&stale);
        missing.show();
        check(!missing.findChild<QTreeView*>("fileTree")->model(), "missing saved folder falls back to unopened state");
        check(!missing.findChild<QLabel*>("browserNotice")->text().isEmpty(), "missing saved folder explains recovery");
        capture(&missing,artifacts+"/missing-folder.png");
    }
    check(window.openDirectory(library), "reopen after failed directory");
    check(notice->text().isEmpty(), "successful open clears error");
    bool acceptedFile=false;
    QTimer filePickerTimer;
    QObject::connect(&filePickerTimer,&QTimer::timeout,[&] {
        if (auto* picker=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            filePickerTimer.stop();
            auto* entry=picker->findChild<QLineEdit*>("fileNameEdit");
            entry->setText(QDir::toNativeSeparators(nestedFile));
            capture(picker,artifacts+"/open-picker.png");
            // Screenshot event processing can deliver the initial model selection.
            entry->setText(QDir::toNativeSeparators(nestedFile));
            QMetaObject::invokeMethod(picker,"accept",Qt::DirectConnection);
            acceptedFile=picker->result()==QDialog::Accepted;
        }
    });
    filePickerTimer.start(50);
    window.findChild<QAction*>("openDirectoryAction")->trigger();
    filePickerTimer.stop();
    check(acceptedFile && model(tree)->rootPath()==QFileInfo(nestedFile).absolutePath(), "file picker opens parent as root");
    check(name->text()=="image.avif", "file picker updates right-side metadata");
    check(waitFor([&] { return model(tree)->rowCount(tree->rootIndex())==1 && model(tree)->filePath(tree->currentIndex())==nestedFile; }), "selected file is highlighted after asynchronous load");
    capture(&window,artifacts+"/opened-file.png");
    OpenPathDialog currentFolder(empty,&window);
    currentFolder.show();
    QApplication::processEvents();
    currentFolder.findChild<QPushButton*>("openPathButton")->click();
    check(currentFolder.result()==QDialog::Accepted && currentFolder.selectedPath()==empty, "picker can choose its current folder without a file");
    {
        OpenPathDialog picker(library, &window);
        picker.setViewMode(QFileDialog::Detail);
        picker.show();
        auto* pickerTree=picker.findChild<QTreeView*>("treeView");
        auto* entry=picker.findChild<QLineEdit*>("fileNameEdit");
        auto* open=picker.findChild<QPushButton*>("openPathButton");
        check(picker.testOption(QFileDialog::ReadOnly), "picker filesystem is read only");
        check(!picker.findChild<QToolButton*>("newFolderButton")->isVisible(), "new folder button hidden");
        check(!picker.findChild<QComboBox*>("fileTypeCombo")->isVisible()
              && !picker.findChild<QLabel*>("fileTypeLabel")->isVisible(), "file type filtering hidden");
        const QString folderPath=QFileInfo(nestedFile).absolutePath();
        const QString childFolder=QDir(library).filePath(QString::fromUtf8("子文件夹"));
        check(waitFor([&] { return model(pickerTree)->index(childFolder).isValid(); }), "picker folder loaded");
        pickerTree->setCurrentIndex(model(pickerTree)->index(childFolder));
        QApplication::processEvents();
        check(entry->text()==QFileInfo(childFolder).fileName(), "folder selection fills name field");
        capture(&picker, artifacts+"/picker-folder-dark.png");
        const auto darkIcon=picker.findChild<QToolButton*>("toParentButton")->icon().cacheKey();
        theme.setMode(ThemeMode::Light);
        QApplication::processEvents();
        check(picker.findChild<QToolButton*>("toParentButton")->icon().cacheKey()!=darkIcon, "open picker navigation icons refresh with theme");
        capture(&picker, artifacts+"/picker-folder-light.png");
        theme.setMode(ThemeMode::Dark);
        doubleClick(pickerTree,model(pickerTree)->index(childFolder));
        check(picker.isVisible() && picker.directory().absolutePath()==childFolder, "double click folder navigates without accepting");
        check(entry->text().isEmpty(), "folder navigation clears stale selection");
        entry->setText("missing-entry");
        open->click();
        check(picker.isVisible() && picker.selectedPath().isEmpty(), "invalid path keeps picker open");
        entry->setText(folderPath);
        open->click();
        check(picker.result()==QDialog::Accepted && picker.selectedPath()==folderPath, "unified button accepts folder");
    }
    {
        OpenPathDialog picker(QFileInfo(nestedFile).absolutePath(), &window);
        picker.setViewMode(QFileDialog::Detail);
        picker.show();
        auto* pickerTree=picker.findChild<QTreeView*>("treeView");
        check(waitFor([&] { return model(pickerTree)->rowCount(pickerTree->rootIndex())==1; }), "picker file loaded");
        doubleClick(pickerTree,model(pickerTree)->index(nestedFile));
        check(picker.result()==QDialog::Accepted && picker.selectedPath()==nestedFile, "double click file accepts selected file");
    }
    {
        OpenPathDialog picker(library, &window);
        picker.show();
        QApplication::processEvents();
        auto* entry=picker.findChild<QLineEdit*>("fileNameEdit");
        entry->setText(QFileInfo(nestedFile).absolutePath());
        entry->setFocus();
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(entry,&enter);
        check(picker.result()==QDialog::Accepted && picker.selectedPath()==QFileInfo(nestedFile).absolutePath(), "enter accepts folder using same path logic");
    }
    QFile report(artifacts+"/checks.txt");
    if (report.open(QIODevice::WriteOnly|QIODevice::Text)) {
        QTextStream stream(&report);
        stream << "Platform: " << QGuiApplication::platformName() << "\nChecks: " << checks
               << "\nFailures: " << failures << "\nDevice pixel ratio: " << window.devicePixelRatioF() << '\n';
        for (const auto& failure : failedChecks) stream << "FAIL: " << failure << '\n';
    }
    qInfo() << "Browser checks:" << checks << "Failures:" << failures;
    return failures ? 1 : 0;
}
