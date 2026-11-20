#include "PlotWidget.h"

#include "Theme.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kDivisions = 10;
constexpr int kLegendRow = 16;

const QColor kGrid(0x34, 0x34, 0x4c);
const QColor kGridStrong(0x50, 0x50, 0x70);
const QColor kPlotBg(0x0e, 0x0e, 0x14);

// Next value up or down in the 1-2-5 sequence.
double step125(double v, bool up)
{
    if (!(v > 0.0))
        return 1.0;
    const double p = std::pow(10.0, std::floor(std::log10(v) + 1e-9));
    const double m = v / p;
    if (up)
        return (m < 1.5 ? 2.0 : m < 3.5 ? 5.0 : 10.0) * p;
    return (m > 7.0 ? 5.0 : m > 3.5 ? 2.0 : m > 1.5 ? 1.0 : 0.5) * p;
}

QString yLabel(TraceFormat f, double v)
{
    if (f == TraceFormat::GroupDelay)
        return Formats::engineering(v, QStringLiteral("s"), 4);
    if (std::abs(v) < 1e-12)
        return QStringLiteral("0");
    return QString::number(v, 'g', 5);
}

QString scaleText(const TraceConfig& t)
{
    if (Formats::isChart(t.format))
        return QStringLiteral("full scale %1").arg(t.refValue, 0, 'g', 4);
    const QString unit = Formats::unit(t.format);
    if (t.format == TraceFormat::GroupDelay)
        return QStringLiteral("%1/  Ref %2").arg(Formats::engineering(t.perDiv, unit, 4),
                                                 Formats::engineering(t.refValue, unit, 4));
    return QStringLiteral("%1 %3/  Ref %2 %3").arg(t.perDiv, 0, 'g', 4).arg(t.refValue, 0, 'g', 5).arg(unit);
}

double tdAxisScale(const TraceConfig& t)
{
    if (!t.td.distanceAxis)
        return 1.0;
    const double oneWay = TimeDomain::kSpeedOfLight * t.td.velocityFactor;
    return isReflection(t.param) ? oneWay / 2.0 : oneWay;
}

} // namespace

PlotWidget::PlotWidget(TraceModel* model, int plotIndex, QWidget* parent)
    : QWidget(parent)
    , _model(model)
    , _plot(plotIndex)
{
    setMinimumSize(260, 180);
    setMouseTracking(false);
    setFocusPolicy(Qt::ClickFocus);
    connect(_model, &TraceModel::resultsChanged, this, qOverload<>(&QWidget::update));
    connect(_model, &TraceModel::structureChanged, this, qOverload<>(&QWidget::update));
}

QList<int> PlotWidget::tracesInPlot() const
{
    QList<int> out;
    const Workspace& ws = _model->workspace();
    for (int i = 0; i < ws.traces.size(); ++i)
        if (ws.traces[i].plot == _plot && ws.traces[i].visible)
            out.append(i);
    return out;
}

int PlotWidget::referenceTrace() const
{
    const QList<int> traces = tracesInPlot();
    if (traces.contains(_model->activeIndex()))
        return _model->activeIndex();
    return traces.isEmpty() ? -1 : traces.first();
}

bool PlotWidget::isChartPlot() const
{
    const int ref = referenceTrace();
    return ref >= 0 && Formats::isChart(_model->workspace().traces[ref].format);
}

PlotWidget::XMap PlotWidget::xMapFor(int traceIndex, const XMap& reference) const
{
    const TraceResult& r = _model->result(traceIndex);
    if (r.x.empty())
        return XMap{};
    if (reference.valid && reference.kind == r.xKind)
        return reference;
    XMap m;
    m.min = r.x.front();
    m.max = r.x.back();
    m.kind = r.xKind;
    m.log = r.logX && m.min > 0.0;
    m.valid = m.max > m.min;
    return m;
}

double PlotWidget::xToPixel(const XMap& m, double x) const
{
    double t;
    if (m.log)
        t = std::log(std::max(x, 1e-300) / m.min) / std::log(m.max / m.min);
    else
        t = (x - m.min) / (m.max - m.min);
    return _area.left() + t * _area.width();
}

double PlotWidget::pixelToX(const XMap& m, double px) const
{
    const double t = std::clamp((px - _area.left()) / _area.width(), 0.0, 1.0);
    return m.log ? m.min * std::pow(m.max / m.min, t) : m.min + t * (m.max - m.min);
}

double PlotWidget::yToPixel(const TraceConfig& t, double y) const
{
    const double perDiv = t.perDiv > 0.0 ? t.perDiv : 1.0;
    const double divsFromBottom = (y - t.refValue) / perDiv + t.refPos;
    return _area.bottom() - divsFromBottom * _area.height() / kDivisions;
}

QPointF PlotWidget::chartPoint(const TraceConfig& t, const Cplx& c) const
{
    const double full = t.refValue > 0.0 ? t.refValue : 1.0;
    const double r = 0.5 * _area.width() / full;
    return QPointF(_area.center().x() + c.real() * r, _area.center().y() - c.imag() * r);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void PlotWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), Theme::Colour::kBase);

    const QList<int> traces = tracesInPlot();
    const int ref = referenceTrace();
    const bool chart = isChartPlot();

    QFont f = font();
    f.setPointSizeF(8.0);
    p.setFont(f);

    const int legendRows = std::max(1, int(traces.size()));
    const double top = 6 + legendRows * kLegendRow + 4;
    if (chart) {
        const QRectF avail(8, top, width() - 16, height() - top - 8);
        const double side = std::max(40.0, std::min(avail.width(), avail.height()));
        _area = QRectF(avail.center().x() - side / 2, avail.center().y() - side / 2, side, side);
    } else {
        _area = QRectF(64, top, std::max(40, width() - 64 - 12), std::max(40.0, height() - top - 30));
    }

    _legend.clear();
    if (traces.isEmpty()) {
        p.setPen(Theme::Colour::kMuted);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Plot %1 - no traces").arg(_plot + 1));
    } else if (chart) {
        paintChart(p, traces, ref);
    } else {
        paintRect(p, traces, ref);
    }
    paintLegend(p, traces, chart);
    if (!traces.isEmpty())
        paintReadouts(p, traces, chart);

    // Outline the plot holding the active trace.
    if (traces.contains(_model->activeIndex())) {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(Theme::Colour::kHighlight, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void PlotWidget::paintLegend(QPainter& p, const QList<int>& traces, bool chart)
{
    const Workspace& ws = _model->workspace();
    double y = 6;
    for (const int i : traces) {
        const TraceConfig& t = ws.traces[i];
        const TraceResult& r = _model->result(i);
        const bool drawn = Formats::isChart(t.format) == chart;

        QString text = QStringLiteral("%1%2  %3  %4  %5")
            .arg(i == _model->activeIndex() ? QStringLiteral("▸") : QStringLiteral("  "),
                 t.name(),
                 t.fileIndex >= 0 && t.fileIndex < ws.files.size()
                     ? QStringLiteral("%1 (%2)").arg(sParamName(t.param), ws.files[t.fileIndex].label)
                     : sParamName(t.param),
                 Formats::name(t.format),
                 scaleText(t));

        QStringList flags;
        if (t.math != MathOp::Off) {
            static const char* kOps[] = {"", "D/M", "D-M", "D+M", "D*M"};
            flags << QString::fromLatin1(kOps[int(t.math)]);
        }
        if (t.show == ShowWhat::Memory) flags << QStringLiteral("Mem");
        else if (t.show == ShowWhat::DataAndMemory) flags << QStringLiteral("D&M");
        if (t.smoothing) flags << QStringLiteral("Smo");
        if (t.gate.enabled) flags << (t.gate.notch ? QStringLiteral("Gate-notch") : QStringLiteral("Gate"));
        if (t.td.enabled) flags << TimeDomain::modeName(t.td.mode);
        if (t.fileIndex < 0 && _model->liveSweep().averages > 1)
            flags << QStringLiteral("Avg %1").arg(_model->liveSweep().averages);
        if (!flags.isEmpty())
            text += QStringLiteral("  [%1]").arg(flags.join(QStringLiteral(", ")));
        if (!drawn)
            text += QStringLiteral("  (not shown on this kind of plot)");
        if (r.limitChecked)
            text += r.limitPass ? QStringLiteral("  PASS") : QStringLiteral("  FAIL");

        QColor c = drawn ? t.color : Theme::Colour::kMuted;
        if (r.limitChecked && !r.limitPass && drawn) {
            // Keep the trace colour for the name; flag the failure after it.
        }
        p.setPen(c);
        const QRectF row(8, y, width() - 16, kLegendRow);
        p.drawText(row, Qt::AlignLeft | Qt::AlignVCenter, text);
        if (r.limitChecked) {
            const QString verdict = r.limitPass ? QStringLiteral("PASS") : QStringLiteral("FAIL");
            const int w = p.fontMetrics().horizontalAdvance(text);
            const int vw = p.fontMetrics().horizontalAdvance(verdict);
            p.setPen(r.limitPass ? Theme::Colour::kOk : Theme::Colour::kError);
            p.drawText(QRectF(8 + w - vw, y, vw + 2, kLegendRow), Qt::AlignLeft | Qt::AlignVCenter, verdict);
        }
        _legend.append(LegendHit{row, i});
        y += kLegendRow;
    }
}

void PlotWidget::paintRect(QPainter& p, const QList<int>& traces, int refTrace)
{
    const Workspace& ws = _model->workspace();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(_area, kPlotBg);

    // Reference x mapping from the reference trace.
    _refMap = XMap{};
    if (refTrace >= 0)
        _refMap = xMapFor(refTrace, XMap{});

    // Graticule.
    p.setPen(QPen(kGrid, 1));
    for (int i = 0; i <= kDivisions; ++i) {
        const double y = _area.top() + i * _area.height() / kDivisions;
        p.drawLine(QPointF(_area.left(), y), QPointF(_area.right(), y));
    }
    if (_refMap.valid && _refMap.log) {
        // Decade lines with 2..9 minors.
        const double d0 = std::floor(std::log10(_refMap.min));
        const double d1 = std::ceil(std::log10(_refMap.max));
        for (double d = d0; d <= d1; d += 1.0) {
            for (int m = 1; m <= 9; ++m) {
                const double x = m * std::pow(10.0, d);
                if (x < _refMap.min || x > _refMap.max)
                    continue;
                p.setPen(QPen(m == 1 ? kGridStrong : kGrid, 1));
                const double px = xToPixel(_refMap, x);
                p.drawLine(QPointF(px, _area.top()), QPointF(px, _area.bottom()));
            }
        }
    } else {
        for (int i = 0; i <= kDivisions; ++i) {
            const double x = _area.left() + i * _area.width() / kDivisions;
            p.drawLine(QPointF(x, _area.top()), QPointF(x, _area.bottom()));
        }
    }
    p.setPen(QPen(kGridStrong, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(_area);

    // Axis labels follow the reference trace.
    if (refTrace >= 0) {
        const TraceConfig& rt = ws.traces[refTrace];
        if (!Formats::isChart(rt.format)) {
            p.setPen(rt.color);
            const int every = _area.height() < 260 ? 2 : 1;
            for (int i = 0; i <= kDivisions; i += every) {
                const double v = rt.refValue + (i - rt.refPos) * rt.perDiv;
                const double y = _area.bottom() - i * _area.height() / kDivisions;
                p.drawText(QRectF(0, y - 8, _area.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                           yLabel(rt.format, v));
            }
        }
        if (_refMap.valid) {
            p.setPen(Theme::Colour::kText);
            const QRectF row(_area.left(), _area.bottom() + 6, _area.width(), 16);
            const char* a = _refMap.kind == XKind::Frequency || _refMap.kind == XKind::Power ? "Start " : "";
            const char* b = _refMap.kind == XKind::Frequency || _refMap.kind == XKind::Power ? "Stop " : "";
            p.drawText(row, Qt::AlignLeft | Qt::AlignVCenter,
                       QString::fromLatin1(a) + Formats::xText(_refMap.min, _refMap.kind));
            p.drawText(row, Qt::AlignRight | Qt::AlignVCenter,
                       QString::fromLatin1(b) + Formats::xText(_refMap.max, _refMap.kind));
            QString mid;
            if (_refMap.log)
                mid = QStringLiteral("log");
            else if (_refMap.kind == XKind::Frequency)
                mid = QStringLiteral("Center %1   Span %2")
                          .arg(Formats::xText(0.5 * (_refMap.min + _refMap.max), _refMap.kind),
                               Formats::xText(_refMap.max - _refMap.min, _refMap.kind));
            else
                mid = Formats::xText((_refMap.max - _refMap.min) / kDivisions, _refMap.kind) + QStringLiteral("/div");
            if (_area.width() > 420)
                p.drawText(row, Qt::AlignHCenter | Qt::AlignVCenter, mid);
        }
    }

    p.save();
    p.setClipRect(_area.adjusted(0, 0, 1, 1));
    p.setRenderHint(QPainter::Antialiasing, true);

    auto drawTrace = [&](const TraceConfig& t, const XMap& map, const std::vector<double>& x,
                         const std::vector<double>& y, const QPen& pen) {
        if (x.size() != y.size() || x.size() < 2)
            return;
        QPolygonF poly;
        poly.reserve(qsizetype(x.size()));
        for (size_t i = 0; i < x.size(); ++i) {
            if (map.log && x[i] <= 0.0)
                continue;
            // Clamp far-off-scale values so the polyline stays well-formed.
            const double py = std::clamp(yToPixel(t, y[i]), _area.top() - 4000.0, _area.bottom() + 4000.0);
            poly.append(QPointF(xToPixel(map, x[i]), py));
        }
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(poly);
    };

    // Inactive traces first, so the active one is drawn on top.
    QList<int> order = traces;
    if (order.removeOne(_model->activeIndex()))
        order.append(_model->activeIndex());

    for (const int i : order) {
        const TraceConfig& t = ws.traces[i];
        const TraceResult& r = _model->result(i);
        if (Formats::isChart(t.format) || !r.valid)
            continue;
        const XMap map = xMapFor(i, _refMap);
        if (!map.valid)
            continue;

        // Limit lines.
        if (t.limitTest) {
            for (const LimitSegment& s : t.limits) {
                p.setPen(QPen(s.upper ? QColor(0xe8, 0x80, 0x80) : QColor(0x80, 0xb0, 0xe8), 1.5, Qt::DashLine));
                p.drawLine(QPointF(xToPixel(map, s.x1), yToPixel(t, s.y1)),
                           QPointF(xToPixel(map, s.x2), yToPixel(t, s.y2)));
            }
        }

        if (t.show != ShowWhat::Data && !r.memY.empty()) {
            QColor mc = t.color.darker(170);
            drawTrace(t, map, r.x, r.memY, QPen(mc, 1.2));
        }
        if (t.show != ShowWhat::Memory)
            drawTrace(t, map, r.x, r.y, QPen(t.color, i == _model->activeIndex() ? 1.7 : 1.3));

        // Points that fail a limit.
        if (r.limitChecked && !r.limitPass) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::Colour::kError);
            for (size_t k = 0; k < r.limitFail.size(); ++k)
                if (r.limitFail[k])
                    p.drawEllipse(QPointF(xToPixel(map, r.x[k]), yToPixel(t, r.y[k])), 1.6, 1.6);
        }

        // Bandwidth edges.
        if (r.bw.valid) {
            p.setPen(QPen(t.color, 1, Qt::DotLine));
            for (const double x : {r.bw.low, r.bw.high}) {
                const double px = xToPixel(map, x);
                p.drawLine(QPointF(px, _area.top()), QPointF(px, _area.bottom()));
            }
        }

        // Time gate span.
        if (t.gate.enabled && t.td.enabled) {
            const double scale = tdAxisScale(t);
            const double a = xToPixel(map, t.gate.start * scale);
            const double b = xToPixel(map, t.gate.stop * scale);
            QColor shade = t.color;
            shade.setAlpha(28);
            p.fillRect(QRectF(QPointF(a, _area.top()), QPointF(b, _area.bottom())), shade);
        }

        for (const MarkerReadout& m : r.markers)
            paintMarker(p, QPointF(xToPixel(map, m.x), yToPixel(t, m.y)), m.number, t.color,
                        i == _model->activeIndex());
    }
    p.restore();

    // Reference-position ticks on the left edge.
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const int i : traces) {
        const TraceConfig& t = ws.traces[i];
        if (Formats::isChart(t.format))
            continue;
        const double y = _area.bottom() - t.refPos * _area.height() / kDivisions;
        QPolygonF tri;
        tri << QPointF(_area.left() - 5, y - 4) << QPointF(_area.left(), y) << QPointF(_area.left() - 5, y + 4);
        p.setPen(Qt::NoPen);
        p.setBrush(t.color);
        p.drawPolygon(tri);
    }

    // Zoom rubber band.
    if (_zooming) {
        QColor band = Theme::Colour::kHighlight;
        band.setAlpha(70);
        const double a = std::clamp(_zoomFrom.x(), _area.left(), _area.right());
        const double b = std::clamp(_zoomTo.x(), _area.left(), _area.right());
        p.fillRect(QRectF(QPointF(std::min(a, b), _area.top()), QPointF(std::max(a, b), _area.bottom())), band);
    }
}

void PlotWidget::paintChart(QPainter& p, const QList<int>& traces, int refTrace)
{
    const Workspace& ws = _model->workspace();
    const TraceConfig& rt = ws.traces[refTrace];
    const bool smith = rt.format == TraceFormat::Smith;
    const double full = rt.refValue > 0.0 ? rt.refValue : 1.0;
    const double unit = 0.5 * _area.width() / full;         // pixels per |gamma| = 1
    const QPointF c = _area.center();
    _refMap = XMap{};

    p.setRenderHint(QPainter::Antialiasing, true);
    p.save();

    QPainterPath clip;
    if (smith) {
        // The grid exists only inside the unit circle; zoomed in, the plot
        // square crops it further.
        QPainterPath circle;
        circle.addEllipse(c, unit, unit);
        QPainterPath box;
        box.addRect(_area);
        clip = circle.intersected(box);
    } else {
        clip.addEllipse(c, 0.5 * _area.width(), 0.5 * _area.width());
    }
    p.fillPath(clip, kPlotBg);
    p.setClipPath(clip);

    p.setBrush(Qt::NoBrush);
    if (smith) {
        for (const double r : {0.0, 0.2, 0.5, 1.0, 2.0, 5.0}) {
            p.setPen(QPen(r == 1.0 || r == 0.0 ? kGridStrong : kGrid, 1));
            const double radius = unit / (1.0 + r);
            p.drawEllipse(QPointF(c.x() + unit * r / (1.0 + r), c.y()), radius, radius);
        }
        p.setPen(QPen(kGrid, 1));
        for (const double x : {0.2, 0.5, 1.0, 2.0, 5.0}) {
            const double radius = unit / x;
            p.drawEllipse(QPointF(c.x() + unit, c.y() - radius), radius, radius);
            p.drawEllipse(QPointF(c.x() + unit, c.y() + radius), radius, radius);
        }
        p.setPen(QPen(kGridStrong, 1));
        p.drawLine(QPointF(c.x() - unit, c.y()), QPointF(c.x() + unit, c.y()));
    } else {
        const double outer = 0.5 * _area.width();
        for (int i = 1; i <= 5; ++i) {
            p.setPen(QPen(i == 5 ? kGridStrong : kGrid, 1));
            p.drawEllipse(c, outer * i / 5.0, outer * i / 5.0);
        }
        p.setPen(QPen(kGrid, 1));
        for (int a = 0; a < 180; a += 30) {
            const double rad = a * M_PI / 180.0;
            const QPointF d(std::cos(rad) * outer, -std::sin(rad) * outer);
            p.drawLine(c - d, c + d);
        }
    }

    QList<int> order = traces;
    if (order.removeOne(_model->activeIndex()))
        order.append(_model->activeIndex());

    for (const int i : order) {
        const TraceConfig& t = ws.traces[i];
        const TraceResult& r = _model->result(i);
        if (!Formats::isChart(t.format) || !r.valid)
            continue;

        // Every chart trace is drawn on the reference trace's scale, so they
        // can be compared; the reference trace's own scale is the plot's.
        auto drawComplex = [&](const std::vector<Cplx>& values, const QPen& pen) {
            if (values.size() < 2)
                return;
            QPolygonF poly;
            poly.reserve(qsizetype(values.size()));
            for (const Cplx& v : values)
                poly.append(chartPoint(rt, v));
            p.setPen(pen);
            p.drawPolyline(poly);
        };
        if (t.show != ShowWhat::Data)
            drawComplex(r.memC, QPen(t.color.darker(170), 1.2));
        if (t.show != ShowWhat::Memory)
            drawComplex(r.c, QPen(t.color, i == _model->activeIndex() ? 1.7 : 1.3));
    }
    p.restore();

    // Scale labels.
    p.setPen(Theme::Colour::kMuted);
    if (smith) {
        for (const double r : {0.2, 0.5, 1.0, 2.0, 5.0}) {
            const double gx = (r - 1.0) / (r + 1.0);
            const QPointF at(c.x() + gx * unit, c.y());
            if (_area.contains(at))
                p.drawText(QRectF(at.x() - 20, at.y() + 1, 40, 12), Qt::AlignCenter,
                           QString::number(r * ws.z0, 'g', 3));
        }
    } else {
        for (int i = 1; i <= 5; ++i)
            p.drawText(QRectF(c.x() + 0.5 * _area.width() * i / 5.0 - 30, c.y() + 1, 28, 12),
                       Qt::AlignRight | Qt::AlignVCenter, QString::number(full * i / 5.0, 'g', 3));
    }

    // Markers go on top, unclipped, so their labels stay readable at the rim.
    for (const int i : order) {
        const TraceConfig& t = ws.traces[i];
        const TraceResult& r = _model->result(i);
        if (!Formats::isChart(t.format) || !r.valid)
            continue;
        for (const MarkerReadout& m : r.markers) {
            const QPointF at = chartPoint(rt, m.c);
            if (_area.adjusted(-2, -2, 2, 2).contains(at))
                paintMarker(p, at, m.number, t.color, i == _model->activeIndex());
        }
    }
}

void PlotWidget::paintMarker(QPainter& p, const QPointF& at, int number, const QColor& colour, bool active)
{
    QPolygonF tri;
    tri << at << QPointF(at.x() - 5, at.y() - 9) << QPointF(at.x() + 5, at.y() - 9);
    p.setPen(QPen(kPlotBg, 1));
    p.setBrush(active ? colour : colour.darker(140));
    p.drawPolygon(tri);
    p.setPen(colour);
    p.drawText(QRectF(at.x() - 12, at.y() - 23, 24, 12), Qt::AlignCenter, QString::number(number));
}

void PlotWidget::paintReadouts(QPainter& p, const QList<int>& traces, bool chart)
{
    const Workspace& ws = _model->workspace();
    struct Line { QColor colour; QString text; };
    QList<Line> lines;

    QList<int> order = traces;
    if (order.removeOne(_model->activeIndex()))
        order.prepend(_model->activeIndex());

    int tracesWithMarkers = 0;
    for (const int i : order)
        if (!_model->result(i).markers.isEmpty())
            ++tracesWithMarkers;

    QStringList notes;
    for (const int i : order) {
        const TraceConfig& t = ws.traces[i];
        const TraceResult& r = _model->result(i);
        if (!r.note.isEmpty())
            notes << QStringLiteral("%1: %2").arg(t.name(), r.note);
        if (Formats::isChart(t.format) != chart)
            continue;
        for (const MarkerReadout& m : r.markers)
            lines.append(Line{t.color, tracesWithMarkers > 1 ? t.name() + QStringLiteral("  ") + m.text : m.text});
        if (t.bw.enabled) {
            if (r.bw.valid) {
                lines.append(Line{t.color, QStringLiteral("BW %1   Center %2")
                    .arg(Formats::xText(r.bw.bandwidth, r.xKind), Formats::xText(r.bw.center, r.xKind))});
                lines.append(Line{t.color, QStringLiteral("Low %1   High %2")
                    .arg(Formats::xText(r.bw.low, r.xKind), Formats::xText(r.bw.high, r.xKind))});
                lines.append(Line{t.color, QStringLiteral("Q %1   %2 %3 %4")
                    .arg(r.bw.q, 0, 'f', 2)
                    .arg(t.bw.notch ? QStringLiteral("Min") : QStringLiteral("Loss"))
                    .arg(r.bw.loss, 0, 'f', 3)
                    .arg(Formats::unit(t.format))});
            } else {
                lines.append(Line{t.color, QStringLiteral("BW: no %1 dB points found").arg(t.bw.level, 0, 'g', 3)});
            }
        }
    }

    const QFontMetrics fm = p.fontMetrics();
    int w = 0;
    for (const Line& l : lines)
        w = std::max(w, fm.horizontalAdvance(l.text));

    if (!lines.isEmpty()) {
        const QRectF box(_area.right() - w - 14, _area.top() + 4, w + 10, lines.size() * 14 + 6);
        QColor bg = kPlotBg;
        bg.setAlpha(200);
        p.fillRect(box, bg);
        double y = box.top() + 3;
        for (const Line& l : lines) {
            p.setPen(l.colour);
            p.drawText(QRectF(box.left() + 5, y, w + 2, 14), Qt::AlignLeft | Qt::AlignVCenter, l.text);
            y += 14;
        }
    }

    if (!notes.isEmpty()) {
        p.setPen(Theme::Colour::kWarn);
        p.drawText(_area.adjusted(6, 0, -6, -4), Qt::AlignLeft | Qt::AlignBottom, notes.join(QStringLiteral("   ")));
    }
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

bool PlotWidget::hitMarker(const QPointF& pos, int& traceOut, int& numberOut) const
{
    const Workspace& ws = _model->workspace();
    const bool chart = isChartPlot();
    const int ref = referenceTrace();
    double best = 14.0;
    bool found = false;

    for (const int i : tracesInPlot()) {
        const TraceConfig& t = ws.traces[i];
        const TraceResult& r = _model->result(i);
        if (Formats::isChart(t.format) != chart || !r.valid)
            continue;
        const XMap map = chart ? XMap{} : xMapFor(i, _refMap);
        for (const MarkerReadout& m : r.markers) {
            const QPointF at = chart ? chartPoint(ws.traces[ref], m.c)
                                     : QPointF(xToPixel(map, m.x), yToPixel(t, m.y));
            // The triangle sits above the point, so aim at its middle.
            const double d = QLineF(pos, at - QPointF(0, 5)).length();
            // Prefer the active trace when two markers coincide.
            const double score = d - (i == _model->activeIndex() ? 2.0 : 0.0);
            if (score < best) {
                best = score;
                traceOut = i;
                numberOut = m.number;
                found = true;
            }
        }
    }
    return found;
}

double PlotWidget::stimulusAt(int traceIndex, const QPointF& pos, bool& ok) const
{
    ok = false;
    const Workspace& ws = _model->workspace();
    if (traceIndex < 0 || traceIndex >= ws.traces.size())
        return 0.0;
    const TraceConfig& t = ws.traces[traceIndex];
    const TraceResult& r = _model->result(traceIndex);
    if (!r.valid || r.x.empty())
        return 0.0;

    if (Formats::isChart(t.format)) {
        // No stimulus axis on a chart: take the nearest drawn point.
        const int ref = referenceTrace();
        if (ref < 0)
            return 0.0;
        double best = -1.0;
        size_t idx = 0;
        for (size_t i = 0; i < r.c.size(); ++i) {
            const double d = QLineF(pos, chartPoint(ws.traces[ref], r.c[i])).length();
            if (best < 0.0 || d < best) {
                best = d;
                idx = i;
            }
        }
        ok = true;
        return r.x[idx];
    }

    const XMap map = xMapFor(traceIndex, _refMap);
    if (!map.valid)
        return 0.0;
    ok = true;
    return pixelToX(map, pos.x());
}

void PlotWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    const QPointF pos = e->position();

    for (const LegendHit& h : _legend) {
        if (h.rect.contains(pos)) {
            _model->setActiveTrace(h.trace);
            return;
        }
    }

    int trace = -1, number = 0;
    if (hitMarker(pos, trace, number)) {
        _dragTrace = trace;
        _dragMarker = number;
        _model->setActiveTrace(trace);
        return;
    }

    // Clicking a plot that does not hold the active trace selects into it.
    const QList<int> traces = tracesInPlot();
    if (!traces.isEmpty() && !traces.contains(_model->activeIndex()))
        _model->setActiveTrace(traces.first());

    if ((e->modifiers() & Qt::ShiftModifier) && !isChartPlot() && _refMap.valid && _area.contains(pos)) {
        _zooming = true;
        _zoomFrom = _zoomTo = pos;
        update();
    }
}

void PlotWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (_dragTrace >= 0) {
        bool ok = false;
        const double x = stimulusAt(_dragTrace, e->position(), ok);
        if (ok)
            _model->moveMarker(_dragTrace, _dragMarker, x);
    } else if (_zooming) {
        _zoomTo = e->position();
        update();
    }
}

void PlotWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    _dragTrace = -1;

    if (_zooming) {
        _zooming = false;
        const double a = pixelToX(_refMap, std::min(_zoomFrom.x(), _zoomTo.x()));
        const double b = pixelToX(_refMap, std::max(_zoomFrom.x(), _zoomTo.x()));
        const int ref = referenceTrace();
        if (ref >= 0 && std::abs(_zoomTo.x() - _zoomFrom.x()) > 6 && b > a) {
            TraceConfig& t = _model->edit().traces[ref];
            if (t.td.enabled && _refMap.kind != XKind::Frequency) {
                // A time-domain trace zooms its own window.
                const double scale = tdAxisScale(t);
                t.td.start = a / scale;
                t.td.stop = b / scale;
                _model->notifyStructureChanged();
            } else if (t.fileIndex < 0 && _refMap.kind == XKind::Frequency) {
                emit stimulusZoomRequested(a, b);
            }
        }
        update();
    }
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        emit maximiseToggled(_plot);
}

void PlotWidget::wheelEvent(QWheelEvent* e)
{
    const QList<int> traces = tracesInPlot();
    int idx = traces.contains(_model->activeIndex()) ? _model->activeIndex() : referenceTrace();
    if (idx < 0 || e->angleDelta().y() == 0) {
        e->ignore();
        return;
    }
    TraceConfig& t = _model->edit().traces[idx];
    const bool up = e->angleDelta().y() > 0;

    if (Formats::isChart(t.format)) {
        // Wheel up zooms in, i.e. a smaller full-scale value.
        t.refValue = std::clamp(step125(t.refValue, !up), 0.01, 100.0);
    } else if (e->modifiers() & Qt::ShiftModifier) {
        t.refValue += up ? t.perDiv : -t.perDiv;
    } else {
        t.perDiv = step125(t.perDiv, !up);
    }
    _model->notifyStructureChanged();    // the trace panel shows the scale too
    e->accept();
}

void PlotWidget::contextMenuEvent(QContextMenuEvent* e)
{
    // Look for a marker first: making its trace active below changes nothing
    // about where markers are drawn, but do it in this order to be safe.
    int markerTrace = -1, markerNumber = 0;
    if (!hitMarker(e->pos(), markerTrace, markerNumber))
        markerTrace = -1;

    const QList<int> traces = tracesInPlot();
    if (markerTrace >= 0)
        _model->setActiveTrace(markerTrace);
    else if (!traces.isEmpty() && !traces.contains(_model->activeIndex()))
        _model->setActiveTrace(traces.first());

    bool ok = false;
    const double x = stimulusAt(_model->activeIndex(), e->pos(), ok);
    emit contextMenuRequested(_plot, e->globalPos(), x, ok && traces.contains(_model->activeIndex()),
                              markerTrace, markerNumber);
}
