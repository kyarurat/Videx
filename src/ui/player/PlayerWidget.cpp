#include "PlayerWidget.h"
#include "PlayerControls.h"
#include "ui/common/UiComponents.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QPushButton>
#include <QDir>
#include <QLocale>

PlayerWidget::PlayerWidget(ThemeManager* theme, QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(0);
    auto* header = new QWidget(this);
    header->setObjectName("mediaHeader");
    header->setAttribute(Qt::WA_StyledBackground);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18,10,16,10);
    header->setMinimumHeight(54);
    m_showExplorer = new QPushButton(tr("展开文件树"), header);
    m_showExplorer->setObjectName("showExplorerButton");
    m_showExplorer->setToolTip(tr("恢复左侧文件树 (Ctrl+B)"));
    m_showExplorer->hide();
    connect(m_showExplorer, &QPushButton::clicked, this, &PlayerWidget::showExplorerRequested);
    headerLayout->addWidget(m_showExplorer);
    m_title = new QLabel(tr("媒体查看器"), header);
    m_title->setTextFormat(Qt::PlainText);
    m_title->setMinimumWidth(0);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerLayout->addWidget(m_title,1);
    layout->addWidget(header);
    m_notice = new QLabel(this);
    m_notice->setObjectName("browserNotice");
    m_notice->setTextFormat(Qt::PlainText);
    m_notice->setWordWrap(true);
    m_notice->setProperty("role", "error");
    m_notice->setContentsMargins(20,12,20,12);
    m_notice->hide();
    layout->addWidget(m_notice);
    m_stack = new QStackedWidget(this);
    m_empty = new EmptyState({}, {}, this);
    auto* open = m_empty->addAction(tr("打开文件或文件夹"), true);
    open->setObjectName("welcomeOpenDirectory");
    connect(open, &QPushButton::clicked, this, &PlayerWidget::openRequested);
    m_stack->addWidget(m_empty);

    auto* details = new QWidget(this);
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(32,24,32,24);
    detailsLayout->setSpacing(18);
    detailsLayout->addStretch();
    m_name = new QLabel(details);
    m_name->setObjectName("selectedFileName");
    m_name->setProperty("role", "heading");
    detailsLayout->addWidget(m_name);
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(16);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_path = new QLabel(details);
    m_path->setObjectName("selectedFilePath");
    m_size = new QLabel(details);
    m_type = new QLabel(details);
    m_modified = new QLabel(details);
    for (auto* label : {m_name, m_path, m_size, m_type, m_modified}) {
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        label->setWordWrap(true);
        label->setMinimumWidth(0);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    form->addRow(tr("位置"), m_path);
    form->addRow(tr("大小"), m_size);
    form->addRow(tr("扩展名"), m_type);
    form->addRow(tr("修改时间"), m_modified);
    detailsLayout->addLayout(form);
    auto* hint = new QLabel(tr("当前仅显示文件信息，视频播放与图片查看将在后续版本提供。"), details);
    hint->setProperty("role", "muted");
    hint->setWordWrap(true);
    detailsLayout->addWidget(hint);
    detailsLayout->addStretch();
    m_stack->addWidget(details);
    layout->addWidget(m_stack,1);
    m_controls = new PlayerControls(theme, this);
    layout->addWidget(m_controls);
    connect(m_controls, &PlayerControls::fullscreenRequested, this, &PlayerWidget::fullscreenRequested);
    setDirectory({});
}

void PlayerWidget::setDirectory(const QString& path)
{
    m_directory = path;
    setNotice({});
    setFile({});
}

void PlayerWidget::setFile(const FileDetails& details)
{
    setPlaybackMedia(PlaybackMedia::None);
    setNotice({});
    const bool selected = !details.path.isEmpty();
    m_title->setText(selected ? details.name : tr("媒体查看器"));
    m_title->setToolTip(selected ? details.name : QString{});
    m_stack->setCurrentIndex(selected ? 1 : 0);
    if (!selected) {
        m_name->clear();
        m_path->clear();
        m_empty->setText(m_directory.isEmpty() ? tr("视频与图片，一处浏览") : tr("双击打开文件"),
            m_directory.isEmpty() ? tr("选择本地文件直接查看，或选择文件夹浏览其中的内容。")
                                  : tr("双击左侧文件树中的文件，在这里查看名称、位置和基本信息。"));
        return;
    }
    refreshFileDetails(details);
}

void PlayerWidget::refreshFileDetails(const FileDetails& details)
{
    m_name->setText(details.name);
    m_path->setText(QDir::toNativeSeparators(details.path));
    m_size->setText(tr("%1（%2 字节）").arg(QLocale().formattedDataSize(details.size), QLocale().toString(details.size)));
    m_type->setText(details.suffix.isEmpty() ? tr("无扩展名") : details.suffix.toUpper());
    m_modified->setText(QLocale().toString(details.modified, QLocale::ShortFormat));
}

void PlayerWidget::setNotice(const QString& message)
{
    m_notice->setText(message);
    m_notice->setVisible(!message.isEmpty());
}

void PlayerWidget::setPlaybackMedia(PlaybackMedia media)
{
    m_controls->setVisible(media == PlaybackMedia::Video || media == PlaybackMedia::Audio);
}

void PlayerWidget::setFullscreen(bool fullscreen)
{
    m_controls->setFullscreen(fullscreen);
}

void PlayerWidget::setExplorerVisible(bool visible)
{
    m_showExplorer->setVisible(!visible);
}
