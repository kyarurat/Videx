#include "AboutDialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCoreApplication>
AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("关于 Videx")); resize(480,380); setMinimumSize(440,360);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(32,28,32,24); layout->setSpacing(16);
    auto* title = new QLabel(QStringLiteral("Videx"),this); title->setProperty("role","heading"); layout->addWidget(title);
    auto* version = new QLabel(tr("版本 %1").arg(QCoreApplication::applicationVersion()),this);
    version->setProperty("role","badge"); layout->addWidget(version,0,Qt::AlignLeft);
    auto* author = new QLabel(tr("作者：%1").arg(QStringLiteral("KyaruRat")),this);
    layout->addWidget(author);
    auto* repository = new QLabel(QStringLiteral("<a href=\"https://github.com/kyarurat/Videx\">GitHub · kyarurat/Videx</a>"),this);
    repository->setTextFormat(Qt::RichText);
    repository->setTextInteractionFlags(Qt::TextBrowserInteraction);
    repository->setOpenExternalLinks(true);
    layout->addWidget(repository);
    auto* description = new QLabel(tr("轻量的本地媒体查看器。\n浏览图片，查看相机 RAW 与拍摄信息。"),this);
    description->setWordWrap(true); layout->addWidget(description);
    auto* technology = new QLabel(tr("使用 C++20 与 Qt 6 Widgets 构建。\n播放引擎计划使用 libmpv，当前尚未接入。"),this);
    technology->setProperty("role","muted"); technology->setWordWrap(true); layout->addWidget(technology);
    layout->addStretch(); auto* close = new QPushButton(tr("关闭"),this); close->setDefault(true);
    connect(close,&QPushButton::clicked,this,&QDialog::accept); layout->addWidget(close,0,Qt::AlignRight);
}
