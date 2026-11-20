#include "MarkerPanel.h"

#include "EngEdit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString unitOf(XKind k)
{
    switch (k) {
    case XKind::Frequency: return QStringLiteral("Hz");
    case XKind::Power:     return QStringLiteral("dBm");
    case XKind::Time:      return QStringLiteral("s");
    case XKind::Distance:  return QStringLiteral("m");
    }
    return QString();
}
} // namespace

MarkerPanel::MarkerPanel(TraceModel* model, QWidget* parent)
    : QWidget(parent)
    , _model(model)
{
    auto* col = new QVBoxLayout(this);

    _table = new QTableWidget(0, 3);
    _table->setHorizontalHeaderLabels({tr("Marker"), tr("Stimulus"), tr("Tracking")});
    _table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    _table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    _table->verticalHeader()->setVisible(false);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setMinimumHeight(120);
    col->addWidget(_table, 1);

    auto* grid = new QGridLayout;
    auto* add = new QPushButton(tr("Add"));
    auto* remove = new QPushButton(tr("Remove"));
    auto* clear = new QPushButton(tr("Clear all"));
    auto* max = new QPushButton(tr("Max"));
    auto* min = new QPushButton(tr("Min"));
    auto* peakL = new QPushButton(tr("◄ Peak"));
    auto* peakR = new QPushButton(tr("Peak ►"));
    auto* targetL = new QPushButton(tr("◄ Target"));
    auto* targetR = new QPushButton(tr("Target ►"));
    grid->addWidget(add, 0, 0);
    grid->addWidget(remove, 0, 1);
    grid->addWidget(clear, 0, 2);
    grid->addWidget(max, 1, 0);
    grid->addWidget(peakL, 1, 1);
    grid->addWidget(peakR, 1, 2);
    grid->addWidget(min, 2, 0);
    grid->addWidget(targetL, 2, 1);
    grid->addWidget(targetR, 2, 2);
    col->addLayout(grid);

    auto* form = new QFormLayout;
    _target = new QDoubleSpinBox;
    _target->setRange(-1e6, 1e6);
    _target->setDecimals(3);
    _target->setValue(-3.0);
    _target->setToolTip(tr("Value the Target searches look for, in the trace's units"));
    _prominence = new QDoubleSpinBox;
    _prominence->setRange(0.0, 1000.0);
    _prominence->setDecimals(3);
    _prominence->setToolTip(tr("How far a peak must stand above its surroundings to count"));
    _deltaRef = new QComboBox;
    _coupled = new QCheckBox(tr("Couple markers across traces"));
    form->addRow(tr("Target value"), _target);
    form->addRow(tr("Peak excursion"), _prominence);
    form->addRow(tr("Delta reference"), _deltaRef);
    form->addRow(_coupled);
    col->addLayout(form);

    auto* bwBox = new QGroupBox(tr("Bandwidth search"));
    auto* bw = new QFormLayout(bwBox);
    _bwEnabled = new QCheckBox(tr("Search every sweep"));
    _bwLevel = new QDoubleSpinBox;
    _bwLevel->setRange(-100, 100);
    _bwLevel->setDecimals(2);
    _bwLevel->setSuffix(QStringLiteral(" dB"));
    _bwNotch = new QCheckBox(tr("Notch (search around the minimum)"));
    _bwFromMarker = new QCheckBox(tr("Reference is marker 1, not the peak"));
    _bwResult = new QLabel;
    _bwResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bw->addRow(_bwEnabled);
    bw->addRow(tr("Level"), _bwLevel);
    bw->addRow(_bwNotch);
    bw->addRow(_bwFromMarker);
    bw->addRow(_bwResult);
    col->addWidget(bwBox);

    connect(add, &QPushButton::clicked, this, [this] { addMarker(); });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int n = selectedNumber();
        if (n > 0)
            _model->removeMarker(_model->activeIndex(), n);
        rebuild();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        _model->clearMarkers(_model->activeIndex());
        rebuild();
    });
    connect(max, &QPushButton::clicked, this, [this] { search(TraceModel::Search::Max); });
    connect(min, &QPushButton::clicked, this, [this] { search(TraceModel::Search::Min); });
    connect(peakL, &QPushButton::clicked, this, [this] { search(TraceModel::Search::PeakLeft); });
    connect(peakR, &QPushButton::clicked, this, [this] { search(TraceModel::Search::PeakRight); });
    connect(targetL, &QPushButton::clicked, this, [this] { search(TraceModel::Search::TargetLeft); });
    connect(targetR, &QPushButton::clicked, this, [this] { search(TraceModel::Search::TargetRight); });

    connect(_prominence, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (!_refreshing)
            _model->edit().peakProminence = v;
    });
    connect(_deltaRef, &QComboBox::activated, this, [this](int i) {
        if (TraceConfig* t = _model->activeTrace()) {
            t->deltaRef = _deltaRef->itemData(i).toInt();
            _model->changed();
        }
    });
    connect(_coupled, &QCheckBox::toggled, this, [this](bool on) {
        if (!_refreshing)
            _model->edit().coupledMarkers = on;
    });

    auto bwEdit = [this] {
        if (_refreshing)
            return;
        if (TraceConfig* t = _model->activeTrace()) {
            t->bw.enabled = _bwEnabled->isChecked();
            t->bw.level = _bwLevel->value();
            t->bw.notch = _bwNotch->isChecked();
            t->bw.fromMarker = _bwFromMarker->isChecked();
            _model->changed();
        }
    };
    connect(_bwEnabled, &QCheckBox::toggled, this, bwEdit);
    connect(_bwNotch, &QCheckBox::toggled, this, bwEdit);
    connect(_bwFromMarker, &QCheckBox::toggled, this, bwEdit);
    connect(_bwLevel, &QDoubleSpinBox::valueChanged, this, bwEdit);

    connect(_model, &TraceModel::structureChanged, this, &MarkerPanel::rebuild);
    connect(_model, &TraceModel::resultsChanged, this, &MarkerPanel::updateValues);
    rebuild();
}

int MarkerPanel::selectedNumber() const
{
    const TraceConfig* t = _model->activeTrace();
    if (!t || t->markers.isEmpty())
        return 0;
    const int row = _table->currentRow();
    return (row >= 0 && row < t->markers.size()) ? t->markers[row].number : t->markers.first().number;
}

void MarkerPanel::addMarker(bool atX, double x)
{
    const int trace = _model->activeIndex();
    const TraceResult& r = _model->result(trace);
    if (!r.valid || r.x.empty())
        return;
    const int number = _model->addMarker(trace, atX ? x : 0.5 * (r.x.front() + r.x.back()));
    // A new marker is most useful on the feature being looked at.
    if (!atX)
        _model->searchMarker(trace, number, TraceModel::Search::Max);
    rebuild();
    if (const TraceConfig* t = _model->activeTrace()) {
        for (int i = 0; i < t->markers.size(); ++i)
            if (t->markers[i].number == number)
                _table->setCurrentCell(i, 0);
    }
}

void MarkerPanel::search(TraceModel::Search kind)
{
    if (!_model->activeTrace())
        return;
    if (_model->activeTrace()->markers.isEmpty()) {
        addMarker();
        if (kind == TraceModel::Search::Max)
            return;
    }
    _model->searchMarker(_model->activeIndex(), selectedNumber(), kind, _target->value());
}

void MarkerPanel::rebuild()
{
    _refreshing = true;
    const Workspace& ws = _model->workspace();
    const TraceConfig* t = _model->activeTrace();
    const int trace = _model->activeIndex();
    const int keepRow = _table->currentRow();

    _table->setRowCount(t ? int(t->markers.size()) : 0);
    _deltaRef->clear();
    _deltaRef->addItem(tr("Off"), 0);

    if (t) {
        const QString unit = unitOf(_model->result(trace).xKind);
        for (int i = 0; i < t->markers.size(); ++i) {
            const Marker& m = t->markers[i];
            const int number = m.number;

            auto* label = new QTableWidgetItem(QStringLiteral("M%1").arg(number));
            label->setFlags(label->flags() & ~Qt::ItemIsEditable);
            label->setForeground(t->color);
            _table->setItem(i, 0, label);

            auto* x = new EngEdit(unit);
            x->setFrame(false);
            x->setValue(m.x);
            connect(x, &EngEdit::valueEdited, this, [this, number](double v) {
                _model->moveMarker(_model->activeIndex(), number, v);
            });
            _table->setCellWidget(i, 1, x);

            auto* track = new QComboBox;
            track->addItems({tr("Off"), tr("Max"), tr("Min"), tr("Target")});
            track->setCurrentIndex(int(m.track));
            connect(track, &QComboBox::activated, this, [this, number](int idx) {
                TraceConfig* at = _model->activeTrace();
                if (!at)
                    return;
                for (Marker& mk : at->markers) {
                    if (mk.number == number) {
                        mk.track = static_cast<MarkerTrack>(idx);
                        mk.target = _target->value();
                    }
                }
                _model->changed();
            });
            _table->setCellWidget(i, 2, track);

            _deltaRef->addItem(QStringLiteral("M%1").arg(number), number);
        }
        _deltaRef->setCurrentIndex(std::max(0, _deltaRef->findData(t->deltaRef)));
        if (keepRow >= 0 && keepRow < _table->rowCount())
            _table->setCurrentCell(keepRow, 0);

        _bwEnabled->setChecked(t->bw.enabled);
        _bwLevel->setValue(t->bw.level);
        _bwNotch->setChecked(t->bw.notch);
        _bwFromMarker->setChecked(t->bw.fromMarker);
    }
    _coupled->setChecked(ws.coupledMarkers);
    _prominence->setValue(ws.peakProminence);
    _refreshing = false;
    updateValues();
}

void MarkerPanel::updateValues()
{
    const TraceConfig* t = _model->activeTrace();
    const TraceResult& r = _model->result(_model->activeIndex());
    if (!t)
        return;

    // Marker count changed behind our back (context menu, workspace load).
    if (_table->rowCount() != t->markers.size()) {
        rebuild();
        return;
    }
    for (int i = 0; i < t->markers.size(); ++i) {
        if (auto* x = qobject_cast<EngEdit*>(_table->cellWidget(i, 1)))
            x->setValue(t->markers[i].x);
        if (i < r.markers.size())
            _table->item(i, 0)->setToolTip(r.markers[i].text);
    }

    if (!t->bw.enabled) {
        _bwResult->setText(QString());
    } else if (!r.bw.valid) {
        _bwResult->setText(tr("No %1 dB points found").arg(t->bw.level, 0, 'g', 3));
    } else {
        _bwResult->setText(tr("BW %1\nCenter %2\nLow %3\nHigh %4\nQ %5\n%6 %7 %8")
            .arg(Formats::xText(r.bw.bandwidth, r.xKind), Formats::xText(r.bw.center, r.xKind),
                 Formats::xText(r.bw.low, r.xKind), Formats::xText(r.bw.high, r.xKind))
            .arg(r.bw.q, 0, 'f', 2)
            .arg(t->bw.notch ? tr("Minimum") : tr("Loss"))
            .arg(r.bw.loss, 0, 'f', 3)
            .arg(Formats::unit(t->format)));
    }
}
