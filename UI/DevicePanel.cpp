#include "DevicePanel.h"

#include "Dsp/Formats.h"
#include "Instrument/VnaDevice.h"
#include "Theme.h"
#include "Utils/AppSettings.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

DevicePanel::DevicePanel(VnaDevice* device, QWidget* parent)
    : QWidget(parent)
    , _device(device)
{
    auto* col = new QVBoxLayout(this);

    auto* idBox = new QGroupBox(tr("Instrument"));
    auto* idForm = new QFormLayout(idBox);
    _identity = new QLabel;
    _identity->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _identity->setWordWrap(true);
    _limits = new QLabel;
    _limits->setWordWrap(true);
    _health = new QLabel;
    _temperature = new QLabel;
    idForm->addRow(_identity);
    idForm->addRow(tr("Limits"), _limits);
    idForm->addRow(tr("Status"), _health);
    idForm->addRow(tr("RF unit"), _temperature);
    col->addWidget(idBox);

    auto* acqBox = new QGroupBox(tr("On the instrument"));
    auto* acqForm = new QFormLayout(acqBox);
    _deviceRun = new QPushButton;
    _deviceRun->setToolTip(tr("Starts or stops VNStudio's own sweep on the tablet. "
                              "This application can sweep either way."));
    _externalRef = new QCheckBox(tr("External 10 MHz reference"));
    auto* preset = new QPushButton(tr("Preset (*RST)"));
    acqForm->addRow(tr("Tablet sweep"), _deviceRun);
    acqForm->addRow(_externalRef);
    acqForm->addRow(preset);
    col->addWidget(acqBox);

    auto* fileBox = new QGroupBox(tr("Files on the instrument"));
    auto* fileCol = new QVBoxLayout(fileBox);
    auto* hint = new QLabel(tr("Paths are on the tablet's own file system, for example "
                               "D:/VNA/setup.vnaz. Calibrate in VNStudio and save it there; "
                               "it can then be loaded from here."));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::Colour::kSection.name()));
    fileCol->addWidget(hint);

    auto* fileForm = new QFormLayout;
    _statePath = new QLineEdit(AppSettings::value(QStringLiteral("Device/statePath")).toString());
    _statePath->setPlaceholderText(QStringLiteral("D:/VNA/setup.vnaz"));
    auto* stateButtons = new QHBoxLayout;
    auto* saveState = new QPushButton(tr("Save state"));
    auto* loadState = new QPushButton(tr("Load state"));
    stateButtons->addWidget(saveState);
    stateButtons->addWidget(loadState);
    _calPath = new QLineEdit(AppSettings::value(QStringLiteral("Device/calPath")).toString());
    _calPath->setPlaceholderText(tr("Path of a calibration saved in VNStudio"));
    auto* loadCal = new QPushButton(tr("Load calibration"));
    fileForm->addRow(tr("State"), _statePath);
    fileForm->addRow(stateButtons);
    fileForm->addRow(tr("Calibration"), _calPath);
    fileForm->addRow(loadCal);
    fileCol->addLayout(fileForm);
    col->addWidget(fileBox);
    col->addStretch(1);

    connect(_deviceRun, &QPushButton::clicked, this, [this] {
        _device->setDeviceRunning(!_device->stimulus().deviceRunning);
    });
    connect(_externalRef, &QCheckBox::toggled, _device, &VnaDevice::setExternalReference);
    connect(preset, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Preset"),
                tr("Reset the instrument to its defaults?\n\nThis also discards the calibration "
                   "currently applied on the tablet.")) == QMessageBox::Yes)
            _device->preset();
    });
    connect(saveState, &QPushButton::clicked, this, [this] {
        const QString path = _statePath->text().trimmed();
        if (path.isEmpty())
            return;
        AppSettings::setValue(QStringLiteral("Device/statePath"), path);
        _device->saveState(path);
    });
    connect(loadState, &QPushButton::clicked, this, [this] {
        const QString path = _statePath->text().trimmed();
        if (path.isEmpty())
            return;
        AppSettings::setValue(QStringLiteral("Device/statePath"), path);
        _device->loadState(path);
    });
    connect(loadCal, &QPushButton::clicked, this, [this] {
        const QString path = _calPath->text().trimmed();
        if (path.isEmpty())
            return;
        AppSettings::setValue(QStringLiteral("Device/calPath"), path);
        _device->loadCalibration(path);
    });

    connect(_device, &VnaDevice::stateChanged, this, &DevicePanel::refresh);
    connect(_device, &VnaDevice::infoChanged, this, &DevicePanel::refresh);
    connect(_device, &VnaDevice::healthChanged, this, &DevicePanel::refresh);
    connect(_device, &VnaDevice::stimulusChanged, this, &DevicePanel::refresh);
    refresh();
}

void DevicePanel::refresh()
{
    const bool ready = _device->isReady();
    setEnabled(ready);

    const DeviceInfo& info = _device->info();
    if (!ready) {
        _identity->setText(tr("Not connected"));
        _limits->clear();
        _health->clear();
        _temperature->clear();
        _deviceRun->setText(tr("-"));
        return;
    }

    _identity->setText(tr("%1 %2\nSerial %3\nFirmware %4, hardware %5\nVNStudio %6, SCPI service %7")
        .arg(info.manufacturer, info.model, info.serial, info.firmware, info.hardware,
             info.clientVersion, info.serviceVersion));
    _limits->setText(tr("%1 – %2\nIF bandwidth %3 – %4\n%5 points, %6 to %7 dBm")
        .arg(Formats::engineering(info.minFreq, QStringLiteral("Hz")),
             Formats::engineering(info.maxFreq, QStringLiteral("Hz")),
             Formats::engineering(info.minIfbw, QStringLiteral("Hz")),
             Formats::engineering(info.maxIfbw, QStringLiteral("Hz")))
        .arg(info.maxPoints).arg(info.minPower).arg(info.maxPower));

    const Health& h = _device->health();
    QStringList problems;
    if (h.unlocked) problems << tr("PLL unlocked");
    if (h.adcOverload) problems << tr("ADC overload");
    if (h.unlevelled) problems << tr("unlevelled");
    _health->setText(problems.isEmpty() ? tr("OK") : problems.join(QStringLiteral(", ")));
    _health->setStyleSheet(QStringLiteral("color: %1;")
        .arg((problems.isEmpty() ? Theme::Colour::kOk : Theme::Colour::kError).name()));
    _temperature->setText(tr("%1 °C").arg(h.temperatureC, 0, 'f', 0));

    const Stimulus& st = _device->stimulus();
    _deviceRun->setText(st.deviceRunning ? tr("Running - click to stop") : tr("Stopped - click to run"));
    const QSignalBlocker block(_externalRef);
    _externalRef->setChecked(st.externalRef);
}
