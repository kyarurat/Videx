#include "PlayerWidget.h"
#include "ui/common/UiComponents.h"
#include "ui/common/ComboBox.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QStackedWidget>
#include <QPushButton>
#include <QSignalBlocker>

PlayerWidget::PlayerWidget(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    auto* header = new QWidget(this);
    header->setObjectName("mediaHeader"); header->setAttribute(Qt::WA_StyledBackground);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18,10,16,10);
    m_title = new QLabel(tr("媒体查看器"), header);
    m_title->setMinimumWidth(0);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_badge = new QLabel(tr("界面演示"), header); m_badge->setProperty("role", "badge");
    m_stage = new ComboBox(header);
    m_stage->setObjectName("previewStage");
    m_stage->setAccessibleName(tr("演示状态"));
    m_stage->addItem(tr("播放中"), int(PlaybackStage::Playing));
    m_stage->addItem(tr("已暂停"), int(PlaybackStage::Paused));
    m_stage->addItem(tr("加载中"), int(PlaybackStage::Loading));
    m_stage->addItem(tr("播放失败"), int(PlaybackStage::Failed));
    m_stage->addItem(tr("播放结束"), int(PlaybackStage::Finished));
    connect(m_stage, &QComboBox::currentIndexChanged, this, [this] {
        emit stageRequested(static_cast<PlaybackStage>(m_stage->currentData().toInt()));
    });
    headerLayout->addWidget(m_title,1); headerLayout->addWidget(m_badge); headerLayout->addWidget(m_stage);
    layout->addWidget(header);
    m_stack = new QStackedWidget(this);
    auto* welcome = new EmptyState(tr("视频与图片，一处浏览"),
        tr("轻量的本地媒体查看器，让浏览与查看更简单。\n打开目录开始浏览，或先体验 Videx 的界面。"), this);
    connect(welcome->addAction(tr("打开目录"),true), &QPushButton::clicked, this, &PlayerWidget::openRequested);
    connect(welcome->addAction(tr("查看界面演示")), &QPushButton::clicked, this, &PlayerWidget::demoRequested);
    auto* canvas = new QWidget(this);
    canvas->setObjectName("canvas"); canvas->setAttribute(Qt::WA_StyledBackground);
    auto* canvasLayout = new QVBoxLayout(canvas);
    auto* demo = new QLabel(tr("界面演示 · 不会播放真实视频"), canvas);
    demo->setProperty("role", "badge");
    canvasLayout->addWidget(demo,0,Qt::AlignLeft);
    m_preview = new EmptyState({}, {}, canvas);
    m_retry = m_preview->addAction(tr("重试演示"));
    connect(m_retry, &QPushButton::clicked, this, [this] { emit stageRequested(PlaybackStage::Playing); });
    canvasLayout->addWidget(m_preview,1);
    auto* hint = new QLabel(tr("Space 播放 / 暂停     ← → 跳转     F 全屏     Esc 退出全屏"),canvas);
    hint->setAlignment(Qt::AlignCenter); hint->setWordWrap(true); hint->setProperty("role", "muted");
    hint->setContentsMargins(10,10,10,18); canvasLayout->addWidget(hint);
    m_stack->addWidget(welcome); m_stack->addWidget(canvas);
    layout->addWidget(m_stack,1);
    setState({});
}
void PlayerWidget::setState(const MediaUiState& state)
{
    m_title->setText(state.demo ? state.title : tr("媒体查看器"));
    m_title->setToolTip(state.title);
    m_badge->setVisible(state.demo); m_stage->setVisible(state.demo);
    m_stack->setCurrentIndex(state.demo ? 1 : 0);
    const QSignalBlocker blocker(m_stage);
    m_stage->setCurrentIndex(m_stage->findData(int(state.stage)));
    m_retry->setVisible(state.stage == PlaybackStage::Failed);
    switch (state.stage) {
    case PlaybackStage::Playing: m_preview->setText(tr("播放中"),tr("%1\n当前为静态界面演示，尚未接入播放引擎。").arg(state.title)); break;
    case PlaybackStage::Paused: m_preview->setText(tr("已暂停"),tr("按空格键，继续演示。")); break;
    case PlaybackStage::Loading: m_preview->setText(tr("正在准备视频…"),tr("这是加载状态预览，可从上方切换其他状态。")); break;
    case PlaybackStage::Failed: m_preview->setText(tr("暂时无法播放"),tr("模拟错误：无法读取媒体文件。\n点击重试，返回播放演示。")); break;
    case PlaybackStage::Finished: m_preview->setText(tr("播放结束"),tr("选择其他视频，或点击播放按钮重新开始演示。")); break;
    case PlaybackStage::Empty: break;
    }
}
