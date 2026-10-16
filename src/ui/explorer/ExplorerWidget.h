#pragma once
#include <QWidget>
#include <QStringList>
#include "media/MediaType.h"
#include "media/MediaNavigation.h"
#include <QSet>
struct SavedSession;
class QFileSystemModel;
class QTreeView;
class QStackedWidget;
class ThemeManager;
class QLabel;
class QTimer;
class ExplorerWidget : public QWidget
{
    Q_OBJECT
public:
    ExplorerWidget(ThemeManager* theme, QWidget* parent = nullptr);
    ~ExplorerWidget() override;
    void captureSession(SavedSession& session) const;
    void restoreSession(const SavedSession& session);
    void setDirectory(const QString& path);
    void clearDirectory();
    void highlightFile(const QString& path);
    MediaNavigation::CandidateSource navigationSource(const QString& currentPath, int direction, MediaType category);
    QStringList adjacentImages(const QString& currentPath);
    QString directory() const { return m_directory; }
    void setPlaybackSeekingEnabled(bool enabled);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
signals:
    void openRequested();
    void hideRequested();
    void fileOpenRequested(const QString& path);
    void contentsReady();
    void playbackSeekRequested(int direction, bool pressed, bool autoRepeat);
    void playbackSeekCancelled();
private:
    void applyRestoredTree(bool settled = false);
    QStringList m_restoreExpanded;
    QString m_restoreSelection;
    int m_restoreScroll = -1;
    QFileSystemModel* m_model = nullptr;
    QTreeView* m_tree;
    QStackedWidget* m_stack;
    QLabel* m_rootLabel;
    QLabel* m_hint;
    QString m_directory;
    QString m_pendingHighlight;
    bool m_directoryLoaded = false;
    bool m_forwardingNavigation = false;
    bool m_playbackSeekingEnabled = false;
    QSet<int> m_playbackSeekKeys;
    QTimer* m_contentsTimer;
};
