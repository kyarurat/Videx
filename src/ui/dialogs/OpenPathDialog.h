#pragma once
#include <QFileDialog>

class OpenPathDialog : public QFileDialog
{
    Q_OBJECT
public:
    explicit OpenPathDialog(const QString& initialDirectory, QWidget* parent = nullptr);
    QString selectedPath() const { return m_selectedPath; }
public slots:
    void accept() override;
protected:
    void changeEvent(QEvent* event) override;
private:
    void refreshNavigationIcons();
    class QLineEdit* m_pathEdit = nullptr;
    class QLabel* m_hint = nullptr;
    QString m_selectedPath;
};
