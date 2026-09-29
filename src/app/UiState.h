#pragma once
#include <QString>
#include <QDateTime>
enum class ThemeMode { System, Dark, Light };
struct SessionSettings
{
    bool restoreDirectory = true;
    bool resumePlayback = true;
    bool autoPlayNext = false;
    bool explorerVisible = true;
    bool statusBarVisible = true;
    double defaultRate = 1.0;
    ThemeMode theme = ThemeMode::System;
};
struct FileDetails
{
    QString path;
    QString name;
    QString suffix;
    qint64 size = 0;
    QDateTime modified;
};
