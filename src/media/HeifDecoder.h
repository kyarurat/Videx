#pragma once
#include "ImageDecoder.h"

namespace HeifDecoder {
bool recognizes(const QByteArray& header);
ImageResult decode(const QString& path, const std::shared_ptr<std::atomic_bool>& cancelled, QSize targetSize = {});
ImageResult decode(const QByteArray& bytes, const std::shared_ptr<std::atomic_bool>& cancelled, QSize targetSize = {});
}
