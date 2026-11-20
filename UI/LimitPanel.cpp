#include "LimitPanel.h"

#include "EngEdit.h"
#include "Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

LimitPanel::LimitPanel(TraceModel* model, QWidget* parent)
    : QWidget(parent)
    , _model(model)
{
    auto* col = new QVBoxLayout(this);

    _enabled = new QCheckBox(tr("Limit test on the active trace"));
    col->addWidget(_enabled);

    _table = new QTableWidget(0, 5);
    _table->setHorizontalHeaderLabels({tr("Type"), tr("Start"), tr("Value"), tr("Stop"), tr("Value")});
    _table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    _table->verticalHeader()->setVisible(false);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setToolTip(tr("Stimulus values accept 2.4G, 100M, 3n and so on"));
    col->addWidget(_table, 1);

    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add segment"));
    auto* remove = new QPushButton(tr("Remove"));
    auto* clear = new QPushButton(tr("Clear"));
    row->addWidget(add);
    row->addWidget(remove);
    row->addWidget(clear);
    col->addLayout(row);

    _verdict = new QLabel;
    QFont f = _verdict->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 2);
    _verdict->setFont(f);
    col->addWidget(_verdict);

    connect(_enabled, &QCheckBox::toggled, this, [this](bool on) {
        if (_refreshing)
            return;
        if (TraceConfig* t = _model->activeTrace()) {
            t->limitTest = on;
            _model->changed();
        }
    });
    connect(add, &QPushButton::clicked, this, [this] {
        TraceConfig* t = _model->activeTrace();
        const TraceResult& r = _model->result(_model->activeIndex());
        if (!t || r.x.empty())
            return;
        LimitSegment s;
        if (!t->limits.isEmpty()) {
            // Continue from the end of the previous segment.
            const LimitSegment& last = t->limits.last();
            s = last;
            s.x1 = last.x2;
            s.y1 = last.y2;
            s.x2 = r.x.back();
        } else {
            s.x1 = r.x.front();
            s.x2 = r.x.back();
            s.y1 = s.y2 = t->refValue;
        }
        t->limits.append(s);
        t->limitTest = true;
        _model->changed();
        rebuild();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        TraceConfig* t = _model->activeTrace();
        const int rowIndex = _table->currentRow();
        if (!t || rowIndex < 0 || rowIndex >= t->limits.size())
            return;
        t->limits.removeAt(rowIndex);
        _model->changed();
        rebuild();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (TraceConfig* t = _model->activeTrace()) {
            t->limits.clear();
            _model->changed();
            rebuild();
        }
    });
    connect(_table, &QTableWidget::cellChanged, this, [this] {
        if (!_refreshing)
            commitTable();
    });

    connect(_model, &TraceModel::structureChanged, this, &LimitPanel::rebuild);
    connect(_model, &TraceModel::resultsChanged, this, &LimitPanel::updateVerdict);
    rebuild();
}

void LimitPanel::rebuild()
{
    _refreshing = true;
    const TraceConfig* t = _model->activeTrace();
    _enabled->setEnabled(t != nullptr);
    _enabled->setChecked(t && t->limitTest);
    _table->setRowCount(t ? int(t->limits.size()) : 0);

    if (t) {
        const XKind kind = _model->result(_model->activeIndex()).xKind;
        for (int i = 0; i < t->limits.size(); ++i) {
            const LimitSegment& s = t->limits[i];
            auto* type = new QComboBox;
            type->addItems({tr("Upper"), tr("Lower")});
            type->setCurrentIndex(s.upper ? 0 : 1);
            connect(type, &QComboBox::activated, this, [this, i](int idx) {
                TraceConfig* at = _model->activeTrace();
                if (at && i < at->limits.size()) {
                    at->limits[i].upper = idx == 0;
                    _model->changed();
                }
            });
            _table->setCellWidget(i, 0, type);
            _table->setItem(i, 1, new QTableWidgetItem(Formats::xText(s.x1, kind)));
            _table->setItem(i, 2, new QTableWidgetItem(QString::number(s.y1, 'g', 8)));
            _table->setItem(i, 3, new QTableWidgetItem(Formats::xText(s.x2, kind)));
            _table->setItem(i, 4, new QTableWidgetItem(QString::number(s.y2, 'g', 8)));
        }
    }
    _refreshing = false;
    updateVerdict();
}

void LimitPanel::commitTable()
{
    TraceConfig* t = _model->activeTrace();
    if (!t)
        return;
    const XKind kind = _model->result(_model->activeIndex()).xKind;
    const QString unit = kind == XKind::Frequency ? QStringLiteral("Hz")
                       : kind == XKind::Power ? QStringLiteral("dBm")
                       : kind == XKind::Time ? QStringLiteral("s") : QStringLiteral("m");

    for (int i = 0; i < t->limits.size() && i < _table->rowCount(); ++i) {
        LimitSegment& s = t->limits[i];
        double v = 0;
        // An entry that does not parse keeps the old value; rebuild() below
        // then puts the old text back.
        if (_table->item(i, 1) && EngEdit::parse(_table->item(i, 1)->text(), unit, kind == XKind::Frequency, v)) s.x1 = v;
        if (_table->item(i, 3) && EngEdit::parse(_table->item(i, 3)->text(), unit, kind == XKind::Frequency, v)) s.x2 = v;
        if (_table->item(i, 2) && EngEdit::parse(_table->item(i, 2)->text(), QString(), false, v)) s.y1 = v;
        if (_table->item(i, 4) && EngEdit::parse(_table->item(i, 4)->text(), QString(), false, v)) s.y2 = v;
    }
    _model->changed();
    rebuild();
}

void LimitPanel::updateVerdict()
{
    const TraceResult& r = _model->result(_model->activeIndex());
    if (!r.limitChecked) {
        _verdict->setText(QString());
        return;
    }
    _verdict->setText(r.limitPass ? tr("PASS") : tr("FAIL"));
    _verdict->setStyleSheet(QStringLiteral("color: %1;")
        .arg((r.limitPass ? Theme::Colour::kOk : Theme::Colour::kError).name()));
}
