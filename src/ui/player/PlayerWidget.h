#pragma once
#include "app/UiState.h"
#include <QWidget>
class QLabel;
class QStackedWidget;
class QComboBox;
class QPushButton;
class EmptyState;
class PlayerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlayerWidget(QWidget* parent = nullptr);
    void setState(const MediaUiState& state);
signals:
    void openRequested();
    void demoRequested();
    void stageRequested(PlaybackStage stage);
private:
    QLabel* m_title;
    QLabel* m_badge;
    QStackedWidget* m_stack;
    EmptyState* m_preview;
    QComboBox* m_stage;
    QPushButton* m_retry;
};
