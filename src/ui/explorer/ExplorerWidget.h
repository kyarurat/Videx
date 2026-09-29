#pragma once
#include <QWidget>
class QFileSystemModel;
class QTreeView;
class QStackedWidget;
class ThemeManager;
class ExplorerWidget : public QWidget
{
    Q_OBJECT
public:
    ExplorerWidget(ThemeManager* theme, QWidget* parent = nullptr);
    ~ExplorerWidget() override;
    void setDirectory(const QString& path);
    void clearDirectory();
    void highlightFile(const QString& path);
signals:
    void openRequested();
    void fileActivated(const QString& path);
private:
    QFileSystemModel* m_model = nullptr;
    QTreeView* m_tree;
    QStackedWidget* m_stack;
    QString m_currentPath;
};
