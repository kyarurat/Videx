#include "FileIdentity.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

FileIdentity FileIdentity::read(const QString& path, QString* actualName)
{
    FileIdentity result;
    result.renameUnambiguous = !QFileInfo(path).isSymLink();
    if (actualName) *actualName = QFileInfo(path).fileName();
#ifdef Q_OS_WIN
    const auto native = QDir::toNativeSeparators(path);
    HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) return result;
    FILE_STANDARD_INFO standard{};
    if (GetFileInformationByHandleEx(file, FileStandardInfo, &standard, sizeof(standard)) && standard.NumberOfLinks > 1)
        result.renameUnambiguous = false;
    if (actualName) {
        const DWORD size = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED);
        if (size > 0) {
            QString normalized(size, Qt::Uninitialized);
            const DWORD length = GetFinalPathNameByHandleW(file,
                reinterpret_cast<LPWSTR>(normalized.data()), size, FILE_NAME_NORMALIZED);
            if (length > 0 && length < size) {
                normalized.truncate(length);
                *actualName = QFileInfo(QDir::fromNativeSeparators(normalized)).fileName();
            }
        }
    }
    FILE_ID_INFO info{};
    if (GetFileInformationByHandleEx(file, FileIdInfo, &info, sizeof(info))) {
        result.value.append(reinterpret_cast<const char*>(&info.VolumeSerialNumber), sizeof(info.VolumeSerialNumber));
        result.value.append(reinterpret_cast<const char*>(&info.FileId), sizeof(info.FileId));
    } else {
        BY_HANDLE_FILE_INFORMATION legacy{};
        if (GetFileInformationByHandle(file, &legacy)) {
            result.value.append(reinterpret_cast<const char*>(&legacy.dwVolumeSerialNumber), sizeof(legacy.dwVolumeSerialNumber));
            result.value.append(reinterpret_cast<const char*>(&legacy.nFileIndexHigh), sizeof(legacy.nFileIndexHigh));
            result.value.append(reinterpret_cast<const char*>(&legacy.nFileIndexLow), sizeof(legacy.nFileIndexLow));
        }
    }
    CloseHandle(file);
#else
    struct stat info{};
    if (::stat(QFile::encodeName(path).constData(), &info) == 0) {
        if (info.st_nlink > 1) result.renameUnambiguous = false;
        result.value.append(reinterpret_cast<const char*>(&info.st_dev), sizeof(info.st_dev));
        result.value.append(reinterpret_cast<const char*>(&info.st_ino), sizeof(info.st_ino));
    }
#endif
    return result;
}
