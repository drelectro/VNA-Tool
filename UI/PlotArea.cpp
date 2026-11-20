#include "PlotArea.h"

#include "Data/TraceModel.h"
#include "PlotWidget.h"

#include <QGridLayout>

PlotArea::PlotArea(TraceModel* model, QWidget* parent)
    : QWidget(parent)
    , _model(model)
{
    _grid = new QGridLayout(this);
    _grid->setContentsMargins(2, 2, 2, 2);
    _grid->setSpacing(3);
    connect(_model, &TraceModel::structureChanged, this, &PlotArea::rebuild);
    rebuild();
}

void PlotArea::rebuild()
{
    const Workspace& ws = _model->workspace();
    const int count = std::max(1, ws.plotCount);
    const int columns = std::clamp(ws.plotColumns, 1, 4);
    if (count == _builtCount && columns == _builtColumns)
        return;     // structureChanged also fires for trace edits

    qDeleteAll(_plots);
    _plots.clear();
    // Stale stretch factors would keep empty rows and columns open.
    for (int i = 0; i < 16; ++i) {
        _grid->setRowStretch(i, 0);
        _grid->setColumnStretch(i, 0);
    }

    for (int i = 0; i < count; ++i) {
        auto* plot = new PlotWidget(_model, i, this);
        connect(plot, &PlotWidget::maximiseToggled, this, &PlotArea::toggleMaximise);
        connect(plot, &PlotWidget::stimulusZoomRequested, this, &PlotArea::stimulusZoomRequested);
        connect(plot, &PlotWidget::contextMenuRequested, this, &PlotArea::contextMenuRequested);
        _grid->addWidget(plot, i / columns, i % columns);
        _grid->setRowStretch(i / columns, 1);
        _grid->setColumnStretch(i % columns, 1);
        _plots.append(plot);
    }
    _builtCount = count;
    _builtColumns = columns;
    if (_maximised >= count)
        _maximised = -1;
    applyVisibility();
}

void PlotArea::applyVisibility()
{
    for (PlotWidget* p : _plots)
        p->setVisible(_maximised < 0 || p->plotIndex() == _maximised);
}

void PlotArea::toggleMaximise(int plotIndex)
{
    if (_plots.size() < 2)
        return;
    _maximised = _maximised == plotIndex ? -1 : plotIndex;
    applyVisibility();
}

QPixmap PlotArea::grabPlot(int plotIndex)
{
    if (plotIndex >= 0 && plotIndex < _plots.size())
        return _plots[plotIndex]->grab();
    return grab();
}
