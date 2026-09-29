#include "OpenPathDialog.h"
#include "ui/common/ComboBox.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>

OpenPathDialog::OpenPathDialog(const QString& initialDirectory, QWidget* parent)
    : QFileDialog(parent, tr("打开文件或文件夹"), initialDirectory)
{
    // Native pickers do not consistently allow both choices in one dialog.
    setOption(QFileDialog::DontUseNativeDialog);
    setOption(QFileDialog::ReadOnly);
    setFileMode(QFileDialog::ExistingFile);
    setAcceptMode(QFileDialog::AcceptOpen);
    setNameFilter(tr("所有文件 (*)"));
    setLabelText(QFileDialog::Reject, tr("取消"));
    setLabelText(QFileDialog::LookIn, tr("位置"));
    setLabelText(QFileDialog::FileName, tr("文件或文件夹"));
    resize(820, 560);
    styleComboBoxArrow(findChild<QComboBox*>("lookInCombo"));

    // Keep Qt's navigation and lazy filesystem model, but replace its file-only
    // accept action so selecting a directory can also finish the dialog.
    if (auto* button = findChild<QToolButton*>("newFolderButton")) button->hide();
    if (auto* filter = findChild<QComboBox*>("fileTypeCombo")) filter->hide();
    if (auto* label = findChild<QLabel*>("fileTypeLabel")) label->hide();
    m_pathEdit = findChild<QLineEdit*>("fileNameEdit");
    auto* buttons = findChild<QDialogButtonBox*>("buttonBox");
    if (buttons) {
        if (auto* original = buttons->button(QDialogButtonBox::Open)) {
            buttons->removeButton(original);
            original->hide();
            original->setDefault(false);
            original->setAutoDefault(false);
        }
        auto* open = new QPushButton(tr("打开"), buttons);
        open->setObjectName("openPathButton");
        buttons->addButton(open, QDialogButtonBox::ActionRole);
        open->setDefault(true);
        connect(open, &QPushButton::clicked, this, &OpenPathDialog::accept);
    }
    m_hint = new QLabel(tr("选择文件或文件夹后点击“打开”；双击文件夹进入浏览。留空则打开当前文件夹。"), this);
    m_hint->setProperty("role", "muted");
    m_hint->setWordWrap(true);
    if (auto* grid = qobject_cast<QGridLayout*>(layout()))
        grid->addWidget(m_hint, grid->rowCount(), 0, 1, grid->columnCount());
    else layout()->addWidget(m_hint);

    connect(this, &QFileDialog::currentChanged, this, [this](const QString& path) {
        if (m_pathEdit) m_pathEdit->setText(QFileInfo(path).fileName());
    });
    connect(this, &QFileDialog::directoryEntered, this, [this] {
        if (m_pathEdit) m_pathEdit->clear();
    });
    refreshNavigationIcons();
}

void OpenPathDialog::accept()
{
    const QString entry = m_pathEdit ? m_pathEdit->text() : QString{};
    const QString path = entry.isEmpty() ? directory().absolutePath()
                                        : directory().absoluteFilePath(QDir::fromNativeSeparators(entry));
    const QFileInfo info(path);
    if (!info.exists() || (!info.isFile() && !info.isDir())) {
        m_hint->setText(tr("文件或文件夹不存在，请重新选择或检查输入的路径。"));
        if (m_pathEdit) m_pathEdit->setFocus();
        return;
    }
    m_selectedPath = QDir::cleanPath(info.absoluteFilePath());
    QDialog::accept();
}

void OpenPathDialog::changeEvent(QEvent* event)
{
    QFileDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) refreshNavigationIcons();
}

void OpenPathDialog::refreshNavigationIcons()
{
    const char* names[] = {"backButton", "forwardButton", "toParentButton"};
    const QString labels[] = {tr("后退"), tr("前进"), tr("上级文件夹")};
    for (int direction = 0; direction < 3; ++direction) {
        auto* button = findChild<QToolButton*>(names[direction]);
        if (!button) continue;
        QIcon icon;
        for (auto mode : {QIcon::Normal, QIcon::Disabled}) {
            QPixmap pixmap(40, 40);
            pixmap.setDevicePixelRatio(2);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(palette().color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active,
                                               QPalette::ButtonText), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.translate(10, 10);
            painter.rotate(direction == 0 ? 0 : direction == 1 ? 180 : 90);
            painter.drawLine(QPointF(-5, 0), QPointF(5, 0));
            painter.drawPolyline(QPolygonF{QPointF(0, -5), QPointF(-5, 0), QPointF(0, 5)});
            painter.end();
            icon.addPixmap(pixmap, mode);
        }
        button->setIcon(icon);
        button->setIconSize(QSize(20, 20));
        button->setToolTip(labels[direction]);
        button->setAccessibleName(labels[direction]);
    }
}
