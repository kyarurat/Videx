#pragma once
#include "app/UiState.h"
#include <QMainWindow>
class ThemeManager;
class UiPreviewController;
class ExplorerWidget;
class PlayerWidget;
class PlayerControls;
class SettingsDialog;
class AboutDialog;
class QLabel;
class QAction;
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(ThemeManager* theme, QWidget* parent = nullptr);
    ~MainWindow() override;
    void setDemoEnabled(bool enabled);
    void showSettings();
    void showAbout();
private:
    void createMenus();
    void applySettings(const SessionSettings& settings);
    void updateState(const MediaUiState& state);
    void showUnavailable();
    void toggleFullscreen();
    void syncFullscreen();
    void changeEvent(QEvent* event) override;
    ThemeManager* m_theme;
    UiPreviewController* m_preview;
    ExplorerWidget* m_explorer;
    PlayerWidget* m_player;
    PlayerControls* m_controls;
    SettingsDialog* m_settingsDialog = nullptr;
    AboutDialog* m_aboutDialog = nullptr;
    QLabel* m_status;
    QLabel* m_modeLabel;
    QAction* m_demoAction;
    QAction* m_explorerAction;
    QAction* m_statusAction;
    QAction* m_playAction;
    QAction* m_muteAction;
    QAction* m_fullscreenAction = nullptr;
    SessionSettings m_settings;
    Qt::WindowStates m_beforeFullscreen;
};
