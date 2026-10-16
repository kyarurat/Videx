#pragma once
#include <QByteArray>
#include <QString>

// A value only: remembering a file must not keep it open.
struct FileIdentity
{
    QByteArray value;
    bool renameUnambiguous = true;
    bool valid() const { return !value.isEmpty(); }
    bool operator==(const FileIdentity& other) const { return value == other.value; }
    static FileIdentity read(const QString& path, QString* actualName = nullptr);
};
