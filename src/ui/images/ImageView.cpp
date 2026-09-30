#include "ImageView.h"
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QWheelEvent>
#include <algorithm>

ImageView::ImageView(QWidget* parent) : QGraphicsView(parent)
{
    setObjectName("imageView");
    setScene(new QGraphicsScene(this));
    setFrameShape(QFrame::NoFrame);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setRenderHint(QPainter::SmoothPixmapTransform);
    m_pixelRatio = devicePixelRatioF();
}

void ImageView::setImage(const QImage& image, QSize sourceSize, bool preserveView)
{
    const auto center = mapToScene(viewport()->rect().center());
    if (!preserveView || !m_item) clearImage();
    auto pixmap = QPixmap::fromImage(image);
    // Treat media dimensions as source pixels, including files named @2x.
    // Screen scaling is handled by the view's actual-size/zoom transforms.
    pixmap.setDevicePixelRatio(1.0);
    if (!m_item) m_item = scene()->addPixmap(pixmap);
    else m_item->setPixmap(pixmap);
    if (!sourceSize.isValid()) sourceSize = image.size();
    m_previewScale = std::min(double(image.width()) / sourceSize.width(), double(image.height()) / sourceSize.height());
    m_item->setTransform(QTransform::fromScale(double(sourceSize.width()) / image.width(),
                                              double(sourceSize.height()) / image.height()));
    m_item->setTransformationMode(Qt::SmoothTransformation);
    scene()->setSceneRect(m_item->sceneBoundingRect());
    if (m_fit) fitImage();
    else centerOn(center);
}

void ImageView::clearImage()
{
    scene()->clear();
    m_item = nullptr;
    resetTransform();
    m_fit = true;
}

void ImageView::fitImage()
{
    m_pixelRatio = devicePixelRatioF();
    m_fit = true;
    if (!m_item) return;
    fitInView(m_item, Qt::KeepAspectRatio);
    // Small images retain their original size by default.
    const double actual = 1.0 / devicePixelRatioF();
    if (transform().m11() > actual) {
        resetTransform();
        scale(actual, actual);
    }
    emit scaleChanged(transform().m11() * devicePixelRatioF());
}

void ImageView::actualSize()
{
    m_pixelRatio = devicePixelRatioF();
    m_fit = false;
    resetTransform();
    scale(1.0 / devicePixelRatioF(), 1.0 / devicePixelRatioF());
    if (m_item) centerOn(m_item);
    emit scaleChanged(1.0);
    if (m_item && m_previewScale < 1.0) emit originalRequested();
}

void ImageView::zoom(double factor)
{
    if (!m_item) return;
    updatePixelRatio();
    m_fit = false;
    const double oldScale = transform().m11();
    const double newScale = std::clamp(oldScale * factor, 0.01 / devicePixelRatioF(), 32.0 / devicePixelRatioF());
    scale(newScale / oldScale, newScale / oldScale);
    emit scaleChanged(newScale * devicePixelRatioF());
    if (m_previewScale < 1.0 && newScale * devicePixelRatioF() > m_previewScale) emit originalRequested();
}

void ImageView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    updatePixelRatio();
    if (m_fit) fitImage();
}

bool ImageView::event(QEvent* event)
{
    const bool handled = QGraphicsView::event(event);
    if (event->type() == QEvent::ScreenChangeInternal
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        || event->type() == QEvent::DevicePixelRatioChange
#endif
    ) updatePixelRatio();
    return handled;
}

void ImageView::updatePixelRatio()
{
    const qreal ratio = devicePixelRatioF();
    if (qFuzzyCompare(ratio, m_pixelRatio)) return;
    const qreal previous = m_pixelRatio;
    m_pixelRatio = ratio;
    if (!m_item) return;
    if (m_fit) {
        fitImage();
    } else {
        const auto center = mapToScene(viewport()->rect().center());
        // Keep physical zoom (including 100%) and the viewed image region.
        scale(previous / ratio, previous / ratio);
        centerOn(center);
        emit scaleChanged(transform().m11() * ratio);
    }
}

void ImageView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        if (event->angleDelta().y() != 0) zoom(event->angleDelta().y() > 0 ? 1.2 : 1 / 1.2);
        event->accept();
    } else QGraphicsView::wheelEvent(event);
}

void ImageView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) { zoom(1.2); event->accept(); }
    else if (event->key() == Qt::Key_Minus) { zoom(1 / 1.2); event->accept(); }
    else if (event->key() == Qt::Key_0) { fitImage(); event->accept(); }
    else if (event->key() == Qt::Key_1) { actualSize(); event->accept(); }
    else QGraphicsView::keyPressEvent(event);
}
