#pragma once
#include "app/UiState.h"
#include <QDialog>
class QCheckBox;
class QComboBox;
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    void setSettings(const SessionSettings& settings);
    SessionSettings draft() const;
signals:
    void settingsApplied(const SessionSettings& settings);
private:
    void apply();
    QCheckBox* m_restore;
    QCheckBox* m_resume;
    QCheckBox* m_next;
    QCheckBox* m_explorer;
    QCheckBox* m_status;
    QComboBox* m_rate;
    QComboBox* m_theme;
};
