#pragma once
#include <QString>
#include <memory>
#include "app/UiState.h"
class QSettings;

class SettingsService
{
public:
    // An explicit INI file lets checks use isolated storage without touching user preferences.
    explicit SettingsService(const QString& filePath = {});
    ~SettingsService();
    QString lastDirectory() const;
    bool restoreDirectory() const;
    bool saveLastDirectory(const QString& path);
    bool saveRestoreDirectory(bool enabled);
    SessionSettings loadPreferences() const;
    bool savePreferences(const SessionSettings& preferences);
private:
    std::unique_ptr<QSettings> m_settings;
};
