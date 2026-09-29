#include "ExplorerWidget.h"
#include "ui/common/UiComponents.h"
#include <QFileSystemModel>
#include <QTreeView>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QHeaderView>

ExplorerWidget::ExplorerWidget(ThemeManager* theme, QWidget* parent) : QWidget(parent)
{
    setObjectName("explorer");
    setAttribute(Qt::WA_StyledBackground);
    setMinimumWidth(200);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    auto* header = new QHBoxLayout;
    header->setContentsMargins(16,10,10,10);
    auto* title = new QLabel(tr("资源管理器"), this);
    title->setProperty("role", "section");
    auto* open = new IconButton(Glyph::Folder, tr("打开目录 (Ctrl+O)"), theme, this);
    connect(open, &QToolButton::clicked, this, &ExplorerWidget::openRequested);
    header->addWidget(title); header->addStretch(); header->addWidget(open);
    layout->addLayout(header);
    m_stack = new QStackedWidget(this);
    auto* empty = new EmptyState(tr("尚未打开目录"), tr("打开一个文件夹，\n在这里浏览本地媒体。"), this);
    connect(empty->addAction(tr("打开目录")), &QPushButton::clicked, this, &ExplorerWidget::openRequested);
    m_tree = new QTreeView(this);
    m_tree->setObjectName("fileTree");
    m_tree->setAccessibleName(tr("媒体文件树"));
    m_tree->setHeaderHidden(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setAnimated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setTextElideMode(Qt::ElideMiddle);
    connect(m_tree, &QTreeView::activated, this, [this](const QModelIndex& index) {
        if (m_model && !m_model->isDir(index)) emit fileActivated(m_model->filePath(index));
    });
    m_stack->addWidget(empty); m_stack->addWidget(m_tree);
    layout->addWidget(m_stack, 1);
    auto* hint = new QLabel(tr("双击文件打开 · 拖动边界调整宽度"), this);
    hint->setProperty("role", "muted"); hint->setWordWrap(true);
    hint->setContentsMargins(16,12,16,12);
    layout->addWidget(hint);
}
ExplorerWidget::~ExplorerWidget() { clearDirectory(); }
void ExplorerWidget::clearDirectory()
{
    m_tree->setModel(nullptr);
    delete m_model;
    m_model = nullptr;
    m_currentPath.clear();
    m_stack->setCurrentIndex(0);
}
void ExplorerWidget::setDirectory(const QString& path)
{
    clearDirectory();
    m_model = new QFileSystemModel(this);
    m_model->setReadOnly(true);
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_model->setNameFilters({"*.mp4", "*.mkv", "*.webm", "*.mov", "*.avi", "*.m4v", "*.ts", "*.m2ts"});
    m_model->setNameFilterDisables(false);
    m_tree->setModel(m_model);
    for (int column=1; column<4; ++column) m_tree->hideColumn(column);
    m_tree->header()->setStretchLastSection(true);
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this](const QString& directory) {
        m_tree->expand(m_model->index(directory));
        if (!m_currentPath.isEmpty()) highlightFile(m_currentPath);
    });
    m_tree->setRootIndex(m_model->setRootPath(path));
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(0, Qt::AscendingOrder);
    m_stack->setCurrentIndex(1);
}
void ExplorerWidget::highlightFile(const QString& path)
{
    m_currentPath = path;
    if (!m_model) return;
    const auto index = m_model->index(path);
    if (index.isValid()) {
        m_tree->expand(index.parent());
        m_tree->setCurrentIndex(index);
        m_tree->scrollTo(index);
    }
}
