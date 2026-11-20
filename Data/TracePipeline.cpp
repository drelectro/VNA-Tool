#include "TracePipeline.h"

#include <algorithm>
#include <cmath>

namespace {

void applyPortExtension(const TraceConfig& t, const Workspace& ws,
                        const std::vector<double>& f, std::vector<Cplx>& s)
{
    // Sij passes through port j's fixture going in and port i's coming out,
    // so the one-way delays and losses of both ports apply; for a reflection
    // that is the same port twice.
    const int out = (t.param == SParam::S11 || t.param == SParam::S12) ? 0 : 1;
    const int in  = (t.param == SParam::S11 || t.param == SParam::S21) ? 0 : 1;

    double delay = 0.0, lossDb = 0.0;
    for (const int port : {out, in}) {
        if (ws.ext[port].enabled) {
            delay += ws.ext[port].delay;
            lossDb += ws.ext[port].lossDb;
        }
    }
    if (delay == 0.0 && lossDb == 0.0)
        return;

    const double gain = std::pow(10.0, lossDb / 20.0);
    for (size_t i = 0; i < s.size() && i < f.size(); ++i)
        s[i] *= gain * std::exp(Cplx(0.0, 2.0 * M_PI * f[i] * delay));
}

void applyMath(MathOp op, std::vector<Cplx>& data, const std::vector<Cplx>& mem)
{
    if (op == MathOp::Off || mem.size() != data.size())
        return;
    for (size_t i = 0; i < data.size(); ++i) {
        switch (op) {
        case MathOp::DivMem: data[i] = std::abs(mem[i]) > 1e-15 ? data[i] / mem[i] : Cplx(); break;
        case MathOp::SubMem: data[i] -= mem[i]; break;
        case MathOp::AddMem: data[i] += mem[i]; break;
        case MathOp::MulMem: data[i] *= mem[i]; break;
        case MathOp::Off: break;
        }
    }
}

// Time or distance axis values for a transformed trace.
double tdAxisScale(const TraceConfig& t)
{
    if (!t.td.distanceAxis)
        return 1.0;
    const double oneWay = TimeDomain::kSpeedOfLight * t.td.velocityFactor;
    return isReflection(t.param) ? oneWay / 2.0 : oneWay;
}

// Runs the frequency-domain part of the chain on `data`.
bool frequencyChain(const TraceConfig& t, const Workspace& ws, const std::vector<double>& x,
                    XKind xKind, std::vector<Cplx>& data, QString& note)
{
    if (xKind == XKind::Frequency)
        applyPortExtension(t, ws, x, data);

    if (t.gate.enabled) {
        if (xKind != XKind::Frequency) {
            note = QStringLiteral("Gating needs a frequency sweep");
        } else {
            QString err;
            std::vector<Cplx> gated = TimeDomain::gate(x, data, t.gate.start, t.gate.stop, t.gate.notch, err);
            if (gated.empty())
                note = err;
            else
                data.swap(gated);
        }
    }
    return true;
}

void format(const TraceConfig& t, const Workspace& ws, const std::vector<double>& x,
            const std::vector<Cplx>& c, std::vector<double>& y)
{
    const int aperture = std::max(1, int(std::round(double(c.size()) * t.delayAperturePercent / 200.0)));
    y = Formats::apply(c, x, t.format, ws.z0, aperture);
    if (t.smoothing && !Formats::isChart(t.format))
        y = Formats::smooth(y, t.smoothingPercent);
}

} // namespace

namespace TracePipeline {

bool memorySource(const TraceConfig& trace, const Workspace& ws, const SweepData* sweep,
                  std::vector<double>& x, std::vector<Cplx>& data)
{
    if (!sweep || sweep->isEmpty() || !sweep->has(trace.param))
        return false;
    x = sweep->x;
    data = sweep->param(trace.param);
    QString note;
    frequencyChain(trace, ws, x, sweep->xKind, data, note);
    return true;
}

TraceResult compute(const TraceConfig& t, const Workspace& ws, const SweepData* sweep)
{
    TraceResult r;
    if (!sweep || sweep->isEmpty()) {
        r.note = QStringLiteral("No data");
        return r;
    }
    if (!sweep->has(t.param)) {
        r.note = QStringLiteral("%1 was not measured").arg(sParamName(t.param));
        return r;
    }

    std::vector<double> x = sweep->x;
    std::vector<Cplx> data = sweep->param(t.param);
    r.xKind = sweep->xKind;
    r.logX = sweep->logSpacing;

    frequencyChain(t, ws, x, r.xKind, data, r.note);

    // Memory lives in the same domain as the data it was captured from, and
    // is only usable while the stimulus axis still matches.
    const bool memoryUsable = !t.memory.empty() && t.memory.size() == data.size()
        && t.memoryX.size() == x.size() && !x.empty()
        && std::abs(t.memoryX.front() - x.front()) <= 1e-9 * std::max(1.0, std::abs(x.front()))
        && std::abs(t.memoryX.back() - x.back()) <= 1e-9 * std::max(1.0, std::abs(x.back()));
    std::vector<Cplx> mem = memoryUsable ? t.memory : std::vector<Cplx>();
    if (!t.memory.empty() && !memoryUsable && (t.math != MathOp::Off || t.show != ShowWhat::Data))
        r.note = QStringLiteral("Memory does not match the current sweep");

    applyMath(t.math, data, mem);

    if (t.td.enabled) {
        if (r.xKind != XKind::Frequency) {
            r.note = QStringLiteral("Time domain needs a frequency sweep");
        } else {
            const int points = std::clamp(int(x.size()) * 2, 201, 2001);
            TimeDomain::Result td = TimeDomain::transform(x, data, t.td.mode, t.td.window,
                                                          t.td.start, t.td.stop, points);
            if (!td.error.isEmpty()) {
                r.note = td.error;
            } else {
                if (!mem.empty()) {
                    const TimeDomain::Result tm = TimeDomain::transform(x, mem, t.td.mode, t.td.window,
                                                                        t.td.start, t.td.stop, points);
                    mem = tm.h;
                }
                const double scale = tdAxisScale(t);
                x = td.t;
                for (auto& v : x)
                    v *= scale;
                data = td.h;
                r.xKind = t.td.distanceAxis ? XKind::Distance : XKind::Time;
                r.logX = false;
            }
        }
    }

    r.x = x;
    r.c = data;
    format(t, ws, r.x, r.c, r.y);
    if (!mem.empty() && mem.size() == r.x.size()) {
        r.memC = mem;
        format(t, ws, r.x, r.memC, r.memY);
    }
    r.valid = true;

    // Bandwidth search.
    if (t.bw.enabled && !Formats::isChart(t.format) && !r.y.empty()) {
        int ref = t.bw.notch ? MarkerSearch::minIndex(r.y) : MarkerSearch::maxIndex(r.y);
        if (t.bw.fromMarker && !t.markers.isEmpty())
            ref = MarkerSearch::nearestIndex(r.x, t.markers.first().x);
        r.bw = MarkerSearch::bandwidth(r.x, r.y, ref, t.bw.level, t.bw.notch);
    }

    // Limit test.
    if (t.limitTest && !t.limits.isEmpty() && !Formats::isChart(t.format)) {
        r.limitChecked = true;
        r.limitFail.assign(r.y.size(), 0);
        for (size_t i = 0; i < r.y.size(); ++i) {
            for (const LimitSegment& seg : t.limits) {
                const double lo = std::min(seg.x1, seg.x2), hi = std::max(seg.x1, seg.x2);
                if (r.x[i] < lo || r.x[i] > hi)
                    continue;
                const double f = hi > lo ? (r.x[i] - seg.x1) / (seg.x2 - seg.x1) : 0.0;
                const double limit = seg.y1 + (seg.y2 - seg.y1) * f;
                if (seg.upper ? r.y[i] > limit : r.y[i] < limit) {
                    r.limitFail[i] = 1;
                    r.limitPass = false;
                }
            }
        }
    }

    evaluateMarkers(t, ws, r);
    return r;
}

void evaluateMarkers(const TraceConfig& t, const Workspace& ws, TraceResult& r)
{
    r.markers.clear();
    if (!r.valid || r.x.empty())
        return;

    const Marker* ref = nullptr;
    for (const Marker& m : t.markers)
        if (m.number == t.deltaRef)
            ref = &m;

    auto valueAt = [&](double x, double& y, Cplx& c) {
        c = Formats::interpolate(r.x, r.c, x);
        y = Formats::isChart(t.format) ? std::abs(c) : MarkerSearch::interpolate(r.x, r.y, x);
    };

    double refY = 0.0;
    Cplx refC;
    if (ref)
        valueAt(std::clamp(ref->x, r.x.front(), r.x.back()), refY, refC);

    const QString unit = Formats::unit(t.format);
    for (const Marker& m : t.markers) {
        MarkerReadout out;
        out.number = m.number;
        out.x = std::clamp(m.x, r.x.front(), r.x.back());
        valueAt(out.x, out.y, out.c);

        const bool relative = ref && ref != &m;
        out.relative = relative;
        const QString label = QStringLiteral("M%1%2").arg(m.number).arg(ref == &m ? QStringLiteral("R") : QString());

        QString value;
        if (Formats::isChart(t.format)) {
            if (t.format == TraceFormat::Smith && r.xKind == XKind::Frequency)
                value = Formats::smithText(out.c, out.x, ws.z0);
            else
                value = QStringLiteral("%1 ∠ %2°").arg(std::abs(out.c), 0, 'f', 4)
                            .arg(std::arg(out.c) * 180.0 / M_PI, 0, 'f', 2);
        } else {
            const double shown = relative ? out.y - refY : out.y;
            if (t.format == TraceFormat::GroupDelay)
                value = Formats::engineering(shown, QStringLiteral("s"), 4);
            else
                value = QStringLiteral("%1 %2").arg(shown, 0, 'f', t.format == TraceFormat::LogMag ? 3 : 4).arg(unit);
        }

        const QString xText = relative
            ? QStringLiteral("Δ") + Formats::xText(out.x - ref->x, r.xKind)
            : Formats::xText(out.x, r.xKind);
        out.text = QStringLiteral("%1  %2  %3%4").arg(label, xText, relative ? QStringLiteral("Δ") : QString(), value);
        r.markers.append(out);
    }
}

} // namespace TracePipeline
