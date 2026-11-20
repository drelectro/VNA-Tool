#pragma once

#include "Data/TraceModel.h"

#include <QList>
#include <QRectF>
#include <QWidget>

// One plot: every trace assigned to it, drawn from the model's results.
//
// A plot is rectangular (stimulus across, one scale per trace) or a chart
// (Smith / polar), decided by the active trace when it is in this plot and by
// the first trace otherwise. Traces of the other kind stay in the legend,
// greyed, rather than being drawn on axes that mean something else.
//
// Scale is per trace, VNA-style: ten divisions, a value per division, and a
// reference value pinned to one graticule line. The axis labels follow the
// plot's reference trace and are drawn in its colour.
//
// Mouse:
//   click legend entry     make that trace active
//   drag a marker          move it
//   wheel                  scale per division of the active trace
//   Shift + wheel          reference value
//   Shift + drag           zoom: sets the instrument's start/stop (or the
//                          time-domain window) to the dragged span
//   double-click           maximise / restore the plot
//   right-click            menu

class PlotWidget : public QWidget
{
    Q_OBJECT

public:
    PlotWidget(TraceModel* model, int plotIndex, QWidget* parent = nullptr);

    int plotIndex() const { return _plot; }

signals:
    void maximiseToggled(int plotIndex);
    // The operator dragged a span on a live frequency trace.
    void stimulusZoomRequested(double start, double stop);
    // markerTrace is -1 unless the click was on a marker.
    void contextMenuRequested(int plotIndex, const QPoint& globalPos, double xAtCursor, bool xValid,
                              int markerTrace, int markerNumber);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    struct XMap
    {
        double min = 0, max = 1;
        bool   log = false;
        XKind  kind = XKind::Frequency;
        bool   valid = false;
    };

    QList<int> tracesInPlot() const;
    int    referenceTrace() const;          // index into the model, or -1
    bool   isChartPlot() const;
    XMap   xMapFor(int traceIndex, const XMap& reference) const;
    double xToPixel(const XMap& m, double x) const;
    double pixelToX(const XMap& m, double px) const;
    double yToPixel(const TraceConfig& t, double y) const;
    QPointF chartPoint(const TraceConfig& t, const Cplx& c) const;

    void paintRect(QPainter& p, const QList<int>& traces, int refTrace);
    void paintChart(QPainter& p, const QList<int>& traces, int refTrace);
    void paintLegend(QPainter& p, const QList<int>& traces, bool chart);
    void paintReadouts(QPainter& p, const QList<int>& traces, bool chart);
    void paintMarker(QPainter& p, const QPointF& at, int number, const QColor& colour, bool active);

    // Finds the marker under `pos`. Returns false if there is none.
    bool hitMarker(const QPointF& pos, int& traceOut, int& numberOut) const;
    double stimulusAt(int traceIndex, const QPointF& pos, bool& ok) const;

    TraceModel* _model = nullptr;
    int    _plot = 0;
    QRectF _area;                           // graticule (rect) or chart bounds
    XMap   _refMap;

    struct LegendHit { QRectF rect; int trace; };
    QList<LegendHit> _legend;

    int  _dragTrace = -1, _dragMarker = 0;
    bool _zooming = false;
    QPointF _zoomFrom, _zoomTo;
};
