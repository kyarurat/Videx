#pragma once
#include <QGraphicsView>
#include <QImage>
class QGraphicsPixmapItem;

class ImageView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);
    void setImage(const QImage& image, QSize sourceSize = {}, bool preserveView = false);
    void clearImage();
    void fitImage();
    void actualSize();
    void zoom(double factor);
signals:
    void scaleChanged(double scale);
    void originalRequested();
protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
private:
    void updatePixelRatio();
    QGraphicsPixmapItem* m_item = nullptr;
    bool m_fit = true;
    qreal m_pixelRatio = 1.0;
    double m_previewScale = 1.0;
};
