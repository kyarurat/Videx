#include "SettingsDialog.h"
#include "ui/common/UiComponents.h"
#include "ui/common/ComboBox.h"
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("设置 — Videx"));
    setObjectName("settingsDialog");
    resize(760,520); setMinimumSize(700,490);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(24,24,24,18); layout->setSpacing(18);
    auto* title = new QLabel(tr("设置"),this); title->setProperty("role","heading");
    layout->addWidget(title);
    auto* body = new QHBoxLayout; body->setSpacing(24);
    auto* navigation = new QListWidget(this); navigation->setObjectName("settingsNavigation");
    navigation->setAccessibleName(tr("设置分类"));
    navigation->addItems({tr("常规"),tr("播放"),tr("界面")}); navigation->setFixedWidth(140);
    auto* pages = new QStackedWidget(this);
    body->addWidget(navigation); body->addWidget(pages,1); layout->addLayout(body,1);
    const auto makePage = [pages](const QString& heading) {
        auto* page = new QWidget(pages);
        auto* rows = new QVBoxLayout(page); rows->setContentsMargins(0,0,0,0); rows->setSpacing(0);
        auto* label = new QLabel(heading,page); label->setProperty("role","section"); rows->addWidget(label);
        pages->addWidget(page); return rows;
    };
    const auto future = tr("界面预览，功能待接入");
    auto* general = makePage(tr("常规偏好"));
    m_restore = new QCheckBox(this);
    general->addWidget(settingRow(tr("恢复上次目录"),future,m_restore,this));
    auto* language = new ComboBox(this); language->addItem(tr("简体中文"));
    general->addWidget(settingRow(tr("语言"),tr("已预留多语言扩展接口。"),language,this));
    general->addStretch();
    auto* playback = makePage(tr("播放偏好"));
    m_resume = new QCheckBox(this); m_next = new QCheckBox(this);
    playback->addWidget(settingRow(tr("记住播放进度"),future,m_resume,this));
    playback->addWidget(settingRow(tr("自动播放下一集"),future,m_next,this));
    m_rate = new ComboBox(this);
    for (double rate : {0.5,0.75,1.0,1.25,1.5,1.75,2.0}) m_rate->addItem(tr("%1×").arg(rate),rate);
    playback->addWidget(settingRow(tr("默认倍速"),tr("下次进入界面演示时使用。"),m_rate,this));
    playback->addStretch();
    auto* appearance = makePage(tr("外观与布局"));
    m_theme = new ComboBox(this); m_theme->setObjectName("themeMode");
    m_theme->addItem(tr("自动（跟随系统）"),int(ThemeMode::System));
    m_theme->addItem(tr("深色"),int(ThemeMode::Dark)); m_theme->addItem(tr("浅色"),int(ThemeMode::Light));
    appearance->addWidget(settingRow(tr("主题"),tr("自动模式随系统切换；应用后立即生效。"),m_theme,this));
    m_explorer = new QCheckBox(this); m_status = new QCheckBox(this);
    appearance->addWidget(settingRow(tr("资源管理器"),tr("显示左侧文件浏览区域。"),m_explorer,this));
    appearance->addWidget(settingRow(tr("状态栏"),tr("显示窗口底部的状态信息。"),m_status,this));
    appearance->addStretch();
    connect(navigation,&QListWidget::currentRowChanged,pages,&QStackedWidget::setCurrentIndex);
    navigation->setCurrentRow(0);
    auto* note = new QLabel(tr("GUI 预览版 · 设置仅在本次会话中保留"),this); note->setProperty("role","muted"); layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(this);
    auto* defaults = buttons->addButton(tr("恢复默认"),QDialogButtonBox::ResetRole); defaults->setObjectName("restoreDefaults");
    auto* cancel = buttons->addButton(tr("取消"),QDialogButtonBox::RejectRole); cancel->setObjectName("cancelSettings");
    auto* applyButton = buttons->addButton(tr("应用"),QDialogButtonBox::ApplyRole); applyButton->setObjectName("applySettings");
    auto* ok = buttons->addButton(tr("确定"),QDialogButtonBox::AcceptRole); ok->setProperty("role","primary"); ok->setDefault(true);
    connect(defaults,&QPushButton::clicked,this,[this] { setSettings({}); });
    connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
    connect(applyButton,&QPushButton::clicked,this,&SettingsDialog::apply);
    connect(ok,&QPushButton::clicked,this,[this] { apply(); accept(); });
    layout->addWidget(buttons);
    setSettings({});
}
void SettingsDialog::setSettings(const SessionSettings& settings)
{
    m_restore->setChecked(settings.restoreDirectory); m_resume->setChecked(settings.resumePlayback);
    m_next->setChecked(settings.autoPlayNext); m_explorer->setChecked(settings.explorerVisible);
    m_status->setChecked(settings.statusBarVisible); m_rate->setCurrentIndex(m_rate->findData(settings.defaultRate));
    m_theme->setCurrentIndex(m_theme->findData(int(settings.theme)));
}
SessionSettings SettingsDialog::draft() const
{
    SessionSettings settings;
    settings.restoreDirectory=m_restore->isChecked(); settings.resumePlayback=m_resume->isChecked();
    settings.autoPlayNext=m_next->isChecked(); settings.explorerVisible=m_explorer->isChecked();
    settings.statusBarVisible=m_status->isChecked(); settings.defaultRate=m_rate->currentData().toDouble();
    settings.theme=static_cast<ThemeMode>(m_theme->currentData().toInt());
    return settings;
}
void SettingsDialog::apply() { emit settingsApplied(draft()); }
