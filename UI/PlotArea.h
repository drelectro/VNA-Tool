#pragma once

#include <QList>
#include <QPixmap>
#include <QWidget>

class PlotWidget;
class QGridLayout;
class TraceModel;

// The grid of plots. Rebuilds itself when the workspace's plot count changes;
// one plot can be maximised over the rest.
class PlotArea : public QWidget
{
    Q_OBJECT

public:
    explicit PlotArea(TraceModel* model, QWidget* parent = nullptr);

    // Image of one plot, or of the whole grid when plotIndex < 0.
    QPixmap grabPlot(int plotIndex = -1);

signals:
    void stimulusZoomRequested(double start, double stop);
    void contextMenuRequested(int plotIndex, const QPoint& globalPos, double x, bool xValid,
                              int markerTrace, int markerNumber);

public slots:
    void toggleMaximise(int plotIndex);

private:
    void rebuild();
    void applyVisibility();

    TraceModel*  _model = nullptr;
    QGridLayout* _grid = nullptr;
    QList<PlotWidget*> _plots;
    int _maximised = -1;
    int _builtCount = 0;
    int _builtColumns = 0;
};
