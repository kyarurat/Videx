#pragma once
#include "media/ImageDecoder.h"
#include <QPointer>
#include <QWidget>
class ImageLoader;
class ImageView;
class QLabel;
class QDialog;
class QTableWidget;

class ImagePane : public QWidget
{
    Q_OBJECT
public:
    explicit ImagePane(QWidget* parent = nullptr);
    void open(const QString& path, bool reload = false);
    void prefetch(const QStringList& paths);
    void clear();
signals:
    void failed(const QString& message);
    void navigationRequested(int direction);
private:
    void resetView();
    void showDetails();
    void fillDetails(QTableWidget* table);
    ImageLoader* m_loader;
    ImageView* m_view;
    QWidget* m_toolbar;
    QLabel* m_loading;
    QLabel* m_scale;
    ImageResult m_result;
    QString m_path;
    QPointer<QDialog> m_details;
    bool m_metadataPending = false;
};
