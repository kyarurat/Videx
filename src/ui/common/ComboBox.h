#pragma once
#include <QComboBox>

// Shared popup presentation; navigation and selection remain native QComboBox behavior.
class ComboBox : public QComboBox
{
public:
    explicit ComboBox(QWidget* parent = nullptr);
};

// Apply the same chevron to Qt-owned combo boxes without replacing their models.
void styleComboBoxArrow(QComboBox* combo);
