#pragma once
#include <QWidget>
class ThemeManager;
class IconButton;
class QSlider;
class QLabel;
class QComboBox;
class PlayerControls : public QWidget
{
    Q_OBJECT
public:
    PlayerControls(ThemeManager* theme, QWidget* parent = nullptr);
    void setFullscreen(bool fullscreen);
signals:
    void playRequested();
    void seekRequested(int seconds);
    void volumeRequested(int volume);
    void muteRequested();
    void rateRequested(double rate);
    void fullscreenRequested();
private:
    IconButton* m_play;
    IconButton* m_mute;
    IconButton* m_fullscreen;
    QSlider* m_seek;
    QSlider* m_volume;
    QLabel* m_time;
    QComboBox* m_rate;
};
