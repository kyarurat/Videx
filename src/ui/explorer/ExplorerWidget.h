#pragma once
#include <QWidget>
class QFileSystemModel;
class QTreeView;
class QStackedWidget;
class ThemeManager;
class QLabel;
class ExplorerWidget : public QWidget
{
    Q_OBJECT
public:
    ExplorerWidget(ThemeManager* theme, QWidget* parent = nullptr);
    ~ExplorerWidget() override;
    void setDirectory(const QString& path);
    void clearDirectory();
    void highlightFile(const QString& path);
    QString directory() const { return m_directory; }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
signals:
    void openRequested();
    void hideRequested();
    void fileOpenRequested(const QString& path);
private:
    QFileSystemModel* m_model = nullptr;
    QTreeView* m_tree;
    QStackedWidget* m_stack;
    QLabel* m_rootLabel;
    QLabel* m_hint;
    QString m_directory;
    QString m_pendingHighlight;
    bool m_directoryLoaded = false;
};
