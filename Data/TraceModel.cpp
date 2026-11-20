#include "TraceModel.h"

#include "TracePipeline.h"

#include <algorithm>
#include <cmath>

QString TraceConfig::name() const
{
    return QStringLiteral("Tr%1").arg(id);
}

QColor TraceModel::defaultColor(int n)
{
    static const QColor k[] = {
        QColor(0xf2, 0xd0, 0x4a), QColor(0x4a, 0xc8, 0xf2), QColor(0xf2, 0x7a, 0xb0),
        QColor(0x7a, 0xe8, 0x7a), QColor(0xf2, 0x9a, 0x4a), QColor(0xb0, 0x90, 0xf2),
        QColor(0x4a, 0xe8, 0xc8), QColor(0xe8, 0x70, 0x70),
    };
    return k[size_t(n) % (sizeof k / sizeof k[0])];
}

TraceModel::TraceModel(QObject* parent)
    : QObject(parent)
{
}

void TraceModel::setWorkspace(const Workspace& ws)
{
    _ws = ws;
    _ws.plotCount = std::max(1, _ws.plotCount);
    _ws.activeTrace = std::clamp(_ws.activeTrace, 0, std::max(0, traceCount() - 1));
    recompute();
    emit structureChanged();
    emit resultsChanged();
}

const SweepData* TraceModel::sourceSweep(const TraceConfig& t) const
{
    if (t.fileIndex < 0)
        return &_live;
    if (t.fileIndex < _ws.files.size())
        return &_ws.files[t.fileIndex];
    return nullptr;
}

const TraceResult& TraceModel::result(int traceIndex) const
{
    static const TraceResult kEmpty;
    return (traceIndex >= 0 && traceIndex < _results.size()) ? _results[traceIndex] : kEmpty;
}

TraceConfig* TraceModel::activeTrace()
{
    return (_ws.activeTrace >= 0 && _ws.activeTrace < traceCount()) ? &_ws.traces[_ws.activeTrace] : nullptr;
}

void TraceModel::setActiveTrace(int index)
{
    if (index < 0 || index >= traceCount() || index == _ws.activeTrace)
        return;
    _ws.activeTrace = index;
    emit structureChanged();
    emit resultsChanged();
}

quint32 TraceModel::neededMask() const
{
    quint32 mask = 0;
    for (const TraceConfig& t : _ws.traces)
        if (t.fileIndex < 0)
            mask |= 1u << static_cast<int>(t.param);
    return mask;
}

int TraceModel::addTrace(SParam param, TraceFormat format, int plot, int fileIndex)
{
    TraceConfig t;
    t.id = _ws.nextTraceId++;
    t.param = param;
    t.format = format;
    t.plot = std::clamp(plot, 0, _ws.plotCount - 1);
    t.fileIndex = fileIndex;
    t.color = defaultColor(t.id - 1);
    Formats::defaultScale(format, t.perDiv, t.refValue, t.refPos);
    _ws.traces.append(t);
    _ws.activeTrace = traceCount() - 1;
    recompute();
    emit structureChanged();
    emit resultsChanged();
    return _ws.activeTrace;
}

void TraceModel::removeTrace(int index)
{
    if (index < 0 || index >= traceCount())
        return;
    _ws.traces.removeAt(index);
    _ws.activeTrace = std::clamp(_ws.activeTrace, 0, std::max(0, traceCount() - 1));
    recompute();
    emit structureChanged();
    emit resultsChanged();
}

void TraceModel::setFormat(int index, TraceFormat format)
{
    if (index < 0 || index >= traceCount())
        return;
    TraceConfig& t = _ws.traces[index];
    if (t.format == format)
        return;
    t.format = format;
    Formats::defaultScale(format, t.perDiv, t.refValue, t.refPos);
    recompute();
    autoScale(index);
}

void TraceModel::setPlotCount(int count)
{
    count = std::clamp(count, 1, 16);
    _ws.plotCount = count;
    // Traces go with the plot they were in. Moving them into the plots that
    // remain would pile unrelated traces onto axes chosen for something else.
    _ws.traces.removeIf([count](const TraceConfig& t) { return t.plot >= count; });
    _ws.activeTrace = std::clamp(_ws.activeTrace, 0, std::max(0, traceCount() - 1));
    recompute();
    emit structureChanged();
    emit resultsChanged();
}

void TraceModel::removePlot(int index)
{
    if (index < 0 || index >= _ws.plotCount || _ws.plotCount <= 1)
        return;
    _ws.traces.removeIf([index](const TraceConfig& t) { return t.plot == index; });
    for (TraceConfig& t : _ws.traces)
        if (t.plot > index)
            --t.plot;
    --_ws.plotCount;
    _ws.activeTrace = std::clamp(_ws.activeTrace, 0, std::max(0, traceCount() - 1));
    recompute();
    emit structureChanged();
    emit resultsChanged();
}

namespace {
// Rounds up to 1, 2 or 5 times a power of ten.
double niceStep(double raw)
{
    if (!(raw > 0.0) || !std::isfinite(raw))
        return 1.0;
    const double p = std::pow(10.0, std::floor(std::log10(raw)));
    const double m = raw / p;
    return (m <= 1.0 ? 1.0 : m <= 2.0 ? 2.0 : m <= 5.0 ? 5.0 : 10.0) * p;
}
} // namespace

void TraceModel::autoScale(int index)
{
    if (index < 0 || index >= traceCount())
        return;
    TraceConfig& t = _ws.traces[index];
    const TraceResult& r = result(index);
    if (!r.valid || r.y.empty()) {
        emit resultsChanged();
        return;
    }

    if (Formats::isChart(t.format)) {
        double peak = 0.0;
        for (const Cplx& c : r.c)
            peak = std::max(peak, std::abs(c));
        // A chart's outer circle is refValue; 1.0 is the natural full scale.
        t.refValue = peak <= 1.0 ? 1.0 : niceStep(peak);
    } else {
        auto [lo, hi] = std::minmax_element(r.y.begin(), r.y.end());
        double ymin = *lo, ymax = *hi;
        for (const double v : r.memY) {
            ymin = std::min(ymin, v);
            ymax = std::max(ymax, v);
        }
        const double span = std::max(ymax - ymin, 1e-12);
        t.perDiv = niceStep(span / 8.0);
        t.refPos = 5;
        // Reference on a multiple of the step nearest the middle of the data.
        t.refValue = std::round(0.5 * (ymin + ymax) / t.perDiv) * t.perDiv;
    }
    emit structureChanged();    // panels show the scale
    emit resultsChanged();
}

void TraceModel::autoScaleAll()
{
    for (int i = 0; i < traceCount(); ++i)
        autoScale(i);
}

void TraceModel::dataToMemory(int index)
{
    if (index < 0 || index >= traceCount())
        return;
    TraceConfig& t = _ws.traces[index];
    if (!TracePipeline::memorySource(t, _ws, sourceSweep(t), t.memoryX, t.memory))
        return;
    if (t.show == ShowWhat::Data)
        t.show = ShowWhat::DataAndMemory;
    changed();
}

int TraceModel::addFile(const SweepData& sweep)
{
    _ws.files.append(sweep);
    return int(_ws.files.size()) - 1;
}

// ---------------------------------------------------------------------------
// Markers
// ---------------------------------------------------------------------------

Marker* TraceModel::findMarker(int traceIndex, int number)
{
    if (traceIndex < 0 || traceIndex >= traceCount())
        return nullptr;
    for (Marker& m : _ws.traces[traceIndex].markers)
        if (m.number == number)
            return &m;
    return nullptr;
}

int TraceModel::addMarker(int traceIndex, double x)
{
    if (traceIndex < 0 || traceIndex >= traceCount())
        return 0;
    TraceConfig& t = _ws.traces[traceIndex];
    int number = 1;
    while (std::any_of(t.markers.begin(), t.markers.end(), [number](const Marker& m) { return m.number == number; }))
        ++number;
    Marker m;
    m.number = number;
    m.x = x;
    t.markers.append(m);
    std::sort(t.markers.begin(), t.markers.end(), [](const Marker& a, const Marker& b) { return a.number < b.number; });
    changed();
    return number;
}

void TraceModel::removeMarker(int traceIndex, int number)
{
    if (traceIndex < 0 || traceIndex >= traceCount())
        return;
    TraceConfig& t = _ws.traces[traceIndex];
    t.markers.removeIf([number](const Marker& m) { return m.number == number; });
    if (t.deltaRef == number)
        t.deltaRef = 0;
    changed();
}

void TraceModel::clearMarkers(int traceIndex)
{
    for (int i = 0; i < traceCount(); ++i) {
        if (traceIndex >= 0 && i != traceIndex)
            continue;
        _ws.traces[i].markers.clear();
        _ws.traces[i].deltaRef = 0;
    }
    changed();
}

void TraceModel::applyCoupling(int traceIndex, int number, double x)
{
    if (!_ws.coupledMarkers)
        return;
    const XKind kind = result(traceIndex).xKind;
    for (int i = 0; i < traceCount(); ++i) {
        if (i == traceIndex || result(i).xKind != kind)
            continue;
        if (Marker* m = findMarker(i, number)) {
            m->x = x;
            m->track = MarkerTrack::None;
        }
    }
}

void TraceModel::moveMarker(int traceIndex, int number, double x)
{
    Marker* m = findMarker(traceIndex, number);
    if (!m)
        return;
    const TraceResult& r = result(traceIndex);
    if (!r.x.empty())
        x = std::clamp(x, r.x.front(), r.x.back());
    m->x = x;
    m->track = MarkerTrack::None;       // dragging a marker takes it off tracking
    applyCoupling(traceIndex, number, x);
    changed();
}

bool TraceModel::searchMarker(int traceIndex, int number, Search kind, double target)
{
    Marker* m = findMarker(traceIndex, number);
    const TraceResult& r = result(traceIndex);
    if (!m || !r.valid || r.y.empty())
        return false;

    const bool chart = Formats::isChart(_ws.traces[traceIndex].format);
    // Charts are searched on magnitude.
    std::vector<double> mag;
    if (chart) {
        mag.reserve(r.c.size());
        for (const Cplx& c : r.c)
            mag.push_back(std::abs(c));
    }
    const std::vector<double>& y = chart ? mag : r.y;

    const int here = MarkerSearch::nearestIndex(r.x, m->x);
    double found = m->x;
    bool ok = false;

    switch (kind) {
    case Search::Max:
        found = r.x[size_t(MarkerSearch::maxIndex(y))];
        ok = true;
        break;
    case Search::Min:
        found = r.x[size_t(MarkerSearch::minIndex(y))];
        ok = true;
        break;
    case Search::PeakLeft:
    case Search::PeakRight: {
        const int p = MarkerSearch::nextPeak(y, here, kind == Search::PeakLeft ? -1 : +1, _ws.peakProminence);
        if (p >= 0) {
            found = r.x[size_t(p)];
            ok = true;
        }
        break;
    }
    case Search::TargetLeft:
    case Search::TargetRight: {
        const std::vector<double> xs = MarkerSearch::crossings(r.x, y, target);
        const double eps = 1e-9 * std::max(1.0, std::abs(m->x));
        if (kind == Search::TargetLeft) {
            for (auto it = xs.rbegin(); it != xs.rend(); ++it)
                if (*it < m->x - eps) { found = *it; ok = true; break; }
        } else {
            for (const double v : xs)
                if (v > m->x + eps) { found = v; ok = true; break; }
        }
        break;
    }
    }

    if (ok) {
        m->x = found;
        applyCoupling(traceIndex, number, found);
        changed();
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Recompute
// ---------------------------------------------------------------------------

void TraceModel::setLiveSweep(const SweepData& sweep)
{
    _live = sweep;
    recompute();
    emit resultsChanged();
}

void TraceModel::changed()
{
    recompute();
    emit resultsChanged();
}

void TraceModel::notifyStructureChanged()
{
    recompute();
    emit structureChanged();
    emit resultsChanged();
}

void TraceModel::recompute()
{
    _results.resize(traceCount());
    for (int i = 0; i < traceCount(); ++i) {
        TraceConfig& t = _ws.traces[i];
        _results[i] = TracePipeline::compute(t, _ws, sourceSweep(t));

        // Tracking markers follow the data: move them, then re-read values.
        TraceResult& r = _results[i];
        if (!r.valid || r.y.empty())
            continue;
        bool moved = false;
        for (Marker& m : t.markers) {
            if (m.track == MarkerTrack::None || Formats::isChart(t.format))
                continue;
            if (m.track == MarkerTrack::Max) {
                m.x = r.x[size_t(MarkerSearch::maxIndex(r.y))];
            } else if (m.track == MarkerTrack::Min) {
                m.x = r.x[size_t(MarkerSearch::minIndex(r.y))];
            } else {
                // Nearest crossing of the target to where the marker is now.
                const std::vector<double> xs = MarkerSearch::crossings(r.x, r.y, m.target);
                double best = m.x, bestDist = -1.0;
                for (const double v : xs) {
                    const double d = std::abs(v - m.x);
                    if (bestDist < 0.0 || d < bestDist) { best = v; bestDist = d; }
                }
                m.x = best;
            }
            moved = true;
        }
        if (moved)
            TracePipeline::evaluateMarkers(t, _ws, r);
    }
}
