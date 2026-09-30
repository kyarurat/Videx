#include "ExplorerWidget.h"
#include "ui/common/UiComponents.h"
#include <QFileSystemModel>
#include "media/ImageDecoder.h"

#include <QTreeView>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QDir>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QScopedValueRollback>

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
    auto* open = new IconButton(Glyph::Folder, tr("打开文件或文件夹 (Ctrl+O)"), theme, this);
    connect(open, &QToolButton::clicked, this, &ExplorerWidget::openRequested);
    auto* hide = new IconButton(Glyph::HideSidebar, tr("收起侧栏（Ctrl+B 可恢复）"), theme, this);
    hide->setObjectName("hideExplorerButton");
    connect(hide, &QToolButton::clicked, this, &ExplorerWidget::hideRequested);
    header->addWidget(title); header->addStretch(); header->addWidget(open);
    header->addWidget(hide);
    layout->addLayout(header);
    m_rootLabel = new QLabel(this);
    m_rootLabel->setObjectName("rootDirectoryLabel");
    m_rootLabel->setTextFormat(Qt::PlainText);
    m_rootLabel->setProperty("role", "section");
    m_rootLabel->setWordWrap(true);
    m_rootLabel->setContentsMargins(16,0,16,8);
    m_rootLabel->hide();
    layout->addWidget(m_rootLabel);
    m_stack = new QStackedWidget(this);
    auto* empty = new EmptyState(tr("尚未打开目录"), tr("选择文件或文件夹，\n在这里浏览本地媒体。"), this);
    connect(empty->addAction(tr("打开…")), &QPushButton::clicked, this, &ExplorerWidget::openRequested);
    m_tree = new QTreeView(this);
    m_tree->setObjectName("fileTree");
    m_tree->setAccessibleName(tr("媒体文件树"));
    m_tree->setHeaderHidden(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setAnimated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setTextElideMode(Qt::ElideMiddle);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->installEventFilter(this);
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        if (m_model && index.isValid() && !m_model->isDir(index))
            emit fileOpenRequested(m_model->filePath(index));
    });
    m_stack->addWidget(empty); m_stack->addWidget(m_tree);
    layout->addWidget(m_stack, 1);
    m_hint = new QLabel(this);
    m_hint->hide();
    m_hint->setProperty("role", "muted"); m_hint->setWordWrap(true);
    m_hint->setContentsMargins(16,12,16,12);
    layout->addWidget(m_hint);
}
ExplorerWidget::~ExplorerWidget() { clearDirectory(); }
bool ExplorerWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_tree && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (!m_forwardingNavigation && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)
            && key->modifiers() == Qt::NoModifier) {
            const auto previous = m_tree->currentIndex();
            QScopedValueRollback<bool> forwarding(m_forwardingNavigation, true);
            QCoreApplication::sendEvent(m_tree, event);
            const auto current = m_tree->currentIndex();
            if (m_model && current.isValid() && current != previous && !m_model->isDir(current))
                emit fileOpenRequested(m_model->filePath(current));
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            && (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
            const auto index = m_tree->currentIndex();
            if (m_model && index.isValid() && !key->isAutoRepeat()) {
                if (m_model->isDir(index))
                    m_tree->setExpanded(index, !m_tree->isExpanded(index));
                else
                    emit fileOpenRequested(m_model->filePath(index));
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
void ExplorerWidget::clearDirectory()
{
    auto* selection = m_tree->selectionModel();
    if (selection) disconnect(selection, nullptr, this, nullptr);
    m_tree->setModel(nullptr);
    delete selection;
    delete m_model;
    m_model = nullptr;
    m_directory.clear();
    m_pendingHighlight.clear();
    m_directoryLoaded=false;
    m_rootLabel->clear();
    m_rootLabel->hide();
    m_hint->clear();
    m_hint->hide();
    m_stack->setCurrentIndex(0);
}
void ExplorerWidget::setDirectory(const QString& path)
{
    clearDirectory();
    m_directory = path;
    m_rootLabel->setText(QDir(path).dirName().isEmpty() ? QDir::toNativeSeparators(path) : QDir(path).dirName());
    m_rootLabel->setToolTip(QDir::toNativeSeparators(path));
    m_rootLabel->show();
    m_model = new QFileSystemModel(this);
    m_model->setReadOnly(true);
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    m_tree->setModel(m_model);
    for (int column=1; column<4; ++column) m_tree->hideColumn(column);
    m_tree->header()->setStretchLastSection(true);
    const auto updateHint = [this] {
        const bool empty = m_model->rowCount(m_tree->rootIndex()) == 0;
        m_hint->setText(empty ? tr("文件夹为空") : QString{});
        m_hint->setVisible(empty);
    };
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this, updateHint](const QString& directory) {
        if (QDir::cleanPath(directory) == QDir::cleanPath(m_directory)) {
            m_directoryLoaded=true;
            updateHint();
            if (!m_pendingHighlight.isEmpty()) {
                const QString path=m_pendingHighlight;
                highlightFile(path);
                m_pendingHighlight.clear();
            }
        }
        emit contentsReady();
    });
    connect(m_model, &QAbstractItemModel::rowsInserted, this, updateHint);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, updateHint);
    m_tree->setRootIndex(m_model->setRootPath(path));
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(0, Qt::AscendingOrder);
    m_stack->setCurrentIndex(1);
}

void ExplorerWidget::highlightFile(const QString& path)
{
    if (!m_model || path.isEmpty()) return;
    m_pendingHighlight=m_directoryLoaded ? QString{} : path;
    const auto index=m_model->index(path);
    if (!index.isValid()) return;
    m_tree->setCurrentIndex(index);
    m_tree->scrollTo(index);
}

void ExplorerWidget::navigateImage(const QString& currentPath, int direction)
{
    if (!m_model || currentPath.isEmpty() || direction == 0) return;
    // index(path) can insert the opened file before the asynchronous directory
    // listing arrives. Flush the pending sort before interpreting row order.
    m_model->sort(m_tree->header()->sortIndicatorSection(), m_tree->header()->sortIndicatorOrder());
    const auto current = m_model->index(currentPath);
    if (!current.isValid()) return;
    const auto parent = current.parent();
    const int step = direction > 0 ? 1 : -1;
    for (int row = current.row() + step; row >= 0 && row < m_model->rowCount(parent); row += step) {
        const auto index = m_model->index(row, 0, parent);
        const auto path = m_model->filePath(index);
        if (!m_model->isDir(index) && ImageDecoder::isImageCandidate(path)) {
            emit fileOpenRequested(path);
            return;
        }
    }
}

QStringList ExplorerWidget::adjacentImages(const QString& currentPath)
{
    QStringList paths;
    if (!m_model || currentPath.isEmpty() || !ImageDecoder::isImageCandidate(currentPath)) return paths;
    m_model->sort(m_tree->header()->sortIndicatorSection(), m_tree->header()->sortIndicatorOrder());
    const auto current = m_model->index(currentPath);
    if (!current.isValid()) return paths;
    const auto parent = current.parent();
    const auto extensions = ImageDecoder::supportedExtensions();
    for (const int step : {1, -1}) {
        for (int row = current.row() + step; row >= 0 && row < m_model->rowCount(parent); row += step) {
            const auto index = m_model->index(row, 0, parent);
            if (!m_model->isDir(index) && extensions.contains(m_model->fileInfo(index).suffix().toLower())) {
                paths.append(m_model->filePath(index));
                break;
            }
        }
    }
    return paths;
}
