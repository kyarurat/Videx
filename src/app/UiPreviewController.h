#pragma once
#include "UiState.h"
#include <QObject>
#include <memory>
class QTemporaryDir;
class UiPreviewController : public QObject
{
    Q_OBJECT
public:
    explicit UiPreviewController(QObject* parent = nullptr);
    ~UiPreviewController() override;
    const MediaUiState& state() const { return m_state; }
    bool start(double rate);
    void stop();
    void openFile(const QString& path);
    void setStage(PlaybackStage stage);
    void togglePlayback();
    void seek(int seconds);
    void setVolume(int volume);
    void toggleMute();
    void setRate(double rate);
signals:
    void stateChanged(const MediaUiState& state);
    void directoryReady(const QString& path);
    void directoryAboutToClose();
    void failed(const QString& message);
private:
    MediaUiState m_state;
    std::unique_ptr<QTemporaryDir> m_directory;
};
