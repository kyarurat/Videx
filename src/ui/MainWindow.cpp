#include "MainWindow.h"
#include "app/ThemeManager.h"
#include "app/UiPreviewController.h"
#include "ui/explorer/ExplorerWidget.h"
#include "ui/player/PlayerWidget.h"
#include "ui/player/PlayerControls.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/dialogs/AboutDialog.h"
#include <QSplitter>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QLabel>
#include <QAction>
#include <QMessageBox>
#include <QShortcut>
#include <QSlider>
#include <QComboBox>
#include <QApplication>
#include <QSignalBlocker>
#include <QEvent>

MainWindow::MainWindow(ThemeManager* theme, QWidget* parent) : QMainWindow(parent), m_theme(theme)
{
    setWindowTitle(tr("Videx — 视频与图片查看器")); resize(1200,760); setMinimumSize(900,600);
    m_preview = new UiPreviewController(this);
    auto* central = new QWidget(this); auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    auto* splitter = new QSplitter(Qt::Horizontal,central); splitter->setChildrenCollapsible(false); splitter->setHandleWidth(1);
    m_explorer = new ExplorerWidget(theme,splitter); m_player = new PlayerWidget(splitter);
    splitter->addWidget(m_explorer); splitter->addWidget(m_player); splitter->setStretchFactor(0,0); splitter->setStretchFactor(1,1);
    splitter->setSizes({260,940}); layout->addWidget(splitter,1);
    m_controls = new PlayerControls(theme,central); layout->addWidget(m_controls); setCentralWidget(central);
    m_status = new QLabel(this); m_modeLabel = new QLabel(tr("GUI 预览版"),this);
    statusBar()->setSizeGripEnabled(true); statusBar()->addWidget(m_status,1); statusBar()->addPermanentWidget(m_modeLabel);
    createMenus();
    connect(m_explorer,&ExplorerWidget::openRequested,this,&MainWindow::showUnavailable);
    connect(m_player,&PlayerWidget::openRequested,this,&MainWindow::showUnavailable);
    connect(m_player,&PlayerWidget::demoRequested,this,[this] { setDemoEnabled(true); });
    connect(m_player,&PlayerWidget::stageRequested,m_preview,&UiPreviewController::setStage);
    connect(m_explorer,&ExplorerWidget::fileActivated,m_preview,&UiPreviewController::openFile);
    connect(m_preview,&UiPreviewController::directoryReady,m_explorer,&ExplorerWidget::setDirectory);
    connect(m_preview,&UiPreviewController::directoryAboutToClose,m_explorer,&ExplorerWidget::clearDirectory);
    connect(m_preview,&UiPreviewController::stateChanged,this,&MainWindow::updateState);
    connect(m_preview,&UiPreviewController::failed,this,[this](const QString& message) { QMessageBox::warning(this,tr("演示无法启动"),message); });
    connect(m_controls,&PlayerControls::playRequested,m_preview,&UiPreviewController::togglePlayback);
    connect(m_controls,&PlayerControls::seekRequested,m_preview,&UiPreviewController::seek);
    connect(m_controls,&PlayerControls::volumeRequested,m_preview,&UiPreviewController::setVolume);
    connect(m_controls,&PlayerControls::muteRequested,m_preview,&UiPreviewController::toggleMute);
    connect(m_controls,&PlayerControls::rateRequested,m_preview,&UiPreviewController::setRate);
    connect(m_controls,&PlayerControls::fullscreenRequested,this,&MainWindow::toggleFullscreen);
    updateState(m_preview->state());
}
MainWindow::~MainWindow() { m_preview->stop(); }
void MainWindow::createMenus()
{
    auto* file = menuBar()->addMenu(tr("文件(&F)"));
    auto* open = file->addAction(tr("打开目录…")); open->setShortcut(QKeySequence::Open); connect(open,&QAction::triggered,this,&MainWindow::showUnavailable);
    file->addSeparator(); auto* settings = file->addAction(tr("设置…")); settings->setShortcut(QKeySequence("Ctrl+,")); connect(settings,&QAction::triggered,this,&MainWindow::showSettings);
    file->addSeparator(); auto* exit = file->addAction(tr("退出")); connect(exit,&QAction::triggered,this,&QWidget::close);
    auto* view = menuBar()->addMenu(tr("视图(&V)"));
    m_explorerAction = view->addAction(tr("资源管理器")); m_explorerAction->setCheckable(true); m_explorerAction->setChecked(true); m_explorerAction->setShortcut(QKeySequence("Ctrl+B"));
    connect(m_explorerAction,&QAction::toggled,this,[this](bool visible) { m_settings.explorerVisible=visible; m_explorer->setVisible(visible); });
    m_statusAction = view->addAction(tr("状态栏")); m_statusAction->setCheckable(true); m_statusAction->setChecked(true);
    connect(m_statusAction,&QAction::toggled,this,[this](bool visible) { m_settings.statusBarVisible=visible; statusBar()->setVisible(visible); });
    m_fullscreenAction = view->addAction(tr("全屏")); m_fullscreenAction->setCheckable(true); m_fullscreenAction->setShortcut(QKeySequence("F")); connect(m_fullscreenAction,&QAction::triggered,this,&MainWindow::toggleFullscreen);
    view->addSeparator(); m_demoAction = view->addAction(tr("界面演示")); m_demoAction->setCheckable(true); connect(m_demoAction,&QAction::toggled,this,&MainWindow::setDemoEnabled);
    auto* playback = menuBar()->addMenu(tr("播放(&P)"));
    m_playAction = playback->addAction(tr("播放 / 暂停")); m_playAction->setShortcut(QKeySequence(Qt::Key_Space)); connect(m_playAction,&QAction::triggered,m_preview,&UiPreviewController::togglePlayback);
    m_muteAction = playback->addAction(tr("静音")); m_muteAction->setCheckable(true); m_muteAction->setShortcut(QKeySequence("M")); connect(m_muteAction,&QAction::triggered,m_preview,&UiPreviewController::toggleMute);
    auto* help = menuBar()->addMenu(tr("帮助(&H)")); auto* about = help->addAction(tr("关于 Videx")); connect(about,&QAction::triggered,this,&MainWindow::showAbout);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape),this);
    connect(escape,&QShortcut::activated,this,[this] { if (isFullScreen()) toggleFullscreen(); });
    for (auto key : {Qt::Key_Left,Qt::Key_Right,Qt::Key_Up,Qt::Key_Down}) {
        auto* shortcut = new QShortcut(QKeySequence(key),m_player);
        // Keep arrow keys local to the player so the tree and sliders retain native navigation.
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut,&QShortcut::activated,this,[this,key] {
            const auto& state=m_preview->state();
            if (key==Qt::Key_Left || key==Qt::Key_Right) m_preview->seek(state.position+(key==Qt::Key_Left?-5:5));
            else m_preview->setVolume(state.volume+(key==Qt::Key_Up?5:-5));
        });
    }
    m_player->setFocusPolicy(Qt::StrongFocus);
}
void MainWindow::setDemoEnabled(bool enabled)
{
    if (enabled) {
        if (m_preview->start(m_settings.defaultRate)) m_player->setFocus();
    } else m_preview->stop();
    const QSignalBlocker block(m_demoAction); m_demoAction->setChecked(m_preview->state().demo);
}
void MainWindow::updateState(const MediaUiState& state)
{
    m_player->setState(state); m_controls->setState(state);
    if (state.demo) m_explorer->highlightFile(state.path);
    m_status->setText(state.demo ? tr("界面演示 · %1").arg(state.title) : tr("就绪 · 打开目录，开始浏览本地媒体"));
    m_modeLabel->setText(state.demo ? tr("模拟数据 · 不播放视频") : tr("GUI 预览版"));
    m_playAction->setEnabled(state.canControl()); m_muteAction->setEnabled(state.canControl());
    m_muteAction->setChecked(state.muted);
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
    m_settings=settings; m_theme->setMode(settings.theme);
    m_explorerAction->setChecked(settings.explorerVisible); m_statusAction->setChecked(settings.statusBarVisible);
}
void MainWindow::showUnavailable()
{
    QMessageBox::information(this,tr("打开目录"),tr("本次版本专注于界面展示，真实目录打开与视频播放尚未接入。\n可以通过“视图 → 界面演示”体验文件树与播放器状态。"));
}
void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) setWindowState(m_beforeFullscreen);
    else { m_beforeFullscreen=windowState(); showFullScreen(); }
    syncFullscreen();
}
void MainWindow::syncFullscreen()
{
    m_fullscreenAction->setChecked(isFullScreen()); m_controls->setFullscreen(isFullScreen());
}
void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type()==QEvent::WindowStateChange && m_fullscreenAction) syncFullscreen();
}
