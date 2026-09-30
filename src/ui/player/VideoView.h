#pragma once
#include <QOpenGLWidget>
#include <QPointer>
class MpvPlayer;

// Qt owns the OpenGL context and framebuffer; libmpv stays behind MpvPlayer.
class VideoView : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit VideoView(MpvPlayer* player, QWidget* parent = nullptr);
    ~VideoView() override;
protected:
    void initializeGL() override;
    void paintGL() override;
private:
    void prepareRenderer();
    void releaseRenderer();
    QPointer<MpvPlayer> m_player;
};
