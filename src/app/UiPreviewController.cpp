#include "UiPreviewController.h"
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>

UiPreviewController::UiPreviewController(QObject* parent) : QObject(parent) {}
UiPreviewController::~UiPreviewController() = default;
bool UiPreviewController::start(double rate)
{
    if (m_state.demo) return true;
    auto directory = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/videx-preview-XXXXXX");
    if (!directory->isValid()) { emit failed(tr("无法创建演示目录，请检查临时目录的写入权限。")); return false; }
    const QString folder = directory->path() + '/' + tr("示例视频");
    if (!QDir().mkpath(folder)) { emit failed(tr("无法创建示例媒体目录。")); return false; }
    const QStringList names = {tr("01 山间日出.mp4"), tr("02 城市漫步.mp4"),
                               tr("03 海边落日.mkv"), tr("04 旅途随拍.mp4")};
    for (const auto& name : names) {
        QFile file(folder + '/' + name);
        if (!file.open(QIODevice::WriteOnly)) { emit failed(tr("无法写入示例文件：%1").arg(file.errorString())); return false; }
    }
    m_directory = std::move(directory);
    m_state.demo = true;
    m_state.rate = rate;
    emit directoryReady(m_directory->path());
    openFile(folder + '/' + names.first());
    return true;
}
void UiPreviewController::stop()
{
    emit directoryAboutToClose(); // Consumers must release QFileSystemModel before removing the fixture.
    m_directory.reset();
    m_state = MediaUiState{};
    emit stateChanged(m_state);
}
void UiPreviewController::openFile(const QString& path)
{
    if (!m_directory || !QFileInfo(path).isFile()) return;
    const QString relative = QDir(m_directory->path()).relativeFilePath(path);
    if (relative.startsWith("../") || QDir::isAbsolutePath(relative)) return;
    m_state.path = path;
    m_state.title = QFileInfo(path).fileName();
    m_state.duration = 24*60+18;
    m_state.position = 3*60+42;
    m_state.stage = PlaybackStage::Playing;
    emit stateChanged(m_state);
}
void UiPreviewController::setStage(PlaybackStage stage)
{
    if (!m_state.demo || stage == PlaybackStage::Empty) return;
    m_state.stage = stage;
    if (stage == PlaybackStage::Finished) m_state.position = m_state.duration;
    else if (m_state.position == m_state.duration) m_state.position = 0;
    emit stateChanged(m_state);
}
void UiPreviewController::togglePlayback()
{
    if (!m_state.canControl()) return;
    setStage(m_state.stage == PlaybackStage::Playing ? PlaybackStage::Paused : PlaybackStage::Playing);
}
void UiPreviewController::seek(int seconds)
{
    if (!m_state.canControl()) return;
    m_state.position = qBound(0, seconds, m_state.duration);
    if (m_state.stage == PlaybackStage::Finished && m_state.position < m_state.duration) m_state.stage = PlaybackStage::Paused;
    emit stateChanged(m_state);
}
void UiPreviewController::setVolume(int volume)
{
    if (!m_state.canControl()) return;
    m_state.volume = qBound(0, volume, 100);
    emit stateChanged(m_state);
}
void UiPreviewController::toggleMute()
{
    if (!m_state.canControl()) return;
    m_state.muted = !m_state.muted;
    emit stateChanged(m_state);
}
void UiPreviewController::setRate(double rate)
{
    if (!m_state.canControl()) return;
    m_state.rate = qBound(0.5, rate, 2.0);
    emit stateChanged(m_state);
}
