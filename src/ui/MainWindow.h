#pragma once
#include "app/UiState.h"
#include <QMainWindow>
class ThemeManager;
class BrowserController;
class SettingsService;
class ExplorerWidget;
class PlayerWidget;
class SettingsDialog;
class AboutDialog;
class QLabel;
class QAction;
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(ThemeManager* theme, SettingsService* settings, QWidget* parent = nullptr);
    bool openDirectory(const QString& path);
    bool openPath(const QString& path);
    void showSettings();
    void showAbout();
private:
    void createMenus();
    void applySettings(const SessionSettings& settings);
    void savePreferences();
    void choosePath();
    void showError(const QString& message);
    void toggleFullscreen();
    void syncFullscreen();
    void changeEvent(QEvent* event) override;
    ThemeManager* m_theme;
    SettingsService* m_settingsService;
    BrowserController* m_browser;
    ExplorerWidget* m_explorer;
    PlayerWidget* m_player;
    SettingsDialog* m_settingsDialog = nullptr;
    AboutDialog* m_aboutDialog = nullptr;
    QLabel* m_status;
    QLabel* m_modeLabel;
    QAction* m_explorerAction;
    QAction* m_statusAction;
    QAction* m_fullscreenAction = nullptr;
    SessionSettings m_settings;
    bool m_applyingSettings = true;
    Qt::WindowStates m_beforeFullscreen;
};
