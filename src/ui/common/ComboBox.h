#pragma once
#include <QComboBox>

// Shared popup presentation; navigation and selection remain native QComboBox behavior.
class ComboBox : public QComboBox
{
public:
    explicit ComboBox(QWidget* parent = nullptr);
    void showPopup() override;
    void hidePopup() override;
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    bool m_popupVisible = false;
};
