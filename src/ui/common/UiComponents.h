#pragma once
#include <QWidget>
#include <QToolButton>
class QLabel;
class QPushButton;
class ThemeManager;
enum class Glyph { Play, Pause, Folder, Volume, Muted, Fullscreen, Settings, Video };
class IconButton : public QToolButton
{
public:
    IconButton(Glyph glyph, const QString& label, ThemeManager* theme, QWidget* parent = nullptr);
    void setGlyph(Glyph glyph);
private:
    void refreshIcon();
    Glyph m_glyph;
};
class EmptyState : public QWidget
{
    Q_OBJECT
public:
    EmptyState(const QString& title, const QString& description, QWidget* parent = nullptr);
    void setText(const QString& title, const QString& description);
    QPushButton* addAction(const QString& text, bool primary = false);
private:
    QLabel* m_title;
    QLabel* m_description;
    class QVBoxLayout* m_layout;
};
QWidget* settingRow(const QString& title, const QString& description, QWidget* control, QWidget* parent);
QString formatTime(int seconds);
