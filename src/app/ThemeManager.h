#pragma once
#include "UiState.h"
#include <QObject>
#include <Qt>
class ThemeManager : public QObject
{
    Q_OBJECT
public:
    explicit ThemeManager(QObject* parent = nullptr);
    void setMode(ThemeMode mode);
    ThemeMode mode() const { return m_mode; }
    bool isDark() const { return m_dark; }
    static bool resolvesDark(ThemeMode mode, Qt::ColorScheme system);
signals:
    void themeChanged();
private:
    void apply();
    ThemeMode m_mode = ThemeMode::System;
    bool m_dark = false;
};
