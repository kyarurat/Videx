#include "AboutDialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCoreApplication>
AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("关于 Videx")); resize(440,320); setMinimumSize(420,300);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(32,28,32,24); layout->setSpacing(16);
    auto* title = new QLabel(QStringLiteral("Videx"),this); title->setProperty("role","heading"); layout->addWidget(title);
    auto* version = new QLabel(tr("版本 %1 · 文件浏览").arg(QCoreApplication::applicationVersion()),this);
    version->setProperty("role","badge"); layout->addWidget(version,0,Qt::AlignLeft);
    auto* description = new QLabel(tr("轻量的本地视频与图片查看器。\n视频与图片，一处浏览。"),this);
    description->setWordWrap(true); layout->addWidget(description);
    auto* technology = new QLabel(tr("使用 C++20 与 Qt 6 Widgets 构建。\n播放引擎计划使用 libmpv，当前尚未接入。"),this);
    technology->setProperty("role","muted"); technology->setWordWrap(true); layout->addWidget(technology);
    layout->addStretch(); auto* close = new QPushButton(tr("关闭"),this); close->setDefault(true);
    connect(close,&QPushButton::clicked,this,&QDialog::accept); layout->addWidget(close,0,Qt::AlignRight);
}
