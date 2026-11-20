#include "Workspace.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace {

QJsonArray complexArray(const std::vector<Cplx>& v)
{
    QJsonArray a;
    for (const Cplx& c : v) {
        a.append(c.real());
        a.append(c.imag());
    }
    return a;
}

std::vector<Cplx> complexFrom(const QJsonArray& a)
{
    std::vector<Cplx> v;
    v.reserve(size_t(a.size() / 2));
    for (qsizetype i = 0; i + 1 < a.size(); i += 2)
        v.emplace_back(a[i].toDouble(), a[i + 1].toDouble());
    return v;
}

QJsonArray realArray(const std::vector<double>& v)
{
    QJsonArray a;
    for (const double d : v)
        a.append(d);
    return a;
}

std::vector<double> realFrom(const QJsonArray& a)
{
    std::vector<double> v;
    v.reserve(size_t(a.size()));
    for (const QJsonValue& d : a)
        v.push_back(d.toDouble());
    return v;
}

QJsonObject sweepJson(const SweepData& s)
{
    QJsonObject o;
    o["label"] = s.label;
    o["mask"] = int(s.mask);
    o["xKind"] = int(s.xKind);
    o["log"] = s.logSpacing;
    o["x"] = realArray(s.x);
    for (int k = 0; k < kSParamCount; ++k)
        if (s.has(static_cast<SParam>(k)))
            o[sParamName(static_cast<SParam>(k))] = complexArray(s.s[size_t(k)]);
    return o;
}

SweepData sweepFrom(const QJsonObject& o)
{
    SweepData s;
    s.label = o["label"].toString();
    s.mask = quint32(o["mask"].toInt());
    s.xKind = static_cast<XKind>(o["xKind"].toInt());
    s.logSpacing = o["log"].toBool();
    s.x = realFrom(o["x"].toArray());
    for (int k = 0; k < kSParamCount; ++k)
        if (s.has(static_cast<SParam>(k)))
            s.s[size_t(k)] = complexFrom(o[sParamName(static_cast<SParam>(k))].toArray());
    return s;
}

QJsonObject traceJson(const TraceConfig& t)
{
    QJsonObject o;
    o["id"] = t.id;
    o["plot"] = t.plot;
    o["visible"] = t.visible;
    o["file"] = t.fileIndex;
    o["param"] = sParamName(t.param);
    o["format"] = int(t.format);
    o["perDiv"] = t.perDiv;
    o["ref"] = t.refValue;
    o["refPos"] = t.refPos;
    o["color"] = t.color.name();
    o["smoothing"] = t.smoothing;
    o["smoothingPercent"] = t.smoothingPercent;
    o["delayAperture"] = t.delayAperturePercent;
    o["math"] = int(t.math);
    o["show"] = int(t.show);
    if (!t.memory.empty()) {
        o["memoryX"] = realArray(t.memoryX);
        o["memory"] = complexArray(t.memory);
    }

    QJsonObject td;
    td["enabled"] = t.td.enabled;
    td["mode"] = int(t.td.mode);
    td["window"] = int(t.td.window);
    td["start"] = t.td.start;
    td["stop"] = t.td.stop;
    td["distance"] = t.td.distanceAxis;
    td["vf"] = t.td.velocityFactor;
    o["td"] = td;

    QJsonObject gate;
    gate["enabled"] = t.gate.enabled;
    gate["start"] = t.gate.start;
    gate["stop"] = t.gate.stop;
    gate["notch"] = t.gate.notch;
    o["gate"] = gate;

    QJsonObject bw;
    bw["enabled"] = t.bw.enabled;
    bw["level"] = t.bw.level;
    bw["notch"] = t.bw.notch;
    bw["fromMarker"] = t.bw.fromMarker;
    o["bw"] = bw;

    QJsonArray markers;
    for (const Marker& m : t.markers) {
        QJsonObject mo;
        mo["n"] = m.number;
        mo["x"] = m.x;
        mo["track"] = int(m.track);
        mo["target"] = m.target;
        markers.append(mo);
    }
    o["markers"] = markers;
    o["deltaRef"] = t.deltaRef;

    o["limitTest"] = t.limitTest;
    QJsonArray limits;
    for (const LimitSegment& s : t.limits) {
        QJsonObject so;
        so["x1"] = s.x1; so["y1"] = s.y1; so["x2"] = s.x2; so["y2"] = s.y2;
        so["upper"] = s.upper;
        limits.append(so);
    }
    o["limits"] = limits;
    return o;
}

TraceConfig traceFrom(const QJsonObject& o)
{
    TraceConfig t;
    t.id = o["id"].toInt(1);
    t.plot = o["plot"].toInt();
    t.visible = o["visible"].toBool(true);
    t.fileIndex = o["file"].toInt(-1);
    sParamFromName(o["param"].toString(), t.param);
    t.format = static_cast<TraceFormat>(std::clamp(o["format"].toInt(), 0, kTraceFormatCount - 1));
    t.perDiv = o["perDiv"].toDouble(10);
    t.refValue = o["ref"].toDouble();
    t.refPos = o["refPos"].toInt(5);
    t.color = QColor(o["color"].toString());
    if (!t.color.isValid())
        t.color = TraceModel::defaultColor(t.id - 1);
    t.smoothing = o["smoothing"].toBool();
    t.smoothingPercent = o["smoothingPercent"].toDouble(2);
    t.delayAperturePercent = o["delayAperture"].toDouble(1);
    t.math = static_cast<MathOp>(o["math"].toInt());
    t.show = static_cast<ShowWhat>(o["show"].toInt());
    t.memoryX = realFrom(o["memoryX"].toArray());
    t.memory = complexFrom(o["memory"].toArray());

    const QJsonObject td = o["td"].toObject();
    t.td.enabled = td["enabled"].toBool();
    t.td.mode = static_cast<TdMode>(td["mode"].toInt());
    t.td.window = static_cast<WindowType>(td["window"].toInt(1));
    t.td.start = td["start"].toDouble(-2e-9);
    t.td.stop = td["stop"].toDouble(20e-9);
    t.td.distanceAxis = td["distance"].toBool();
    t.td.velocityFactor = td["vf"].toDouble(0.7);

    const QJsonObject gate = o["gate"].toObject();
    t.gate.enabled = gate["enabled"].toBool();
    t.gate.start = gate["start"].toDouble();
    t.gate.stop = gate["stop"].toDouble(5e-9);
    t.gate.notch = gate["notch"].toBool();

    const QJsonObject bw = o["bw"].toObject();
    t.bw.enabled = bw["enabled"].toBool();
    t.bw.level = bw["level"].toDouble(-3);
    t.bw.notch = bw["notch"].toBool();
    t.bw.fromMarker = bw["fromMarker"].toBool();

    for (const QJsonValue& v : o["markers"].toArray()) {
        const QJsonObject mo = v.toObject();
        Marker m;
        m.number = mo["n"].toInt(1);
        m.x = mo["x"].toDouble();
        m.track = static_cast<MarkerTrack>(mo["track"].toInt());
        m.target = mo["target"].toDouble(-3);
        t.markers.append(m);
    }
    t.deltaRef = o["deltaRef"].toInt();

    t.limitTest = o["limitTest"].toBool();
    for (const QJsonValue& v : o["limits"].toArray()) {
        const QJsonObject so = v.toObject();
        LimitSegment s;
        s.x1 = so["x1"].toDouble(); s.y1 = so["y1"].toDouble();
        s.x2 = so["x2"].toDouble(); s.y2 = so["y2"].toDouble();
        s.upper = so["upper"].toBool(true);
        t.limits.append(s);
    }
    return t;
}

} // namespace

namespace WorkspaceIo {

QJsonObject toJson(const Workspace& ws)
{
    QJsonObject o;
    o["format"] = QStringLiteral("vna-tool-workspace");
    o["version"] = 1;
    o["plotCount"] = ws.plotCount;
    o["plotColumns"] = ws.plotColumns;
    o["activeTrace"] = ws.activeTrace;
    o["z0"] = ws.z0;
    o["coupledMarkers"] = ws.coupledMarkers;
    o["peakProminence"] = ws.peakProminence;
    o["nextTraceId"] = ws.nextTraceId;

    QJsonArray ext;
    for (const PortExtension& e : ws.ext) {
        QJsonObject eo;
        eo["enabled"] = e.enabled;
        eo["delay"] = e.delay;
        eo["lossDb"] = e.lossDb;
        ext.append(eo);
    }
    o["portExtension"] = ext;

    QJsonArray traces;
    for (const TraceConfig& t : ws.traces)
        traces.append(traceJson(t));
    o["traces"] = traces;

    QJsonArray files;
    for (const SweepData& s : ws.files)
        files.append(sweepJson(s));
    o["files"] = files;
    return o;
}

bool fromJson(const QJsonObject& o, Workspace& ws, QString& err)
{
    if (o["format"].toString() != QLatin1String("vna-tool-workspace")) {
        err = QStringLiteral("Not a VNA Tool workspace file");
        return false;
    }
    ws = Workspace{};
    ws.plotCount = std::clamp(o["plotCount"].toInt(1), 1, 16);
    ws.plotColumns = std::clamp(o["plotColumns"].toInt(1), 1, 4);
    ws.z0 = o["z0"].toDouble(50);
    ws.coupledMarkers = o["coupledMarkers"].toBool();
    ws.peakProminence = o["peakProminence"].toDouble(3);

    const QJsonArray ext = o["portExtension"].toArray();
    for (int i = 0; i < 2 && i < ext.size(); ++i) {
        const QJsonObject eo = ext[i].toObject();
        ws.ext[i].enabled = eo["enabled"].toBool();
        ws.ext[i].delay = eo["delay"].toDouble();
        ws.ext[i].lossDb = eo["lossDb"].toDouble();
    }

    for (const QJsonValue& v : o["files"].toArray())
        ws.files.append(sweepFrom(v.toObject()));

    int maxId = 0;
    for (const QJsonValue& v : o["traces"].toArray()) {
        TraceConfig t = traceFrom(v.toObject());
        t.plot = std::clamp(t.plot, 0, ws.plotCount - 1);
        if (t.fileIndex >= ws.files.size())
            t.fileIndex = -1;
        maxId = std::max(maxId, t.id);
        ws.traces.append(t);
    }
    ws.nextTraceId = std::max(o["nextTraceId"].toInt(1), maxId + 1);
    ws.activeTrace = std::clamp(o["activeTrace"].toInt(), 0, std::max(0, int(ws.traces.size()) - 1));
    return true;
}

bool save(const QString& path, const Workspace& ws, QString& err)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        err = QStringLiteral("Could not write %1: %2").arg(path, f.errorString());
        return false;
    }
    f.write(QJsonDocument(toJson(ws)).toJson(QJsonDocument::Compact));
    return true;
}

bool load(const QString& path, Workspace& ws, QString& err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        err = QStringLiteral("Could not read %1: %2").arg(path, f.errorString());
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        err = QStringLiteral("%1 is not valid JSON: %2").arg(path, pe.errorString());
        return false;
    }
    return fromJson(doc.object(), ws, err);
}

Workspace defaultWorkspace()
{
    Workspace ws;
    ws.plotCount = 1;
    ws.plotColumns = 1;
    int n = 0;
    for (const SParam p : {SParam::S11, SParam::S21}) {
        TraceConfig t;
        t.id = ws.nextTraceId++;
        t.param = p;
        t.format = TraceFormat::LogMag;
        t.color = TraceModel::defaultColor(n++);
        Formats::defaultScale(t.format, t.perDiv, t.refValue, t.refPos);
        ws.traces.append(t);
    }
    return ws;
}

Workspace fourPlotPreset()
{
    Workspace ws;
    ws.plotCount = 4;
    ws.plotColumns = 2;
    struct Entry { int plot; SParam param; TraceFormat format; };
    const Entry entries[] = {
        {0, SParam::S11, TraceFormat::LogMag},
        {0, SParam::S21, TraceFormat::LogMag},
        {1, SParam::S11, TraceFormat::Smith},
        {2, SParam::S11, TraceFormat::Swr},
        {3, SParam::S22, TraceFormat::LogMag},
    };
    int n = 0;
    for (const Entry& e : entries) {
        TraceConfig t;
        t.id = ws.nextTraceId++;
        t.plot = e.plot;
        t.param = e.param;
        t.format = e.format;
        t.color = TraceModel::defaultColor(n++);
        Formats::defaultScale(t.format, t.perDiv, t.refValue, t.refPos);
        ws.traces.append(t);
    }
    return ws;
}

} // namespace WorkspaceIo
