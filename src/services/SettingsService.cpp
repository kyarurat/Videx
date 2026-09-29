#include "SettingsService.h"
#include <QSettings>
#include <QDebug>

SettingsService::SettingsService(const QString& filePath)
    : m_settings(filePath.isEmpty() ? std::make_unique<QSettings>()
                                   : std::make_unique<QSettings>(filePath, QSettings::IniFormat))
{
    m_settings->setFallbacksEnabled(false);
}
SettingsService::~SettingsService() = default;
SessionSettings SettingsService::loadPreferences() const
{
    SessionSettings preferences;
    preferences.restoreDirectory = restoreDirectory();
    preferences.resumePlayback = m_settings->value("playback/resume", preferences.resumePlayback).toBool();
    preferences.autoPlayNext = m_settings->value("playback/autoNext", preferences.autoPlayNext).toBool();
    preferences.explorerVisible = m_settings->value("appearance/explorerVisible", preferences.explorerVisible).toBool();
    preferences.statusBarVisible = m_settings->value("appearance/statusBarVisible", preferences.statusBarVisible).toBool();
    const QString theme = m_settings->value("appearance/theme", "system").toString();
    if (theme == "dark") preferences.theme = ThemeMode::Dark;
    else if (theme == "light") preferences.theme = ThemeMode::Light;
    const double rate = m_settings->value("playback/defaultRate", preferences.defaultRate).toDouble();
    for (double supported : {0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0})
        if (rate == supported) preferences.defaultRate = rate;
    return preferences;
}

bool SettingsService::savePreferences(const SessionSettings& preferences)
{
    m_settings->setValue("browser/restoreDirectory", preferences.restoreDirectory);
    m_settings->setValue("playback/resume", preferences.resumePlayback);
    m_settings->setValue("playback/autoNext", preferences.autoPlayNext);
    m_settings->setValue("playback/defaultRate", preferences.defaultRate);
    m_settings->setValue("appearance/explorerVisible", preferences.explorerVisible);
    m_settings->setValue("appearance/statusBarVisible", preferences.statusBarVisible);
    m_settings->setValue("appearance/theme", preferences.theme == ThemeMode::Dark ? "dark"
                                          : preferences.theme == ThemeMode::Light ? "light" : "system");
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        qWarning() << "Cannot save preferences in" << m_settings->fileName();
        return false;
    }
    return true;
}
QString SettingsService::lastDirectory() const
{
    return m_settings->value(QStringLiteral("browser/lastDirectory")).toString();
}
bool SettingsService::restoreDirectory() const
{
    return m_settings->value(QStringLiteral("browser/restoreDirectory"), true).toBool();
}
bool SettingsService::saveLastDirectory(const QString& path)
{
    m_settings->setValue(QStringLiteral("browser/lastDirectory"), path);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        qWarning() << "Cannot save last directory in" << m_settings->fileName();
        return false;
    }
    return true;
}
bool SettingsService::saveRestoreDirectory(bool enabled)
{
    m_settings->setValue(QStringLiteral("browser/restoreDirectory"), enabled);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        qWarning() << "Cannot save directory restoration preference in" << m_settings->fileName();
        return false;
    }
    return true;
}
