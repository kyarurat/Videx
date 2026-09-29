#include "ComboBox.h"
#include <QEvent>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QStyle>

namespace {
class ComboArrow final : public QWidget
{
public:
    explicit ComboArrow(QComboBox* combo) : QWidget(combo), m_combo(combo)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        combo->installEventFilter(this);
        combo->view()->installEventFilter(this);
        reposition();
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_combo && (event->type() == QEvent::Resize
                                  || event->type() == QEvent::LayoutDirectionChange))
            reposition();
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide
            || event->type() == QEvent::PaletteChange || event->type() == QEvent::EnabledChange)
            update();
        return QWidget::eventFilter(watched, event);
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto group = isEnabled() ? QPalette::Active : QPalette::Disabled;
        painter.setPen(QPen(palette().color(group, QPalette::ButtonText), 1.5,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const qreal x = width() / 2.0;
        const qreal y = height() / 2.0;
        const qreal direction = m_combo->view()->isVisible() ? -1.0 : 1.0;
        QPainterPath arrow;
        arrow.moveTo(x - 4, y - 2 * direction);
        arrow.lineTo(x, y + 2 * direction);
        arrow.lineTo(x + 4, y - 2 * direction);
        painter.drawPath(arrow);
    }
private:
    void reposition()
    {
        setGeometry(m_combo->layoutDirection() == Qt::RightToLeft ? 2 : m_combo->width() - 30,
                    1, 28, m_combo->height() - 2);
        raise();
    }
    QComboBox* m_combo;
};
}

void styleComboBoxArrow(QComboBox* combo)
{
    if (!combo || combo->property("customArrow").toBool()) return;
    combo->setProperty("customArrow", true);
    // Qt-owned dialog controls may already be polished before this property is set.
    // Re-evaluate QSS so the native arrow is hidden before adding our chevron.
    combo->style()->unpolish(combo);
    combo->style()->polish(combo);
    new ComboArrow(combo);
}

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
    styleComboBoxArrow(this);
}
