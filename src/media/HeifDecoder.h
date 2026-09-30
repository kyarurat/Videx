#pragma once
#include "ImageDecoder.h"

namespace HeifDecoder {
bool recognizes(const QByteArray& header);
ImageResult decode(const QString& path);
}
