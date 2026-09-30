#include "ImagePane.h"
#include "ImageView.h"
#include "media/ImageLoader.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QShortcut>
#include <QStackedLayout>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

ImagePane::ImagePane(QWidget* parent) : QWidget(parent), m_loader(new ImageLoader(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_toolbar = new QWidget(this);
    auto* controls = new QHBoxLayout(m_toolbar);
    controls->setContentsMargins(12, 6, 12, 6);
    const auto button = [this, controls](const QString& label, const char* name) {
        auto* result = new QPushButton(label, m_toolbar);
        result->setObjectName(QString::fromLatin1(name));
        controls->addWidget(result);
        return result;
    };
    auto* previous = button(tr("上一张"), "previousImageButton");
    auto* next = button(tr("下一张"), "nextImageButton");
    auto* fit = button(tr("适应窗口"), "fitImageButton");
    auto* actual = button(tr("原始尺寸"), "actualImageButton");
    auto* minus = new QToolButton(m_toolbar);
    minus->setText(QStringLiteral("−"));
    minus->setObjectName("zoomOutButton");
    controls->addWidget(minus);
    minus->setAccessibleName(tr("缩小"));
    auto* plus = new QToolButton(m_toolbar);
    plus->setText(QStringLiteral("+"));
    plus->setObjectName("zoomInButton");
    controls->addWidget(plus);
    plus->setAccessibleName(tr("放大"));
    m_scale = new QLabel(m_toolbar);
    m_scale->setMinimumWidth(m_scale->fontMetrics().horizontalAdvance(QStringLiteral("3200%")) + 8);
    controls->addWidget(m_scale);
    controls->addStretch();
    auto* details = button(tr("详细信息"), "imageDetailsButton");
    layout->addWidget(m_toolbar);
    auto* canvas = new QWidget(this);
    auto* canvasLayout = new QStackedLayout(canvas);
    canvasLayout->setStackingMode(QStackedLayout::StackAll);
    m_view = new ImageView(canvas);
    canvasLayout->addWidget(m_view);
    m_loading = new QLabel(tr("正在加载图片…"), canvas);
    m_loading->setAlignment(Qt::AlignCenter);
    m_loading->setAttribute(Qt::WA_TransparentForMouseEvents);
    canvasLayout->addWidget(m_loading);
    layout->addWidget(canvas, 1);
    connect(fit, &QPushButton::clicked, m_view, &ImageView::fitImage);
    connect(actual, &QPushButton::clicked, m_view, &ImageView::actualSize);
    connect(minus, &QToolButton::clicked, this, [this] { m_view->zoom(1 / 1.2); });
    connect(plus, &QToolButton::clicked, this, [this] { m_view->zoom(1.2); });
    connect(details, &QPushButton::clicked, this, &ImagePane::showDetails);
    connect(previous, &QPushButton::clicked, this, [this] { emit navigationRequested(-1); });
    connect(next, &QPushButton::clicked, this, [this] { emit navigationRequested(1); });
    for (const auto& [key, direction] : {std::pair{Qt::Key_PageUp, -1}, std::pair{Qt::Key_PageDown, 1}}) {
        auto* shortcut = new QShortcut(QKeySequence(key), m_view);
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, direction] { emit navigationRequested(direction); });
    }
    connect(m_view, &ImageView::scaleChanged, this, [this](double scale) {
        m_scale->setText(tr("%1%").arg(qRound(scale * 100)));
    });
    connect(m_loader, &ImageLoader::loaded, this, [this](const ImageResult& result) {
        m_result = result;
        m_metadataPending = result.status == ImageResult::Status::Ready;
        if (result.status != ImageResult::Status::Ready) {
            emit failed(result.message.isEmpty() ? tr("格式不支持") : result.message);
            return;
        }
        m_loading->hide();
        m_view->show();
        m_toolbar->show();
        m_view->setImage(result.image);
        // The view owns its display data; release the additional decoded image.
        m_result.image = {};
    });
    connect(m_loader, &ImageLoader::metadataLoaded, this, [this](const QList<ImageMetadataEntry>& entries) {
        m_result.metadata.append(entries);
        m_metadataPending = false;
        if (m_details) {
            if (auto* table = m_details->findChild<QTableWidget*>("imageMetadataTable")) fillDetails(table);
        }
    });
    clear();
}

void ImagePane::clear()
{
    m_loader->clear();
    resetView();
}

void ImagePane::resetView()
{
    if (m_details) m_details->close();
    m_path.clear();
    m_result = {};
    m_metadataPending = false;
    m_view->clearImage();
    m_toolbar->hide();
    m_loading->show();
    m_loading->raise();
}

void ImagePane::open(const QString& path, bool reload)
{
    resetView();
    m_path = path;
    m_loader->open(path, reload);
}

void ImagePane::prefetch(const QStringList& paths) { m_loader->prefetch(paths); }

void ImagePane::showDetails()
{
    if (m_details) { m_details->raise(); m_details->activateWindow(); return; }
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("imageDetailsDialog");
    dialog->setWindowTitle(tr("图片详细信息"));
    dialog->resize(660, 540);
    m_details = dialog;
    auto* layout = new QVBoxLayout(dialog);
    auto* title = new QLabel(QFileInfo(m_path).fileName(), dialog);
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    layout->addWidget(title);
    auto* table = new QTableWidget(0, 2, dialog);
    table->setObjectName("imageMetadataTable");
    table->setHorizontalHeaderLabels({tr("项目"), tr("值")});
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    fillDetails(table);
    layout->addWidget(table, 1);
    auto* note = new QLabel(tr("仅显示文件中保存的信息；未记录的拍摄参数和地理位置不会补全。"), dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    close->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    connect(close, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(close);
    dialog->show();
    QTimer::singleShot(0, table, &QTableWidget::resizeRowsToContents);
}

void ImagePane::fillDetails(QTableWidget* table)
{
    QList<ImageMetadataEntry> entries{
        {tr("文件路径"), m_path}, {tr("格式"), m_result.format},
        {tr("图片尺寸"), tr("%1 × %2 像素").arg(m_result.originalSize.width()).arg(m_result.originalSize.height())},
        {tr("文件大小"), QLocale().formattedDataSize(QFileInfo(m_path).size())}
    };
    entries.append(m_result.metadata);
    if (m_metadataPending) entries.append({tr("拍摄信息"), tr("正在读取…")});
    table->setRowCount(static_cast<int>(entries.size()));
    for (int row = 0; row < entries.size(); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(entries[row].name));
        auto* item = new QTableWidgetItem(entries[row].value);
        item->setToolTip(entries[row].value);
        table->setItem(row, 1, item);
    }
    table->resizeRowsToContents();
}
