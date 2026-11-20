#pragma once

#include <QMetaType>
#include <QString>

#include <array>
#include <complex>
#include <vector>

using Cplx = std::complex<double>;

// Index order matches the order S-parameters appear in the instrument's
// binary frames (see docs/DEVICE_NOTES.md), so it doubles as the mask bit.
enum class SParam : quint8 { S11 = 0, S21 = 1, S12 = 2, S22 = 3 };
inline constexpr int kSParamCount = 4;

QString sParamName(SParam p);
bool    sParamFromName(const QString& name, SParam& out);
// True for S11 and S22: time-domain distance is halved for a round trip.
inline bool isReflection(SParam p) { return p == SParam::S11 || p == SParam::S22; }

enum class SweepMode : quint8 { LinFreq, LogFreq, Power, CW };
QString sweepModeName(SweepMode m);

// What the stimulus axis of a sweep or a computed trace represents.
enum class XKind : quint8 { Frequency, Power, Time, Distance };

struct DeviceInfo
{
    QString manufacturer, model, serial, serviceVersion;
    QString clientVersion, firmware, hardware;

    double minFreq = 100e3, maxFreq = 6.4e9;
    double minIfbw = 15, maxIfbw = 75e3;
    int    maxPoints = 2048;
    double minPower = -40, maxPower = 5;
};

struct Stimulus
{
    SweepMode mode = SweepMode::LinFreq;
    double start = 10e6, stop = 6e9;    // Hz
    int    points = 401;
    double ifbw = 10e3;                 // Hz
    double power = 0;                   // dBm, frequency sweeps
    double powerStart = -30, powerStop = -10;   // dBm, power sweep
    double cwFreq = 1e9;                // Hz, fixed frequency for power sweep
    double dwell = 0;                   // s
    bool   deviceRunning = true;        // VNA:ACQ:RUN?
    bool   externalRef = false;

    double center() const { return 0.5 * (start + stop); }
    double span() const { return stop - start; }
};

struct Health
{
    bool   unlocked = false;
    bool   adcOverload = false;
    bool   unlevelled = false;
    double temperatureC = 0;
    double sweepTimeS = 0;
};

// One complete sweep: every S-parameter the instrument measured, on one
// stimulus axis, taken at the same time.
struct SweepData
{
    std::vector<double> x;
    std::array<std::vector<Cplx>, kSParamCount> s;
    quint32   mask = 0;                 // bit i set when s[i] is present
    XKind     xKind = XKind::Frequency;
    bool      logSpacing = false;
    quint64   sequence = 0;
    int       averages = 1;             // sweeps averaged into this result
    QString   label;                    // file name, for sweeps loaded from disk

    bool has(SParam p) const { return (mask >> static_cast<int>(p)) & 1u; }
    const std::vector<Cplx>& param(SParam p) const { return s[static_cast<size_t>(p)]; }
    bool isEmpty() const { return x.empty(); }
    int  points() const { return static_cast<int>(x.size()); }
};

Q_DECLARE_METATYPE(SweepData)
