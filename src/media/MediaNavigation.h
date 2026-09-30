#pragma once
#include "MediaType.h"
#include <QObject>
#include <QStringList>
#include <functional>
#include <optional>
#include <QCache>
#include <QDateTime>
class MpvPlayer;
class QTimer;

// Resolve only navigation candidates, without playing them or probing the tree.
class MediaNavigation : public QObject
{
    Q_OBJECT
public:
    // nullopt = end; empty string = yield after a bounded batch of tree rows.
    using CandidateSource = std::function<std::optional<QString>()>;
    explicit MediaNavigation(QObject* parent = nullptr);
    void navigate(const QStringList& candidates, MediaType category);
    void navigate(CandidateSource candidates, MediaType category);
    void remember(const QString& path, MediaType category);
    void cancel();
signals:
    void candidateSelected(const QString& path);
    void failed(const QString& message);
private:
    void probeNext();
    void queueNext();
    void selectCandidate();
    MpvPlayer* m_probe;
    QTimer* m_timeout;
    CandidateSource m_candidates;
    struct CategoryEntry { qint64 size; QDateTime modified; MediaType category; };
    QCache<QString, CategoryEntry> m_categories{1024};
    QString m_probePath;
    MediaType m_category = MediaType::Unknown;
    quint64 m_generation = 0;
};
