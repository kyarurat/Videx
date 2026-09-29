#include "ComboBox.h"
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>

ComboBox::ComboBox(QWidget* parent) : QComboBox(parent)
{
    auto* list = new QListView(this);
    list->setFrameShape(QFrame::NoFrame);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setMouseTracking(true);
    list->setUniformItemSizes(true);
    list->setItemDelegate(new QStyledItemDelegate(list));
    setView(list);
    setMaxVisibleItems(8);
}

void ComboBox::showPopup()
{
    QComboBox::showPopup();
    m_popupVisible = view()->isVisible();
    update();
}

void ComboBox::hidePopup()
{
    QComboBox::hidePopup();
    m_popupVisible = false;
    update();
}

void ComboBox::paintEvent(QPaintEvent* event)
{
    QComboBox::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto group = isEnabled() ? QPalette::Active : QPalette::Disabled;
    painter.setPen(QPen(palette().color(group, QPalette::ButtonText), 1.5,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const qreal x = layoutDirection() == Qt::RightToLeft ? 16.0 : width() - 16.0;
    const qreal y = height() / 2.0;
    const qreal direction = m_popupVisible ? -1.0 : 1.0;
    QPainterPath arrow;
    arrow.moveTo(x - 4, y - 2 * direction);
    arrow.lineTo(x, y + 2 * direction);
    arrow.lineTo(x + 4, y - 2 * direction);
    painter.drawPath(arrow);
}
