#include "StimulusPanel.h"

#include "Dsp/Formats.h"
#include "EngEdit.h"
#include "Instrument/VnaDevice.h"
#include "Utils/LogRouter.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

// One-click starting points, after VNStudio's Quick dialog. Power and IF
// bandwidth only; the frequency range is left alone.
struct Scenario
{
    const char* name;
    double power;
    double ifbw;
    int    points;
};
const Scenario kScenarios[] = {
    {"Passive - general",        0,   10e3, 401},
    {"Passive - better dynamic", 0,   1e3,  401},
    {"Passive - high dynamic",   5,   100,  401},
    {"Cable / connector",        0,   10e3, 1001},
    {"Amplifier - small signal", -30, 1e3,  401},
    {"Antenna",                  0,   10e3, 801},
};

void setRowVisible(QFormLayout* form, QWidget* field, bool visible)
{
    form->setRowVisible(field, visible);
}

} // namespace

StimulusPanel::StimulusPanel(VnaDevice* device, QWidget* parent)
    : QWidget(parent)
    , _device(device)
{
    auto* outer = new QVBoxLayout(this);
    _form = new QFormLayout;
    _form->setLabelAlignment(Qt::AlignRight);
    outer->addLayout(_form);
    outer->addStretch(1);

    _mode = new QComboBox;
    for (const SweepMode m : {SweepMode::LinFreq, SweepMode::LogFreq, SweepMode::Power, SweepMode::CW})
        _mode->addItem(sweepModeName(m), int(m));
    _form->addRow(tr("Sweep"), _mode);

    _start = new EngEdit(QStringLiteral("Hz"));
    _stop = new EngEdit(QStringLiteral("Hz"));
    _center = new EngEdit(QStringLiteral("Hz"));
    _span = new EngEdit(QStringLiteral("Hz"));
    _form->addRow(tr("Start"), _start);
    _form->addRow(tr("Stop"), _stop);
    _form->addRow(tr("Center"), _center);
    _form->addRow(tr("Span"), _span);

    _cwFreq = new EngEdit(QStringLiteral("Hz"));
    _form->addRow(tr("Frequency"), _cwFreq);

    _power = new QDoubleSpinBox;
    _power->setSuffix(QStringLiteral(" dBm"));
    _power->setDecimals(2);
    _power->setKeyboardTracking(false);
    _form->addRow(tr("Power"), _power);

    _powerStart = new QDoubleSpinBox;
    _powerStop = new QDoubleSpinBox;
    for (QDoubleSpinBox* s : {_powerStart, _powerStop}) {
        s->setSuffix(QStringLiteral(" dBm"));
        s->setDecimals(2);
        s->setKeyboardTracking(false);
    }
    _form->addRow(tr("Power start"), _powerStart);
    _form->addRow(tr("Power stop"), _powerStop);

    _points = new QSpinBox;
    _points->setRange(2, 2048);
    _points->setKeyboardTracking(false);
    _form->addRow(tr("Points"), _points);

    _ifbw = new EngEdit(QStringLiteral("Hz"));
    _ifbw->setLowercaseMIsMega(false);
    _form->addRow(tr("IF bandwidth"), _ifbw);

    _dwell = new EngEdit(QStringLiteral("s"));
    _form->addRow(tr("Dwell"), _dwell);

    _averaging = new QSpinBox;
    _averaging->setRange(1, 999);
    _averaging->setToolTip(tr("Sweeps averaged on the PC, as complex data"));
    auto* restart = new QPushButton(tr("Restart"));
    auto* avgRow = new QWidget;
    auto* avgLayout = new QHBoxLayout(avgRow);
    avgLayout->setContentsMargins(0, 0, 0, 0);
    avgLayout->addWidget(_averaging, 1);
    avgLayout->addWidget(restart);
    _form->addRow(tr("Averaging"), avgRow);

    _sweepTime = new QLabel(QStringLiteral("-"));
    _form->addRow(tr("Sweep time"), _sweepTime);

    _scenario = new QComboBox;
    // Index 0 is what the combo shows when the settings match no preset.
    _scenario->addItem(tr("Custom"));
    for (const Scenario& s : kScenarios)
        _scenario->addItem(QString::fromLatin1(s.name));
    _scenario->setToolTip(tr("Sets power, IF bandwidth and points for a typical measurement. "
                             "The frequency range is not changed."));
    _form->addRow(tr("Preset"), _scenario);

    connect(_mode, &QComboBox::activated, this, [this](int i) {
        _device->setSweepMode(static_cast<SweepMode>(_mode->itemData(i).toInt()));
    });
    connect(_start, &EngEdit::valueEdited, _device, &VnaDevice::setStart);
    connect(_stop, &EngEdit::valueEdited, _device, &VnaDevice::setStop);
    connect(_center, &EngEdit::valueEdited, this, [this](double c) {
        _device->setCenterSpan(c, _device->stimulus().span());
    });
    connect(_span, &EngEdit::valueEdited, this, [this](double s) {
        _device->setCenterSpan(_device->stimulus().center(), s);
    });
    connect(_cwFreq, &EngEdit::valueEdited, _device, &VnaDevice::setCwFrequency);
    connect(_power, &QDoubleSpinBox::valueChanged, _device, &VnaDevice::setPower);
    connect(_powerStart, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        _device->setPowerRange(v, _powerStop->value());
    });
    connect(_powerStop, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        _device->setPowerRange(_powerStart->value(), v);
    });
    connect(_points, &QSpinBox::valueChanged, _device, &VnaDevice::setPoints);
    connect(_ifbw, &EngEdit::valueEdited, _device, &VnaDevice::setIfbw);
    connect(_dwell, &EngEdit::valueEdited, _device, &VnaDevice::setDwell);
    connect(_averaging, &QSpinBox::valueChanged, _device, &VnaDevice::setAveraging);
    connect(restart, &QPushButton::clicked, _device, &VnaDevice::restartAveraging);
    connect(_scenario, &QComboBox::activated, this, &StimulusPanel::applyScenario);

    connect(_device, &VnaDevice::stimulusChanged, this, &StimulusPanel::refresh);
    connect(_device, &VnaDevice::healthChanged, this, &StimulusPanel::refresh);
    connect(_device, &VnaDevice::infoChanged, this, &StimulusPanel::refresh);
    connect(_device, &VnaDevice::stateChanged, this, &StimulusPanel::refresh);
    refresh();
}

void StimulusPanel::applyScenario(int index)
{
    if (index < 1 || index > int(std::size(kScenarios))) {
        refresh();      // "Custom" is a description, not something to apply
        return;
    }
    const Scenario& s = kScenarios[index - 1];
    LogRouter::post(tr("Preset \"%1\": %2 dBm, IF bandwidth %3, %4 points")
                        .arg(QString::fromLatin1(s.name))
                        .arg(s.power)
                        .arg(Formats::engineering(s.ifbw, QStringLiteral("Hz")))
                        .arg(s.points));
    _device->setPower(s.power);
    _device->setIfbw(s.ifbw);
    _device->setPoints(s.points);
    // The combo then follows the values read back in refresh(): it stays on
    // this preset if all three took, and drops to Custom if one was refused.
}

void StimulusPanel::refresh()
{
    const bool ready = _device->isReady();
    setEnabled(ready);

    const Stimulus& st = _device->stimulus();
    const DeviceInfo& info = _device->info();

    // Populating controls must not send their values back to the instrument.
    const QSignalBlocker b1(_mode), b2(_power), b3(_powerStart), b4(_powerStop), b5(_points), b6(_averaging);

    int scenario = 0;
    for (int i = 0; i < int(std::size(kScenarios)); ++i) {
        const Scenario& s = kScenarios[i];
        if (st.power == s.power && st.ifbw == s.ifbw && st.points == s.points) {
            scenario = i + 1;
            break;
        }
    }
    _scenario->setCurrentIndex(scenario);

    _mode->setCurrentIndex(_mode->findData(int(st.mode)));
    _start->setValue(st.start);
    _stop->setValue(st.stop);
    _center->setValue(st.center());
    _span->setValue(st.span());
    _cwFreq->setValue(st.cwFreq);
    _ifbw->setValue(st.ifbw);
    _dwell->setValue(st.dwell);

    for (QDoubleSpinBox* s : {_power, _powerStart, _powerStop})
        s->setRange(info.minPower, info.maxPower);
    // A spin box being typed into is left alone; it commits on Enter.
    if (!_power->hasFocus()) _power->setValue(st.power);
    if (!_powerStart->hasFocus()) _powerStart->setValue(st.powerStart);
    if (!_powerStop->hasFocus()) _powerStop->setValue(st.powerStop);
    // The instrument accepts more points than it reports as its maximum; above
    // that figure sweeps still work, through a slower transfer.
    _points->setRange(2, std::max(info.maxPoints, 10001));
    _points->setToolTip(tr("Up to %1 points use the fast transfer. More (to 10001) works, but updates "
                           "are slower and each S-parameter comes from a different sweep.").arg(info.maxPoints));
    if (!_points->hasFocus()) _points->setValue(st.points);
    if (!_averaging->hasFocus()) _averaging->setValue(_device->averaging());

    const bool freq = st.mode == SweepMode::LinFreq || st.mode == SweepMode::LogFreq;
    const bool power = st.mode == SweepMode::Power;
    // CW mode measures at the centre of the start/stop range (per the
    // manual), so the range stays editable there.
    setRowVisible(_form, _start, !power);
    setRowVisible(_form, _stop, !power);
    setRowVisible(_form, _center, !power);
    setRowVisible(_form, _span, freq);
    setRowVisible(_form, _cwFreq, power);
    setRowVisible(_form, _power, !power);
    setRowVisible(_form, _powerStart, power);
    setRowVisible(_form, _powerStop, power);

    const double t = _device->health().sweepTimeS;
    _sweepTime->setText(t > 0 ? Formats::engineering(t, QStringLiteral("s"), 3) : QStringLiteral("-"));
}
