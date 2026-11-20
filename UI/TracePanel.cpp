#include "TracePanel.h"

#include "EngEdit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QFormLayout* addGroup(QVBoxLayout* into, const QString& title)
{
    auto* box = new QGroupBox(title);
    auto* form = new QFormLayout(box);
    form->setLabelAlignment(Qt::AlignRight);
    into->addWidget(box);
    return form;
}

} // namespace

TracePanel::TracePanel(TraceModel* model, QWidget* parent)
    : QWidget(parent)
    , _model(model)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto* body = new QWidget;
    scroll->setWidget(body);
    auto* col = new QVBoxLayout(body);

    // --- Trace list ---
    _list = new QTableWidget(0, 4);
    _list->setHorizontalHeaderLabels({tr("Trace"), tr("Meas"), tr("Format"), tr("Plot")});
    _list->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    _list->verticalHeader()->setVisible(false);
    _list->setSelectionBehavior(QAbstractItemView::SelectRows);
    _list->setSelectionMode(QAbstractItemView::SingleSelection);
    _list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _list->setMinimumHeight(110);
    _list->setMaximumHeight(170);
    col->addWidget(_list);

    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add"));
    auto* remove = new QPushButton(tr("Remove"));
    auto* autoScale = new QPushButton(tr("Auto scale"));
    auto* autoAll = new QPushButton(tr("Auto scale all"));
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addWidget(autoScale);
    buttons->addWidget(autoAll);
    col->addLayout(buttons);

    // --- Measurement ---
    QFormLayout* meas = addGroup(col, tr("Measurement"));
    _source = new QComboBox;
    _param = new QComboBox;
    for (int k = 0; k < kSParamCount; ++k)
        _param->addItem(sParamName(static_cast<SParam>(k)), k);
    _format = new QComboBox;
    for (int f = 0; f < kTraceFormatCount; ++f)
        _format->addItem(Formats::name(static_cast<TraceFormat>(f)), f);
    _plot = new QSpinBox;
    _plot->setMinimum(1);
    meas->addRow(tr("Source"), _source);
    meas->addRow(tr("Parameter"), _param);
    meas->addRow(tr("Format"), _format);
    meas->addRow(tr("Plot"), _plot);

    // --- Scale ---
    QFormLayout* scale = addGroup(col, tr("Scale"));
    _perDiv = new EngEdit(QString());
    _refValue = new EngEdit(QString());
    _refPos = new QSpinBox;
    _refPos->setRange(0, 10);
    _refPos->setToolTip(tr("Graticule line holding the reference value: 0 is the bottom, 10 the top"));
    scale->addRow(tr("Per division"), _perDiv);
    scale->addRow(tr("Reference"), _refValue);
    scale->addRow(tr("Ref position"), _refPos);

    // --- Memory, math, smoothing ---
    QFormLayout* mem = addGroup(col, tr("Memory and math"));
    auto* toMemory = new QPushButton(tr("Data → Memory"));
    _show = new QComboBox;
    _show->addItems({tr("Data"), tr("Memory"), tr("Data and memory")});
    _math = new QComboBox;
    _math->addItems({tr("Off"), tr("Data / Memory"), tr("Data − Memory"), tr("Data + Memory"), tr("Data × Memory")});
    _smoothing = new QCheckBox(tr("Smoothing"));
    _smoothPercent = new QDoubleSpinBox;
    _smoothPercent->setRange(0.1, 25.0);
    _smoothPercent->setSuffix(QStringLiteral(" %"));
    _smoothPercent->setDecimals(1);
    _aperture = new QDoubleSpinBox;
    _aperture->setRange(0.1, 25.0);
    _aperture->setSuffix(QStringLiteral(" %"));
    _aperture->setDecimals(1);
    _aperture->setToolTip(tr("Group delay aperture, as a share of the sweep"));
    mem->addRow(toMemory);
    mem->addRow(tr("Show"), _show);
    mem->addRow(tr("Math"), _math);
    mem->addRow(_smoothing, _smoothPercent);
    mem->addRow(tr("Delay aperture"), _aperture);

    // --- Time domain ---
    QFormLayout* td = addGroup(col, tr("Time domain"));
    _tdEnabled = new QCheckBox(tr("Transform to time domain"));
    _tdMode = new QComboBox;
    for (const TdMode m : {TdMode::BandPass, TdMode::LowPassImpulse, TdMode::LowPassStep})
        _tdMode->addItem(TimeDomain::modeName(m), int(m));
    _tdWindow = new QComboBox;
    for (const WindowType w : {WindowType::Rectangular, WindowType::Hamming, WindowType::Hann,
                               WindowType::Blackman, WindowType::Kaiser})
        _tdWindow->addItem(TimeDomain::windowName(w), int(w));
    _tdStart = new EngEdit(QStringLiteral("s"));
    _tdStop = new EngEdit(QStringLiteral("s"));
    _tdDistance = new QCheckBox(tr("Distance axis"));
    _tdVf = new QDoubleSpinBox;
    _tdVf->setRange(0.1, 1.0);
    _tdVf->setDecimals(3);
    _tdVf->setSingleStep(0.01);
    _tdInfo = new QLabel;
    _tdInfo->setWordWrap(true);
    td->addRow(_tdEnabled);
    td->addRow(tr("Mode"), _tdMode);
    td->addRow(tr("Window"), _tdWindow);
    td->addRow(tr("Start"), _tdStart);
    td->addRow(tr("Stop"), _tdStop);
    td->addRow(_tdDistance);
    td->addRow(tr("Velocity factor"), _tdVf);
    td->addRow(_tdInfo);

    // --- Gating ---
    QFormLayout* gate = addGroup(col, tr("Time gating"));
    _gateEnabled = new QCheckBox(tr("Gate"));
    _gateStart = new EngEdit(QStringLiteral("s"));
    _gateStop = new EngEdit(QStringLiteral("s"));
    _gateNotch = new QCheckBox(tr("Notch (remove the gated span)"));
    gate->addRow(_gateEnabled);
    gate->addRow(tr("Start"), _gateStart);
    gate->addRow(tr("Stop"), _gateStop);
    gate->addRow(_gateNotch);

    // --- Port extension (all traces) ---
    QFormLayout* ext = addGroup(col, tr("Port extension (all traces)"));
    for (int port = 0; port < 2; ++port) {
        _extEnabled[port] = new QCheckBox(tr("Port %1").arg(port + 1));
        _extDelay[port] = new EngEdit(QStringLiteral("s"));
        _extDelay[port]->setToolTip(tr("One-way delay of the fixture on this port"));
        _extLoss[port] = new QDoubleSpinBox;
        _extLoss[port]->setRange(0, 60);
        _extLoss[port]->setDecimals(2);
        _extLoss[port]->setSuffix(QStringLiteral(" dB"));
        _extLoss[port]->setToolTip(tr("One-way loss of the fixture on this port"));
        ext->addRow(_extEnabled[port], _extDelay[port]);
        ext->addRow(tr("Loss"), _extLoss[port]);

        connect(_extEnabled[port], &QCheckBox::toggled, this, [this, port](bool on) {
            if (_refreshing) return;
            _model->edit().ext[port].enabled = on;
            _model->changed();
        });
        connect(_extDelay[port], &EngEdit::valueEdited, this, [this, port](double v) {
            _model->edit().ext[port].delay = v;
            _model->changed();
        });
        connect(_extLoss[port], &QDoubleSpinBox::valueChanged, this, [this, port](double v) {
            if (_refreshing) return;
            _model->edit().ext[port].lossDb = v;
            _model->changed();
        });
    }
    _z0 = new QDoubleSpinBox;
    _z0->setRange(1, 1000);
    _z0->setDecimals(1);
    _z0->setSuffix(QStringLiteral(" Ω"));
    ext->addRow(tr("Z0"), _z0);
    connect(_z0, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (_refreshing) return;
        _model->edit().z0 = v;
        _model->changed();
    });

    col->addStretch(1);

    // --- Wiring ---
    connect(_list, &QTableWidget::currentCellChanged, this, [this](int row) {
        if (!_refreshing && row >= 0)
            _model->setActiveTrace(row);
    });
    connect(add, &QPushButton::clicked, this, [this] {
        const TraceConfig* a = _model->activeTrace();
        _model->addTrace(a ? a->param : SParam::S11, TraceFormat::LogMag, a ? a->plot : 0);
    });
    connect(remove, &QPushButton::clicked, this, [this] { _model->removeTrace(_model->activeIndex()); });
    connect(autoScale, &QPushButton::clicked, this, [this] { _model->autoScale(_model->activeIndex()); });
    connect(autoAll, &QPushButton::clicked, _model, &TraceModel::autoScaleAll);

    connect(_source, &QComboBox::activated, this, [this](int i) {
        edit([&](TraceConfig& t) { t.fileIndex = _source->itemData(i).toInt(); }, true);
    });
    connect(_param, &QComboBox::activated, this, [this](int i) {
        edit([&](TraceConfig& t) { t.param = static_cast<SParam>(i); }, true);
    });
    connect(_format, &QComboBox::activated, this, [this](int i) {
        _model->setFormat(_model->activeIndex(), static_cast<TraceFormat>(i));
        _model->notifyStructureChanged();
    });
    connect(_plot, &QSpinBox::valueChanged, this, [this](int v) {
        if (_refreshing) return;
        edit([&](TraceConfig& t) { t.plot = v - 1; }, true);
    });
    connect(_perDiv, &EngEdit::valueEdited, this, [this](double v) {
        if (v > 0) edit([&](TraceConfig& t) { t.perDiv = v; });
    });
    connect(_refValue, &EngEdit::valueEdited, this, [this](double v) {
        edit([&](TraceConfig& t) { t.refValue = v; });
    });
    connect(_refPos, &QSpinBox::valueChanged, this, [this](int v) {
        if (_refreshing) return;
        edit([&](TraceConfig& t) { t.refPos = v; });
    });
    connect(toMemory, &QPushButton::clicked, this, [this] {
        _model->dataToMemory(_model->activeIndex());
        refresh();
    });
    connect(_show, &QComboBox::activated, this, [this](int i) {
        edit([&](TraceConfig& t) { t.show = static_cast<ShowWhat>(i); });
    });
    connect(_math, &QComboBox::activated, this, [this](int i) {
        edit([&](TraceConfig& t) { t.math = static_cast<MathOp>(i); });
    });
    connect(_smoothing, &QCheckBox::toggled, this, [this](bool on) {
        if (_refreshing) return;
        edit([&](TraceConfig& t) { t.smoothing = on; });
    });
    connect(_smoothPercent, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (_refreshing) return;
        edit([&](TraceConfig& t) { t.smoothingPercent = v; });
    });
    connect(_aperture, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (_refreshing) return;
        edit([&](TraceConfig& t) { t.delayAperturePercent = v; });
    });

    connect(_tdEnabled, &QCheckBox::toggled, this, [this](bool on) {
        if (_refreshing) return;
        edit([&](TraceConfig& t) {
            t.td.enabled = on;
            // Log magnitude of a time response is rarely what is wanted first.
            if (on && t.format == TraceFormat::LogMag) {
                t.format = TraceFormat::LinMag;
                Formats::defaultScale(t.format, t.perDiv, t.refValue, t.refPos);
            }
        }, true);
        _model->autoScale(_model->activeIndex());
    });
    connect(_tdMode, &QComboBox::activated, this, [this](int i) {
        edit([&](TraceConfig& t) {
            t.td.mode = static_cast<TdMode>(_tdMode->itemData(i).toInt());
            // A step response is signed; magnitude would fold a short onto an open.
            if (t.td.mode == TdMode::LowPassStep && t.format == TraceFormat::LinMag)
                t.format = TraceFormat::Real;
        }, true);
        _model->autoScale(_model->activeIndex());
    });
    connect(_tdWindow, &QComboBox::activated, this, [this](int i) {
        edit([&](TraceConfig& t) { t.td.window = static_cast<WindowType>(_tdWindow->itemData(i).toInt()); });
    });
    connect(_tdStart, &EngEdit::valueEdited, this, [this](double v) { edit([&](TraceConfig& t) { t.td.start = v; }); });
    connect(_tdStop, &EngEdit::valueEdited, this, [this](double v) { edit([&](TraceConfig& t) { t.td.stop = v; }); });
    connect(_tdDistance, &QCheckBox::toggled, this, [this](bool on) {
        if (_refreshing) return;
        edit([&](TraceConfig& t) { t.td.distanceAxis = on; });
    });
    connect(_tdVf, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (_refreshing) return;
        edit([&](TraceConfig& t) { t.td.velocityFactor = v; });
    });

    connect(_gateEnabled, &QCheckBox::toggled, this, [this](bool on) {
        if (_refreshing) return;
        edit([&](TraceConfig& t) { t.gate.enabled = on; });
    });
    connect(_gateStart, &EngEdit::valueEdited, this, [this](double v) { edit([&](TraceConfig& t) { t.gate.start = v; }); });
    connect(_gateStop, &EngEdit::valueEdited, this, [this](double v) { edit([&](TraceConfig& t) { t.gate.stop = v; }); });
    connect(_gateNotch, &QCheckBox::toggled, this, [this](bool on) {
        if (_refreshing) return;
        edit([&](TraceConfig& t) { t.gate.notch = on; });
    });

    connect(_model, &TraceModel::structureChanged, this, &TracePanel::refresh);
    refresh();
}

void TracePanel::edit(const std::function<void(TraceConfig&)>& fn, bool structural)
{
    TraceConfig* t = _model->activeTrace();
    if (!t)
        return;
    fn(*t);
    if (structural)
        _model->notifyStructureChanged();
    else
        _model->changed();
}

void TracePanel::refresh()
{
    _refreshing = true;
    const Workspace& ws = _model->workspace();

    _list->setRowCount(int(ws.traces.size()));
    for (int i = 0; i < ws.traces.size(); ++i) {
        const TraceConfig& t = ws.traces[i];
        const QStringList cells = {
            t.name(),
            t.fileIndex >= 0 ? sParamName(t.param) + QStringLiteral(" (file)") : sParamName(t.param),
            (t.td.enabled ? QStringLiteral("TD ") : QString()) + Formats::name(t.format),
            QString::number(t.plot + 1),
        };
        for (int c = 0; c < cells.size(); ++c) {
            auto* item = new QTableWidgetItem(cells[c]);
            if (c == 0)
                item->setForeground(t.color);
            _list->setItem(i, c, item);
        }
    }
    if (ws.activeTrace < _list->rowCount())
        _list->setCurrentCell(ws.activeTrace, 0);

    _source->clear();
    _source->addItem(tr("Live"), -1);
    for (int i = 0; i < ws.files.size(); ++i)
        _source->addItem(ws.files[i].label, i);
    _plot->setMaximum(std::max(1, ws.plotCount));

    for (int port = 0; port < 2; ++port) {
        _extEnabled[port]->setChecked(ws.ext[port].enabled);
        _extDelay[port]->setValue(ws.ext[port].delay);
        _extLoss[port]->setValue(ws.ext[port].lossDb);
    }
    _z0->setValue(ws.z0);

    const TraceConfig* t = _model->activeTrace();
    const QList<QWidget*> perTrace = {_source, _param, _format, _plot, _perDiv, _refValue, _refPos, _show, _math,
                                      _smoothing, _smoothPercent, _aperture, _tdEnabled, _tdMode, _tdWindow,
                                      _tdStart, _tdStop, _tdDistance, _tdVf, _gateEnabled, _gateStart,
                                      _gateStop, _gateNotch};
    for (QWidget* w : perTrace)
        w->setEnabled(t != nullptr);

    if (t) {
        _source->setCurrentIndex(std::max(0, _source->findData(t->fileIndex)));
        _param->setCurrentIndex(int(t->param));
        _format->setCurrentIndex(int(t->format));
        _plot->setValue(t->plot + 1);
        _perDiv->setValue(t->perDiv);
        _refValue->setValue(t->refValue);
        _refPos->setValue(t->refPos);
        const bool chart = Formats::isChart(t->format);
        _perDiv->setEnabled(!chart);
        _refPos->setEnabled(!chart);
        _show->setCurrentIndex(int(t->show));
        _math->setCurrentIndex(int(t->math));
        _smoothing->setChecked(t->smoothing);
        _smoothPercent->setValue(t->smoothingPercent);
        _aperture->setValue(t->delayAperturePercent);

        _tdEnabled->setChecked(t->td.enabled);
        _tdMode->setCurrentIndex(_tdMode->findData(int(t->td.mode)));
        _tdWindow->setCurrentIndex(_tdWindow->findData(int(t->td.window)));
        _tdStart->setValue(t->td.start);
        _tdStop->setValue(t->td.stop);
        _tdDistance->setChecked(t->td.distanceAxis);
        _tdVf->setValue(t->td.velocityFactor);

        _gateEnabled->setChecked(t->gate.enabled);
        _gateStart->setValue(t->gate.start);
        _gateStop->setValue(t->gate.stop);
        _gateNotch->setChecked(t->gate.notch);

        // What the current sweep can resolve, so the window can be set sensibly.
        const SweepData* sweep = _model->sourceSweep(*t);
        QString info;
        if (sweep && sweep->xKind == XKind::Frequency && sweep->points() > 2) {
            const double span = TimeDomain::aliasFreeSpan(sweep->x);
            if (span > 0.0) {
                const double bandwidth = sweep->x.back() - sweep->x.front();
                info = tr("Alias-free range %1, resolution about %2")
                           .arg(Formats::engineering(span, QStringLiteral("s"), 3),
                                Formats::engineering(1.0 / bandwidth, QStringLiteral("s"), 3));
            } else {
                info = tr("Needs a linear frequency sweep");
            }
        }
        _tdInfo->setText(info);
    }
    _refreshing = false;
}
