#include "MainWindow.h"
#include "app/ThemeManager.h"
#include "app/BrowserController.h"
#include "media/MediaNavigation.h"
#include "services/SettingsService.h"
#include "ui/explorer/ExplorerWidget.h"
#include "ui/player/PlayerWidget.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/dialogs/AboutDialog.h"
#include "ui/dialogs/OpenPathDialog.h"
#include <QSplitter>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QLabel>
#include <QAction>
#include <QDir>
#include <QStandardPaths>
#include <QShortcut>
#include <QEvent>
#include <QCloseEvent>
#include <QScopedValueRollback>

MainWindow::MainWindow(ThemeManager* theme, SettingsService* settings, QWidget* parent)
    : QMainWindow(parent), m_theme(theme), m_settingsService(settings)
{
    setWindowTitle(tr("Videx — 本地媒体查看器"));
    resize(1200,760); setMinimumSize(900,600);
    m_settings = settings->loadPreferences();
    m_theme->setMode(m_settings.theme);
    m_browser = new BrowserController(settings, this);
    m_mediaNavigation = new MediaNavigation(this);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    m_splitter = new QSplitter(Qt::Horizontal,central);
    m_splitter->setChildrenCollapsible(false); m_splitter->setHandleWidth(1);
    m_explorer = new ExplorerWidget(theme,m_splitter);
    m_player = new PlayerWidget(theme,m_splitter);
    m_player->applyPlaybackPreferences(m_settings.defaultRate, m_settings.autoPlayNext);
    m_splitter->addWidget(m_explorer); m_splitter->addWidget(m_player);
    m_splitter->setStretchFactor(0,0); m_splitter->setStretchFactor(1,1);
    m_splitter->setSizes({260,940}); layout->addWidget(m_splitter,1);
    setCentralWidget(central);
    m_status = new QLabel(tr("就绪 · 请选择文件或文件夹"),this);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setMinimumWidth(0);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_modeLabel = new QLabel(tr("图片 · 视频 · 音频"),this);
    statusBar()->setSizeGripEnabled(true);
    statusBar()->addWidget(m_status,1); statusBar()->addPermanentWidget(m_modeLabel);
    const auto geometry = settings->windowGeometry();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
        setWindowState(windowState() & ~(Qt::WindowFullScreen | Qt::WindowMinimized));
    }
    const auto splitterState = settings->splitterState();
    if (!splitterState.isEmpty()) m_splitter->restoreState(splitterState);
    m_visibleSplitterState = m_splitter->saveState();
    createMenus();
    m_explorerAction->setChecked(m_settings.explorerVisible);
    m_statusAction->setChecked(m_settings.statusBarVisible);
    connect(m_explorer,&ExplorerWidget::openRequested,this,&MainWindow::choosePath);
    connect(m_player,&PlayerWidget::openRequested,this,&MainWindow::choosePath);
    connect(m_explorer,&ExplorerWidget::fileOpenRequested,m_browser,&BrowserController::selectFile);
    connect(m_explorer, &ExplorerWidget::playbackSeekRequested, m_player, &PlayerWidget::handleSeekKey);
    connect(m_explorer, &ExplorerWidget::playbackSeekCancelled, m_player, &PlayerWidget::cancelSeekHold);
    connect(m_player, &PlayerWidget::playbackSeekingEnabledChanged, m_explorer, &ExplorerWidget::setPlaybackSeekingEnabled);
    connect(m_explorer,&ExplorerWidget::hideRequested,this,[this] { m_explorerAction->setChecked(false); });
    connect(m_player,&PlayerWidget::showExplorerRequested,this,[this] { m_explorerAction->setChecked(true); });
    connect(m_player,&PlayerWidget::imageNavigationRequested,this,[this](int direction) {
        m_mediaNavigation->navigate(m_explorer->navigationSource(m_player->currentPath(), direction, MediaType::Image),
                                    MediaType::Image);
    });
    connect(m_player, &PlayerWidget::mediaNavigationRequested, this, [this](int direction) {
        m_mediaNavigation->navigate(m_explorer->navigationSource(m_player->currentPath(), direction, m_player->playbackCategory()),
            m_player->playbackCategory());
    });
    connect(m_mediaNavigation, &MediaNavigation::candidateSelected, m_browser, &BrowserController::selectFile);
    connect(m_player, &PlayerWidget::playbackResolved, m_mediaNavigation, &MediaNavigation::remember);
    connect(m_mediaNavigation, &MediaNavigation::failed, this, &MainWindow::showError);
    connect(m_explorer, &ExplorerWidget::contentsReady, this, [this] {
        m_player->prefetchImages(m_explorer->adjacentImages(m_player->currentPath()));
    });
    connect(m_browser,&BrowserController::directoryChanged,this,[this](const QString& path) {
        m_mediaNavigation->cancel();
        if (path.isEmpty()) m_explorer->clearDirectory();
        else m_explorer->setDirectory(path);
        m_player->setDirectory(path);
        m_status->setText(tr("已打开 · %1").arg(QDir::toNativeSeparators(path)));
        m_status->setToolTip(QDir::toNativeSeparators(path));
    });
    connect(m_browser,&BrowserController::fileChanged,this,[this](const FileDetails& details) {
        m_mediaNavigation->cancel();
        m_player->setFile(details);
        if (!details.path.isEmpty()) m_explorer->highlightFile(details.path);
        m_player->prefetchImages(m_explorer->adjacentImages(details.path));
        m_status->setText(details.path.isEmpty() ? tr("已打开 · %1").arg(QDir::toNativeSeparators(m_browser->directory()))
                                               : tr("已选择 · %1").arg(details.name));
        m_status->setToolTip(QDir::toNativeSeparators(details.path.isEmpty() ? m_browser->directory() : details.path));
    });
    connect(m_browser,&BrowserController::errorOccurred,this,&MainWindow::showError);
    connect(m_browser,&BrowserController::fileDetailsRefreshed,m_player,&PlayerWidget::refreshFileDetails);
    connect(m_player,&PlayerWidget::fullscreenRequested,this,&MainWindow::toggleFullscreen);
    m_browser->restoreLastDirectory();
    m_applyingSettings = false;
}

void MainWindow::createMenus()
{
    auto* file = menuBar()->addMenu(tr("文件(&F)"));
    auto* open = file->addAction(tr("打开文件或文件夹…"));
    open->setObjectName("openDirectoryAction");
    open->setShortcut(QKeySequence::Open);
    connect(open,&QAction::triggered,this,&MainWindow::choosePath);
    file->addSeparator();
    auto* settings = file->addAction(tr("设置…"));
    settings->setShortcut(QKeySequence("Ctrl+,"));
    connect(settings,&QAction::triggered,this,&MainWindow::showSettings);
    file->addSeparator();
    auto* exit = file->addAction(tr("退出"));
    connect(exit,&QAction::triggered,this,&QWidget::close);
    auto* view = menuBar()->addMenu(tr("视图(&V)"));
    m_explorerAction = view->addAction(tr("资源管理器"));
    m_explorerAction->setCheckable(true); m_explorerAction->setChecked(true);
    m_explorerAction->setShortcut(QKeySequence("Ctrl+B"));
    connect(m_explorerAction,&QAction::toggled,this,[this](bool visible) {
        if (!visible) m_visibleSplitterState = m_splitter->saveState();
        m_settings.explorerVisible=visible; m_explorer->setVisible(visible);
        if (visible) m_splitter->restoreState(m_visibleSplitterState);
        m_player->setExplorerVisible(visible);
        if (m_settingsDialog && !m_applyingSettings)
            m_settingsDialog->syncVisibility(m_settings.explorerVisible, m_settings.statusBarVisible);
        if (!m_applyingSettings) savePreferences();
    });
    m_statusAction = view->addAction(tr("状态栏"));
    m_statusAction->setCheckable(true); m_statusAction->setChecked(true);
    connect(m_statusAction,&QAction::toggled,this,[this](bool visible) {
        m_settings.statusBarVisible=visible; statusBar()->setVisible(visible);
        if (m_settingsDialog && !m_applyingSettings)
            m_settingsDialog->syncVisibility(m_settings.explorerVisible, m_settings.statusBarVisible);
        if (!m_applyingSettings) savePreferences();
    });
    m_fullscreenAction = view->addAction(tr("全屏"));
    m_fullscreenAction->setCheckable(true); m_fullscreenAction->setShortcut(QKeySequence("F"));
    connect(m_fullscreenAction,&QAction::triggered,this,&MainWindow::toggleFullscreen);
    auto* playback = menuBar()->addMenu(tr("播放(&P)"));
    connect(playback->addAction(tr("播放 / 暂停")), &QAction::triggered, m_player, &PlayerWidget::togglePause);
    connect(playback->addAction(tr("静音")), &QAction::triggered, m_player, &PlayerWidget::toggleMute);
    auto* help = menuBar()->addMenu(tr("帮助(&H)"));
    connect(help->addAction(tr("关于 Videx")),&QAction::triggered,this,&MainWindow::showAbout);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape),this);
    connect(escape,&QShortcut::activated,this,[this] { if (isFullScreen()) toggleFullscreen(); });
}

void MainWindow::choosePath()
{
    const QString initial = m_browser->directory().isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation) : m_browser->directory();
    OpenPathDialog dialog(initial, this);
    if (dialog.exec()==QDialog::Accepted) openPath(dialog.selectedPath());
}

bool MainWindow::openDirectory(const QString& path) { return m_browser->openDirectory(path); }
bool MainWindow::openPath(const QString& path) { return m_browser->openPath(path); }

void MainWindow::showError(const QString& message)
{
    m_player->setNotice(message);
    m_status->setText(tr("无法完成操作 · %1").arg(message.section('\n',0,0)));
    m_status->setToolTip(message);
}

void MainWindow::showSettings()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(this);
        connect(m_settingsDialog,&SettingsDialog::settingsApplied,this,&MainWindow::applySettings);
    }
    if (!m_settingsDialog->isVisible()) m_settingsDialog->setSettings(m_settings);
    m_settingsDialog->show(); m_settingsDialog->raise(); m_settingsDialog->activateWindow();
}

void MainWindow::showAbout()
{
    if (!m_aboutDialog) m_aboutDialog = new AboutDialog(this);
    m_aboutDialog->show(); m_aboutDialog->raise(); m_aboutDialog->activateWindow();
}

void MainWindow::applySettings(const SessionSettings& settings)
{
    const QScopedValueRollback<bool> applying(m_applyingSettings, true);
    m_settings=settings;
    m_theme->setMode(settings.theme);
    m_player->applyPlaybackPreferences(settings.defaultRate, settings.autoPlayNext);
    m_explorerAction->setChecked(settings.explorerVisible);
    m_statusAction->setChecked(settings.statusBarVisible);
    savePreferences();
}

void MainWindow::savePreferences()
{
    if (!m_settingsService->savePreferences(m_settings))
        showError(tr("设置已在当前窗口应用，但无法保存到本地。请检查应用配置的写入权限。"));
}

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) setWindowState(m_beforeFullscreen);
    else {
        m_beforeFullscreen = windowState();
        m_beforeFullscreenGeometry = saveGeometry();
        showFullScreen();
    }
    syncFullscreen();
}
void MainWindow::syncFullscreen()
{
    m_fullscreenAction->setChecked(isFullScreen()); m_player->setFullscreen(isFullScreen());
}
void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type()==QEvent::WindowStateChange && m_fullscreenAction) syncFullscreen();
}
void MainWindow::closeEvent(QCloseEvent* event)
{
    QMainWindow::closeEvent(event);
    if (!event->isAccepted()) return;
    const auto geometry = isFullScreen() && !m_beforeFullscreenGeometry.isEmpty()
        ? m_beforeFullscreenGeometry : saveGeometry();
    const auto splitter = m_explorer->isHidden() ? m_visibleSplitterState : m_splitter->saveState();
    m_settingsService->saveWindowLayout(geometry, splitter);
}
