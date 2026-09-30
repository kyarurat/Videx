#include "VideoView.h"
#include "media/MpvPlayer.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>

VideoView::VideoView(MpvPlayer* player, QWidget* parent)
    : QOpenGLWidget(parent), m_player(player)
{
    setFocusPolicy(Qt::StrongFocus);
    connect(player, &MpvPlayer::rendererRequested, this, &VideoView::prepareRenderer);
    connect(player, &MpvPlayer::rendererReleaseRequested, this, &VideoView::releaseRenderer);
    connect(player, &MpvPlayer::renderRequested, this, qOverload<>(&VideoView::update));
}
VideoView::~VideoView()
{
    // QOpenGLWidget destroys its context after our members have been destroyed.
    if (context())
        disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &VideoView::releaseRenderer);
    releaseRenderer();
}

void VideoView::prepareRenderer()
{
    if (!m_player || !context() || !isValid()) { update(); return; }
    makeCurrent();
    m_player->initializeRenderer([](void* context, const char* name) -> void* {
        return reinterpret_cast<void*>(static_cast<QOpenGLContext*>(context)->getProcAddress(name));
    }, context());
    doneCurrent();
    update();
}

void VideoView::initializeGL()
{
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &VideoView::releaseRenderer, Qt::DirectConnection);
    if (m_player) m_player->initializeRenderer([](void* context, const char* name) -> void* {
        return reinterpret_cast<void*>(static_cast<QOpenGLContext*>(context)->getProcAddress(name));
    }, context());
}

void VideoView::releaseRenderer()
{
    if (!m_player) return;
    if (context()) makeCurrent();
    m_player->releaseRenderer();
    if (context()) doneCurrent();
}

void VideoView::paintGL()
{
    auto* functions = context()->functions();
    functions->glClearColor(0, 0, 0, 1);
    functions->glClear(GL_COLOR_BUFFER_BIT);
    if (m_player) m_player->renderVideo(int(defaultFramebufferObject()),
        qRound(width() * devicePixelRatioF()), qRound(height() * devicePixelRatioF()));
}
