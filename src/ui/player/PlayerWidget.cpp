#include "PlayerWidget.h"
#include "PlayerControls.h"
#include "ui/images/ImagePane.h"
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
    m_name->setProperty("role", "muted");
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
    auto* hint = new QLabel(tr("格式不支持"), details);
    m_formatHint = hint;
    hint->setObjectName("unsupportedFormatMessage");
    hint->setProperty("role", "heading");
    hint->setWordWrap(true);
    detailsLayout->insertWidget(1, hint);
    detailsLayout->addStretch();
    m_stack->addWidget(details);
    m_imagePane = new ImagePane(this);
    m_stack->addWidget(m_imagePane);
    connect(m_imagePane, &ImagePane::failed, this, [this](const QString& message) {
        m_formatHint->setText(message);
        m_stack->setCurrentIndex(1);
    });
    connect(m_imagePane, &ImagePane::navigationRequested, this, &PlayerWidget::imageNavigationRequested);
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
    m_currentFile = details;
    setPlaybackMedia(PlaybackMedia::None);
    setNotice({});
    const bool selected = !details.path.isEmpty();
    m_title->setText(selected ? details.name : tr("媒体查看器"));
    m_title->setToolTip(selected ? details.name : QString{});
    if (!selected) {
        m_stack->setCurrentIndex(0);
        m_imagePane->clear();
        m_name->clear();
        m_path->clear();
        m_empty->setText(m_directory.isEmpty() ? tr("本地媒体，一处浏览") : tr("双击打开文件"),
            m_directory.isEmpty() ? tr("选择本地文件直接查看，或选择文件夹浏览其中的内容。")
                                  : tr("双击左侧文件树中的文件，在这里查看名称、位置和基本信息。"));
        return;
    }
    refreshFileDetails(details);
    m_stack->setCurrentWidget(m_imagePane);
    m_imagePane->open(details.path);
}

void PlayerWidget::refreshFileDetails(const FileDetails& details)
{
    const bool changed = details.path == m_currentFile.path
        && (details.size != m_currentFile.size || details.modified != m_currentFile.modified);
    m_currentFile = details;
    m_name->setText(details.name);
    m_path->setText(QDir::toNativeSeparators(details.path));
    m_size->setText(tr("%1（%2 字节）").arg(QLocale().formattedDataSize(details.size), QLocale().toString(details.size)));
    m_type->setText(details.suffix.isEmpty() ? tr("无扩展名") : details.suffix.toUpper());
    m_modified->setText(QLocale().toString(details.modified, QLocale::ShortFormat));
    if (changed && !details.path.isEmpty()) {
        m_stack->setCurrentWidget(m_imagePane);
        m_imagePane->open(details.path, true);
    }
}

void PlayerWidget::prefetchImages(const QStringList& paths) { m_imagePane->prefetch(paths); }

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
