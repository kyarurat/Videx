#include "ExternalAccessGuard.h"
#include <QDir>
#include <QDebug>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <QWinEventNotifier>
#endif

struct ExternalAccessGuard::Native
{
#ifdef Q_OS_WIN
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE event = nullptr;
    OVERLAPPED overlapped{};
    REQUEST_OPLOCK_INPUT_BUFFER input{};
    REQUEST_OPLOCK_OUTPUT_BUFFER output{};
    std::unique_ptr<QWinEventNotifier> notifier;
    bool pending = false;
    bool delivered = false;
    ~Native()
    {
        notifier.reset();
        if (pending) {
            CancelIoEx(file, &overlapped);
            // Cancellation completion owns the OVERLAPPED storage until done.
            DWORD transferred = 0;
            GetOverlappedResult(file, &overlapped, &transferred, TRUE);
        }
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (event) CloseHandle(event);
    }
#endif
};

ExternalAccessGuard::ExternalAccessGuard(QObject* parent) : QObject(parent) {}
ExternalAccessGuard::~ExternalAccessGuard() = default;
void ExternalAccessGuard::acknowledge() { m_native.reset(); }
bool ExternalAccessGuard::supported() const { return bool(m_native); }

bool ExternalAccessGuard::arm(const QString& path, bool notify)
{
    acknowledge();
#ifdef Q_OS_WIN
    auto native = std::make_unique<Native>();
    const auto name = QDir::toNativeSeparators(path);
    native->file = CreateFileW(reinterpret_cast<LPCWSTR>(name.utf16()), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, nullptr);
    if (native->file == INVALID_HANDLE_VALUE) return false;
    native->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!native->event) return false;
    native->overlapped.hEvent = native->event;
    native->input = {REQUEST_OPLOCK_CURRENT_VERSION, sizeof(REQUEST_OPLOCK_INPUT_BUFFER),
        OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_HANDLE, REQUEST_OPLOCK_INPUT_FLAG_REQUEST};
    const BOOL completed = DeviceIoControl(native->file, FSCTL_REQUEST_OPLOCK,
        &native->input, sizeof(native->input), &native->output, sizeof(native->output),
        nullptr, &native->overlapped);
    if (completed || GetLastError() != ERROR_IO_PENDING) return false;
    native->pending = true;
    if (notify) {
        native->notifier = std::make_unique<QWinEventNotifier>(native->event);
        connect(native->notifier.get(), &QWinEventNotifier::activated, this, [this] {
            const auto reason = poll();
            if (reason != BreakReason::None) emit broken(reason);
        });
    }
    m_native = std::move(native);
    return true;
#else
    Q_UNUSED(path);
    Q_UNUSED(notify);
    return false;
#endif
}

ExternalAccessGuard::BreakReason ExternalAccessGuard::poll()
{
#ifdef Q_OS_WIN
    if (!m_native || m_native->delivered || WaitForSingleObject(m_native->event, 0) != WAIT_OBJECT_0)
        return BreakReason::None;
    m_native->delivered = true;
    if (m_native->notifier) m_native->notifier->setEnabled(false);
    DWORD transferred = 0;
    const bool completed = GetOverlappedResult(m_native->file, &m_native->overlapped, &transferred, FALSE);
    m_native->pending = false;
    if (!completed) return BreakReason::PathChange;
    const auto& result = m_native->output;
    if (qEnvironmentVariableIsSet("VIDEX_TRACE_FILE_ACCESS"))
        qInfo() << "File access break:" << result.OriginalOplockLevel << result.NewOplockLevel
                << result.Flags << result.AccessMode << result.ShareMode;
    if ((result.Flags & REQUEST_OPLOCK_OUTPUT_FLAG_MODES_PROVIDED)
        && (result.Flags & REQUEST_OPLOCK_OUTPUT_FLAG_ACK_REQUIRED)
        && !(result.ShareMode & FILE_SHARE_READ)) return BreakReason::SharingConflict;
    return result.NewOplockLevel == 0 ? BreakReason::ContentChange : BreakReason::PathChange;
#else
    return BreakReason::None;
#endif
}
