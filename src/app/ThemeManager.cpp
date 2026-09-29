#include "ThemeManager.h"
#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStyleHints>
#include <QDebug>
#include <QFontDatabase>
ThemeManager::ThemeManager(QObject* parent) : QObject(parent)
{
    Q_INIT_RESOURCE(resources);
    QApplication::setStyle(QStringLiteral("Fusion"));
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    const QStringList families = QFontDatabase::families();
    for (const auto& family : {QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Noto Sans CJK SC"),
                               QStringLiteral("Segoe UI"), QStringLiteral("Noto Sans")}) {
        if (families.contains(family)) { font.setFamily(family); break; }
    }
    qApp->setFont(font);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this] { if (m_mode == ThemeMode::System) apply(); });
    apply();
}
bool ThemeManager::resolvesDark(ThemeMode mode, Qt::ColorScheme system)
{
    return mode == ThemeMode::Dark || (mode == ThemeMode::System && system == Qt::ColorScheme::Dark);
}
void ThemeManager::setMode(ThemeMode mode) { m_mode = mode; apply(); }
void ThemeManager::apply()
{
    m_dark = resolvesDark(m_mode, QGuiApplication::styleHints()->colorScheme());
    const QString bg = m_dark ? "#181b20" : "#f3f5f8";
    const QString panel = m_dark ? "#21252b" : "#ffffff";
    const QString text = m_dark ? "#e4e8ef" : "#202734";
    const QString muted = m_dark ? "#9da7b6" : "#657184";
    const QString border = m_dark ? "#343b46" : "#d9dfe8";
    const QString hover = m_dark ? "#303946" : "#e9eef6";
    const QString selected = m_dark ? "#233e5c" : "#dbeafe";
    const QString disabled = m_dark ? "#606a78" : "#929baa";
    const QString accent = m_dark ? "#58a6ff" : "#176ac6";
    QPalette p;
    p.setColor(QPalette::Window, QColor(bg));
    p.setColor(QPalette::WindowText, QColor(text));
    p.setColor(QPalette::Base, QColor(panel));
    p.setColor(QPalette::AlternateBase, QColor(bg));
    p.setColor(QPalette::Text, QColor(text));
    p.setColor(QPalette::Button, QColor(panel));
    p.setColor(QPalette::ButtonText, QColor(text));
    p.setColor(QPalette::Highlight, QColor(selected));
    p.setColor(QPalette::HighlightedText, QColor(text));
    p.setColor(QPalette::ToolTipBase, QColor(panel));
    p.setColor(QPalette::ToolTipText, QColor(text));
    p.setColor(QPalette::Link, QColor(accent));
    p.setColor(QPalette::PlaceholderText, QColor(muted));
    p.setColor(QPalette::Light, QColor(hover));
    p.setColor(QPalette::Mid, QColor(border));
    p.setColor(QPalette::Dark, QColor(border));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(disabled));
    qApp->setPalette(p);
    QFile file(QStringLiteral(":/styles/common.qss"));
    if (!file.open(QIODevice::ReadOnly)) { qWarning() << file.errorString(); return; }
    QString sheet = QString::fromUtf8(file.readAll());
    const QList<QPair<QString, QString>> tokens = {
        {"@bg", bg}, {"@panel", panel}, {"@text", text}, {"@muted", muted},
        {"@border", border}, {"@hover", hover}, {"@selected", selected},
        {"@disabled", disabled}, {"@accent", accent}};
    for (const auto& token : tokens) sheet.replace(token.first, token.second);
    qApp->setStyleSheet(sheet);
    emit themeChanged();
}
