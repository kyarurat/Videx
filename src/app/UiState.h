#pragma once
#include <QString>
enum class ThemeMode { System, Dark, Light };
enum class PlaybackStage { Empty, Playing, Paused, Loading, Failed, Finished };
struct SessionSettings
{
    bool restoreDirectory = true;
    bool resumePlayback = true;
    bool autoPlayNext = false;
    bool explorerVisible = true;
    bool statusBarVisible = true;
    double defaultRate = 1.0;
    ThemeMode theme = ThemeMode::System;
};
struct MediaUiState
{
    bool demo = false;
    QString path;
    QString title;
    PlaybackStage stage = PlaybackStage::Empty;
    int position = 0;
    int duration = 0;
    int volume = 65;
    bool muted = false;
    double rate = 1.0;
    bool canControl() const
    {
        return demo && (stage == PlaybackStage::Playing || stage == PlaybackStage::Paused
                       || stage == PlaybackStage::Finished);
    }
};
