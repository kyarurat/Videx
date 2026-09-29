#include "PlayerControls.h"
#include "ui/common/UiComponents.h"
#include "ui/common/ComboBox.h"
#include <QHBoxLayout>
#include <QSlider>
#include <QLabel>
#include <QComboBox>

PlayerControls::PlayerControls(ThemeManager* theme, QWidget* parent) : QWidget(parent)
{
    setObjectName("controls"); setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16,12,16,12); layout->setSpacing(14);
    m_play = new IconButton(Glyph::Play,tr("播放 / 暂停（尚未接入）"),theme,this);
    m_play->setObjectName("playButton");
    m_seek = new QSlider(Qt::Horizontal,this); m_seek->setAccessibleName(tr("播放进度")); m_seek->setObjectName("seekSlider");
    m_time = new QLabel(QStringLiteral("00:00 / 00:00"),this); m_time->setMinimumWidth(124); m_time->setAlignment(Qt::AlignCenter);
    m_rate = new ComboBox(this); m_rate->setAccessibleName(tr("播放倍速"));
    for (double rate : {0.5,0.75,1.0,1.25,1.5,1.75,2.0}) m_rate->addItem(tr("%1×").arg(rate),rate);
    m_mute = new IconButton(Glyph::Volume,tr("静音（尚未接入）"),theme,this);
    m_mute->setCheckable(true);
    m_volume = new QSlider(Qt::Horizontal,this); m_volume->setRange(0,100); m_volume->setFixedWidth(88); m_volume->setAccessibleName(tr("音量"));
    m_fullscreen = new IconButton(Glyph::Fullscreen,tr("全屏 (F)"),theme,this); m_fullscreen->setCheckable(true);
    layout->addWidget(m_play); layout->addWidget(m_seek,1); layout->addWidget(m_time);
    layout->addWidget(m_rate); layout->addWidget(m_mute); layout->addWidget(m_volume); layout->addWidget(m_fullscreen);
    connect(m_play,&QToolButton::clicked,this,&PlayerControls::playRequested);
    connect(m_seek,&QSlider::valueChanged,this,&PlayerControls::seekRequested);
    connect(m_volume,&QSlider::valueChanged,this,&PlayerControls::volumeRequested);
    connect(m_mute,&QToolButton::clicked,this,&PlayerControls::muteRequested);
    connect(m_fullscreen,&QToolButton::clicked,this,&PlayerControls::fullscreenRequested);
    connect(m_rate,&QComboBox::currentIndexChanged,this,[this] { emit rateRequested(m_rate->currentData().toDouble()); });
    m_seek->setRange(0,0);
    m_volume->setValue(0);
    m_rate->setCurrentIndex(m_rate->findData(1.0));
    for (QWidget* widget : QList<QWidget*>{m_play,m_seek,m_mute,m_volume,m_rate}) {
        widget->setEnabled(false);
        widget->setToolTip(tr("视频播放尚未接入"));
    }
}
void PlayerControls::setFullscreen(bool fullscreen)
{
    m_fullscreen->setChecked(fullscreen);
    m_fullscreen->setToolTip(fullscreen ? tr("退出全屏 (Esc)") : tr("全屏 (F)"));
}
