#pragma once
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <memory>
#include "app/UiState.h"
class QSettings;
struct SavedSession
{
    QString directory, file, selection;
    QStringList expanded;
    int scroll = 0;
    double position = 0, duration = 0, rate = 1, volume = 70;
    bool paused = true, muted = false;
};

class SettingsService
{
public:
    // An explicit INI file lets checks use isolated storage without touching user preferences.
    explicit SettingsService(const QString& filePath = {});
    ~SettingsService();
    SavedSession loadSession() const;
    bool saveSession(const SavedSession& session);
    QString lastDirectory() const;
    bool restoreDirectory() const;
    bool saveLastDirectory(const QString& path);
    bool saveRestoreDirectory(bool enabled);
    SessionSettings loadPreferences() const;
    bool savePreferences(const SessionSettings& preferences);
    QByteArray windowGeometry() const;
    QByteArray splitterState() const;
    bool saveWindowLayout(const QByteArray& geometry, const QByteArray& splitter);
private:
    std::unique_ptr<QSettings> m_settings;
};
