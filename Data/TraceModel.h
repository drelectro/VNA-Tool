#pragma once

#include "Dsp/Formats.h"
#include "Dsp/MarkerSearch.h"
#include "Dsp/TimeDomain.h"
#include "Instrument/VnaTypes.h"

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>

// Everything the PC does with a sweep after it arrives.
//
// The instrument's SCPI interface stops at "here is complex S-parameter data",
// so formats, scale, markers, searches, memory and math, limits, time domain,
// gating and port extension all live here. Workspace is the plain-data
// description (and is what gets saved to disk); TraceModel owns one, holds the
// latest sweep and keeps a computed TraceResult per trace.

enum class MathOp : quint8 { Off, DivMem, SubMem, AddMem, MulMem };
enum class ShowWhat : quint8 { Data, Memory, DataAndMemory };
enum class MarkerTrack : quint8 { None, Max, Min, Target };

struct Marker
{
    int         number = 1;             // M1, M2, ...
    double      x = 0;                  // stimulus value on the trace's x axis
    MarkerTrack track = MarkerTrack::None;
    double      target = -3;            // for MarkerTrack::Target
};

struct LimitSegment
{
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    bool   upper = true;                // true: trace must stay below
};

struct TdConfig
{
    bool       enabled = false;
    TdMode     mode = TdMode::BandPass;
    WindowType window = WindowType::Hamming;
    double     start = -2e-9, stop = 20e-9;     // seconds
    bool       distanceAxis = false;
    double     velocityFactor = 0.7;
};

struct GateConfig
{
    bool   enabled = false;
    double start = 0, stop = 5e-9;      // seconds
    bool   notch = false;
};

struct BandwidthConfig
{
    bool   enabled = false;
    double level = -3;                  // dB relative to the reference
    bool   notch = false;
    bool   fromMarker = false;          // reference is M1 instead of the peak
};

struct TraceConfig
{
    int      id = 0;
    int      plot = 0;
    bool     visible = true;

    int      fileIndex = -1;            // -1: live sweep; else Workspace::files
    SParam   param = SParam::S11;
    TraceFormat format = TraceFormat::LogMag;

    double   perDiv = 10, refValue = 0;
    int      refPos = 8;                // graticule line (0 bottom .. 10 top) holding refValue
    QColor   color;

    bool     smoothing = false;
    double   smoothingPercent = 2;
    double   delayAperturePercent = 1;  // group delay aperture

    MathOp   math = MathOp::Off;
    ShowWhat show = ShowWhat::Data;
    std::vector<double> memoryX;
    std::vector<Cplx>   memory;

    TdConfig   td;
    GateConfig gate;

    QList<Marker> markers;
    int      deltaRef = 0;              // marker number others are shown relative to; 0 = off
    BandwidthConfig bw;

    bool     limitTest = false;
    QList<LimitSegment> limits;

    QString name() const;
};

struct PortExtension
{
    bool   enabled = false;
    double delay = 0;                   // one-way, seconds
    double lossDb = 0;                  // one-way, dB
};

struct Workspace
{
    QList<TraceConfig> traces;
    int  plotCount = 1;
    int  plotColumns = 1;
    int  activeTrace = 0;
    PortExtension ext[2];
    double z0 = 50;
    bool coupledMarkers = false;
    double peakProminence = 3;          // for next-peak searches, in trace units
    QList<SweepData> files;             // Touchstone files loaded as static traces
    int  nextTraceId = 1;
};

struct MarkerReadout
{
    int     number = 0;
    double  x = 0;
    double  y = 0;                      // formatted value (chart formats: magnitude)
    Cplx    c;                          // complex value at the marker
    bool    relative = false;           // values are relative to the delta reference
    QString text;                       // "M1  2.4 GHz  -0.63 dB"
};

struct TraceResult
{
    bool    valid = false;
    QString note;                       // why not, or a warning to show on the plot

    XKind   xKind = XKind::Frequency;
    bool    logX = false;
    std::vector<double> x;
    std::vector<double> y;              // formatted data
    std::vector<Cplx>   c;              // complex data (what charts draw)
    std::vector<double> memY;           // formatted memory, on the same x
    std::vector<Cplx>   memC;

    QList<MarkerReadout> markers;
    MarkerSearch::Bandwidth bw;

    bool limitChecked = false;
    bool limitPass = true;
    std::vector<char> limitFail;        // per point
};

class TraceModel : public QObject
{
    Q_OBJECT

public:
    explicit TraceModel(QObject* parent = nullptr);

    const Workspace& workspace() const { return _ws; }
    // Mutable access for editors. Call changed() (settings) or
    // structureChanged() (traces/plots added or removed) afterwards.
    Workspace& edit() { return _ws; }
    void setWorkspace(const Workspace& ws);

    const SweepData& liveSweep() const { return _live; }
    const SweepData* sourceSweep(const TraceConfig& t) const;
    const TraceResult& result(int traceIndex) const;

    int  traceCount() const { return int(_ws.traces.size()); }
    TraceConfig* activeTrace();
    int  activeIndex() const { return _ws.activeTrace; }
    void setActiveTrace(int index);

    // Which S-parameters the visible live traces need.
    quint32 neededMask() const;

    int  addTrace(SParam param, TraceFormat format, int plot, int fileIndex = -1);
    void removeTrace(int index);
    void setFormat(int index, TraceFormat format);
    // Reducing the count removes the traces in the plots that go.
    void setPlotCount(int count);
    // Removes one plot and its traces; higher-numbered plots move down.
    void removePlot(int index);
    void autoScale(int index);
    void autoScaleAll();
    void dataToMemory(int index);
    int  addFile(const SweepData& sweep);

    // --- Markers ---
    int  addMarker(int traceIndex, double x);       // returns the marker number
    void removeMarker(int traceIndex, int number);
    void clearMarkers(int traceIndex = -1);         // -1: every trace
    void moveMarker(int traceIndex, int number, double x);
    enum class Search : quint8 { Max, Min, PeakLeft, PeakRight, TargetLeft, TargetRight };
    bool searchMarker(int traceIndex, int number, Search kind, double target = 0);

    static QColor defaultColor(int n);

public slots:
    void setLiveSweep(const SweepData& sweep);
    // Recompute every trace and notify. Call after editing the workspace.
    void changed();
    void notifyStructureChanged();

signals:
    void structureChanged();    // traces or plots added/removed, active trace changed
    void resultsChanged();      // data or settings changed; plots should repaint

private:
    void recompute();
    Marker* findMarker(int traceIndex, int number);
    void applyCoupling(int traceIndex, int number, double x);

    Workspace _ws;
    SweepData _live;
    QList<TraceResult> _results;
};
