#include "MediaType.h"
#include "ImageDecoder.h"
#include <QFileInfo>
#include <QStringList>

MediaType classifyMedia(const QString& path)
{
    const auto extension = QFileInfo(path).suffix().toLower();
    static const QStringList video{
        "mp4", "mkv", "webm", "mov", "avi", "m4v", "ts", "m2ts", "mpeg", "mpg", "wmv", "flv", "3gp", "ogv", "vob", "mts"};
    static const QStringList audio{
        "mp3", "flac", "wav", "ogg", "opus", "m4a", "aac", "wma", "aiff", "aif", "ape", "mka", "mp2", "ac3"};
    if (video.contains(extension)) return MediaType::Video;
    if (audio.contains(extension)) return MediaType::Audio;
    if (ImageDecoder::isImageCandidate(path)) return MediaType::Image;
    return MediaType::Unknown;
}
