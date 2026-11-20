#pragma once

#include <QWidget>

class EngEdit;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QSpinBox;
class VnaDevice;

// Stimulus and sweep settings.
//
// Every control shows the value READ BACK from the instrument. An entry the
// instrument refuses therefore snaps back to what is actually set, and the
// refusal appears in the log.
class StimulusPanel : public QWidget
{
    Q_OBJECT

public:
    explicit StimulusPanel(VnaDevice* device, QWidget* parent = nullptr);

private slots:
    void refresh();

private:
    void applyScenario(int index);

    VnaDevice* _device = nullptr;
    QFormLayout* _form = nullptr;

    QComboBox* _mode = nullptr;
    EngEdit*   _start = nullptr;
    EngEdit*   _stop = nullptr;
    EngEdit*   _center = nullptr;
    EngEdit*   _span = nullptr;
    QSpinBox*  _points = nullptr;
    EngEdit*   _ifbw = nullptr;
    QDoubleSpinBox* _power = nullptr;
    QDoubleSpinBox* _powerStart = nullptr;
    QDoubleSpinBox* _powerStop = nullptr;
    EngEdit*   _cwFreq = nullptr;
    EngEdit*   _dwell = nullptr;
    QSpinBox*  _averaging = nullptr;
    QComboBox* _scenario = nullptr;
    QLabel*    _sweepTime = nullptr;
};
