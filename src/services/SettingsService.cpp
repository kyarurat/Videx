#include "SettingsService.h"
#include <QSettings>
#include <QDebug>
#include <algorithm>
#include <cmath>

SettingsService::SettingsService(const QString& filePath)
    : m_settings(filePath.isEmpty() ? std::make_unique<QSettings>()
                                   : std::make_unique<QSettings>(filePath, QSettings::IniFormat))
{
    m_settings->setFallbacksEnabled(false);
}
SettingsService::~SettingsService() = default;
QByteArray SettingsService::windowGeometry() const
{
    return m_settings->value("window/geometry").toByteArray();
}
QByteArray SettingsService::splitterState() const
{
    return m_settings->value("window/splitterState").toByteArray();
}
bool SettingsService::saveWindowLayout(const QByteArray& geometry, const QByteArray& splitter)
{
    m_settings->setValue("window/geometry", geometry);
    m_settings->setValue("window/splitterState", splitter);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        qWarning() << "Cannot save window layout in" << m_settings->fileName();
        return false;
    }
    return true;
}
SessionSettings SettingsService::loadPreferences() const
{
    SessionSettings preferences;
    preferences.restoreDirectory = restoreDirectory();
    const auto language = m_settings->value("appearance/language", "zh_CN").toString();
    if (language == "en_US") preferences.language = language;
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
    m_settings->setValue("appearance/language", preferences.language == "en_US" ? "en_US" : "zh_CN");
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

SavedSession SettingsService::loadSession() const
{
    SavedSession s;
    s.directory = m_settings->value("session/directory", lastDirectory()).toString();
    s.file = m_settings->value("session/file", QString{}).toString();
    s.selection = m_settings->value("session/selection", QString{}).toString();
    s.expanded = m_settings->value("session/expanded", QStringList{}).toStringList();
    s.scroll = m_settings->value("session/scroll", 0).toInt();
    s.position = m_settings->value("session/position", 0).toDouble();
    s.duration = m_settings->value("session/duration", 0).toDouble();
    s.rate = m_settings->value("session/rate", 1).toDouble();
    s.volume = m_settings->value("session/volume", 70).toDouble();
    s.paused = m_settings->value("session/paused", true).toBool();
    s.muted = m_settings->value("session/muted", false).toBool();
    if (!std::isfinite(s.position) || s.position < 0) s.position = 0;
    if (!std::isfinite(s.rate) || s.rate < 0.1 || s.rate > 10) s.rate = 1;
    if (!std::isfinite(s.volume)) s.volume = 70;
    s.volume = std::clamp(s.volume, 0.0, 100.0);
    s.scroll = std::max(0, s.scroll);
    return s;
}
bool SettingsService::saveSession(const SavedSession& s)
{
    m_settings->setValue("session/directory", s.directory);
    m_settings->setValue("session/file", s.file);
    m_settings->setValue("session/selection", s.selection);
    m_settings->setValue("session/expanded", s.expanded);
    m_settings->setValue("session/scroll", s.scroll);
    m_settings->setValue("session/position", s.position);
    m_settings->setValue("session/duration", s.duration);
    m_settings->setValue("session/rate", s.rate);
    m_settings->setValue("session/volume", s.volume);
    m_settings->setValue("session/paused", s.paused);
    m_settings->setValue("session/muted", s.muted);
    m_settings->setValue("session/lastPlayedAt", QDateTime::currentDateTimeUtc());
    m_settings->sync();
    if (m_settings->status() == QSettings::NoError) return true;
    qWarning() << "Cannot save session in" << m_settings->fileName();
    return false;
}
