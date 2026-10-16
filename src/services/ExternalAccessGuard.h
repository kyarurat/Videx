#pragma once
#include <QObject>
#include <memory>

// Windows-specific details and overlapped I/O lifetimes stay out of consumers.
// Workers poll between bounded reads; GUI consumers use event notification.
class ExternalAccessGuard : public QObject
{
    Q_OBJECT
public:
    enum class BreakReason { None, PathChange, ContentChange, SharingConflict };
    explicit ExternalAccessGuard(QObject* parent = nullptr);
    ~ExternalAccessGuard() override;
    bool arm(const QString& path, bool notify = true);
    BreakReason poll();
    void acknowledge();
    bool supported() const;
signals:
    void broken(ExternalAccessGuard::BreakReason reason);
private:
    struct Native;
    std::unique_ptr<Native> m_native;
};
