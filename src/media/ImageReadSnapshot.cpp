#include "ImageReadSnapshot.h"
#include "services/ExternalAccessGuard.h"
#include "services/FileIdentity.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

bool ImageReadSnapshot::read(const QString& path, const std::shared_ptr<std::atomic_bool>& cancelled)
{
    constexpr qint64 maximumBytes = 512LL * 1024 * 1024;
    constexpr qint64 memoryBytes = 32LL * 1024 * 1024;
    const auto fail = [this](const char* message) {
        error = QCoreApplication::translate("ImageDecoder", message);
        return false;
    };
    if (cancelled->load()) return false;
    const QFileInfo before(path);
    const auto identity = FileIdentity::read(path);
    if (!before.isFile()) return fail("无法读取图片，文件可能已被移动或删除。");
    if (before.size() > maximumBytes) return fail("文件超过 512 MiB 的读取限制。");
    ExternalAccessGuard guard;
    guard.arm(path, false);
    QFile source;
#ifdef Q_OS_WIN
    const auto native = QDir::toNativeSeparators(path);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return fail("无法读取图片。");
    const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY | _O_BINARY);
    if (descriptor < 0) { CloseHandle(handle); return fail("无法读取图片。"); }
    if (!source.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        _close(descriptor);
        return fail("无法读取图片。");
    }
#else
    source.setFileName(path);
    if (!source.open(QIODevice::ReadOnly)) return fail("无法读取图片。");
#endif
    const bool memory = before.size() <= memoryBytes;
    if (memory) m_memory.reserve(before.size());
    else if (!m_temporary.open()) return fail("无法创建图片读取缓存，请检查临时目录空间和权限。");
    while (!source.atEnd()) {
        const auto reason = guard.poll();
        if (cancelled->load() || reason == ExternalAccessGuard::BreakReason::SharingConflict
            || reason == ExternalAccessGuard::BreakReason::ContentChange) {
            interrupted = reason == ExternalAccessGuard::BreakReason::SharingConflict;
            sourceChanged = reason == ExternalAccessGuard::BreakReason::ContentChange;
            source.close(); // Close data before acknowledging a waiting writer.
            guard.acknowledge();
            return fail("图片读取被外部文件操作中断，请重新打开。");
        }
        if (reason == ExternalAccessGuard::BreakReason::PathChange) {
            source.close();
            guard.acknowledge();
            sourceChanged = true;
            return fail("图片路径正在变化，请稍后重试。");
        }
        const auto chunk = source.read(1024 * 1024);
        if (chunk.isEmpty() && source.error() != QFileDevice::NoError) return fail("无法完整读取图片。");
        m_size += chunk.size();
        if (m_size > maximumBytes || (memory && m_size > memoryBytes))
            return fail("图片读取期间发生变化，请重新打开。");
        if (memory) m_memory.append(chunk);
        else if (m_temporary.write(chunk) != chunk.size()) return fail("图片读取缓存写入失败，请检查临时目录空间。");
    }
    source.close();
    const auto reason = guard.poll();
    interrupted = reason == ExternalAccessGuard::BreakReason::SharingConflict;
    sourceChanged = reason == ExternalAccessGuard::BreakReason::PathChange || reason == ExternalAccessGuard::BreakReason::ContentChange;
    guard.acknowledge();
    if (interrupted) return fail("图片读取被外部文件操作中断，请重新打开。");
    if (sourceChanged) return fail("图片读取期间发生变化，请稍后重试。");
    const QFileInfo after(path);
    if (cancelled->load()) return false;
    if (identity != FileIdentity::read(path) || before.size() != after.size()
        || before.lastModified() != after.lastModified()) {
        sourceChanged = true;
        return fail("图片读取期间发生变化，请重新打开。");
    }
    if (!memory && m_size) {
        if (!m_temporary.flush()) return fail("图片读取缓存写入失败。");
        m_mapping = m_temporary.map(0, m_size);
        if (!m_mapping) return fail("无法读取图片缓存。");
    }
    return true;
}

QByteArray ImageReadSnapshot::bytes() const
{
    return m_mapping ? QByteArray::fromRawData(reinterpret_cast<const char*>(m_mapping), m_size) : m_memory;
}
