#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class VnaDevice;

// The instrument itself: identity, health, and the actions that change its
// state rather than this application's - acquisition on the tablet, the
// reference source, state and calibration files, preset.
//
// Calibration cannot be performed over SCPI. It is done on the tablet in
// VNStudio; a calibration saved there can be loaded from here by its path on
// the instrument.
class DevicePanel : public QWidget
{
    Q_OBJECT

public:
    explicit DevicePanel(VnaDevice* device, QWidget* parent = nullptr);

private slots:
    void refresh();

private:
    VnaDevice* _device = nullptr;
    QLabel* _identity = nullptr;
    QLabel* _limits = nullptr;
    QLabel* _health = nullptr;
    QLabel* _temperature = nullptr;
    QPushButton* _deviceRun = nullptr;
    QCheckBox* _externalRef = nullptr;
    QLineEdit* _statePath = nullptr;
    QLineEdit* _calPath = nullptr;
};
