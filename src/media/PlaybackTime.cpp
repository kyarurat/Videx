#include "PlaybackTime.h"
#include <QtGlobal>
#include <cmath>
#include <limits>

QString formatPlaybackTime(double value)
{
    if (!std::isfinite(value)) return QStringLiteral("--:--");
    const auto seconds = qint64(qBound(0.0, value, double(std::numeric_limits<int>::max())));
    return seconds >= 3600
        ? QStringLiteral("%1:%2:%3").arg(seconds / 3600).arg(seconds / 60 % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
