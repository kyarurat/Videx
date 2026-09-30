#pragma once
#include <QString>

// Classification is a browsing hint; the decoder determines actual streams.
enum class MediaType { Unknown, Image, Video, Audio };
MediaType classifyMedia(const QString& path);
