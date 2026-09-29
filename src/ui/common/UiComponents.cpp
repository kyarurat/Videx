#include "UiComponents.h"
#include "app/ThemeManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFrame>
#include <QApplication>

IconButton::IconButton(Glyph glyph, const QString& label, ThemeManager* theme, QWidget* parent)
    : QToolButton(parent), m_glyph(glyph)
{
    setToolTip(label);
    setAccessibleName(label);
    setIconSize(QSize(20, 20));
    setFixedSize(34, 34);
    connect(theme, &ThemeManager::themeChanged, this, [this] { refreshIcon(); });
    refreshIcon();
}
void IconButton::setGlyph(Glyph glyph) { m_glyph = glyph; refreshIcon(); }
void IconButton::refreshIcon()
{
    QIcon icon;
    for (auto mode : {QIcon::Normal, QIcon::Disabled}) {
        QPixmap pixmap(40, 40);
        pixmap.setDevicePixelRatio(2);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor color = qApp->palette().color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active,
                                                   QPalette::ButtonText);
        p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        switch (m_glyph) {
        case Glyph::Play:
            p.setBrush(color); p.drawPolygon(QPolygonF{QPointF(6,4), QPointF(16,10), QPointF(6,16)}); break;
        case Glyph::Pause:
            p.drawLine(7,4,7,16); p.drawLine(13,4,13,16); break;
        case Glyph::Folder:
            p.drawPolygon(QPolygonF{QPointF(2,5),QPointF(8,5),QPointF(10,7),QPointF(18,7),QPointF(18,16),QPointF(2,16)}); break;
        case Glyph::Volume:
        case Glyph::Muted:
            p.drawPolygon(QPolygonF{QPointF(3,8),QPointF(6,8),QPointF(10,4),QPointF(10,16),QPointF(6,12),QPointF(3,12)});
            if (m_glyph == Glyph::Muted) { p.drawLine(14,7,18,13); p.drawLine(18,7,14,13); }
            else { p.drawArc(QRectF(8,4,10,12), -60*16, 120*16); }
            break;
        case Glyph::Fullscreen:
            p.drawPolyline(QPolygonF{QPointF(3,8),QPointF(3,3),QPointF(8,3)});
            p.drawPolyline(QPolygonF{QPointF(12,3),QPointF(17,3),QPointF(17,8)});
            p.drawPolyline(QPolygonF{QPointF(3,12),QPointF(3,17),QPointF(8,17)});
            p.drawPolyline(QPolygonF{QPointF(12,17),QPointF(17,17),QPointF(17,12)}); break;
        case Glyph::Settings:
            for (int y : {5,10,15}) p.drawLine(3,y,17,y);
            p.setBrush(qApp->palette().brush(QPalette::Button));
            p.drawEllipse(QPointF(7,5),2,2); p.drawEllipse(QPointF(13,10),2,2); p.drawEllipse(QPointF(8,15),2,2); break;
        case Glyph::Video:
            p.drawRoundedRect(QRectF(2,4,16,12),2,2); p.drawLine(7,4,7,16); break;
        }
        p.end(); icon.addPixmap(pixmap, mode);
    }
    setIcon(icon);
}
EmptyState::EmptyState(const QString& title, const QString& description, QWidget* parent) : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(20,24,20,24);
    m_layout->setSpacing(16);
    m_title = new QLabel(title, this);
    m_title->setProperty("role", "heading");
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setWordWrap(true);
    m_description = new QLabel(description, this);
    m_description->setProperty("role", "muted");
    m_description->setAlignment(Qt::AlignCenter);
    m_description->setWordWrap(true);
    m_layout->addStretch();
    m_layout->addWidget(m_title);
    m_layout->addWidget(m_description);
    m_layout->addStretch();
}
void EmptyState::setText(const QString& title, const QString& description)
{ m_title->setText(title); m_description->setText(description); }
QPushButton* EmptyState::addAction(const QString& text, bool primary)
{
    auto* button = new QPushButton(text, this);
    if (primary) button->setProperty("role", "primary");
    m_layout->insertWidget(m_layout->count()-1, button, 0, Qt::AlignHCenter);
    return button;
}
QWidget* settingRow(const QString& title, const QString& description, QWidget* control, QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName("settingRow");
    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(0,16,0,16);
    layout->setSpacing(20);
    auto* labels = new QVBoxLayout;
    auto* heading = new QLabel(title, frame);
    heading->setWordWrap(true);
    auto* subtitle = new QLabel(description, frame);
    subtitle->setProperty("role", "muted");
    subtitle->setWordWrap(true);
    labels->addWidget(heading); labels->addWidget(subtitle);
    layout->addLayout(labels, 1); layout->addWidget(control);
    control->setAccessibleName(title);
    return frame;
}
QString formatTime(int seconds)
{
    seconds = qMax(0, seconds);
    if (seconds >= 3600)
        return QStringLiteral("%1:%2:%3").arg(seconds/3600).arg(seconds/60%60,2,10,QChar('0')).arg(seconds%60,2,10,QChar('0'));
    return QStringLiteral("%1:%2").arg(seconds/60,2,10,QChar('0')).arg(seconds%60,2,10,QChar('0'));
}
