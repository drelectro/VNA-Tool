#pragma once

#include "Instrument/VnaTypes.h"

#include <QString>

#include <vector>

// Turning complex S-parameter data into what a trace displays.

enum class TraceFormat : quint8 {
    LogMag, LinMag, Phase, UnwrappedPhase, Real, Imag, Swr, GroupDelay,
    Smith, Polar, Impedance,
};
inline constexpr int kTraceFormatCount = 11;

namespace Formats {

QString name(TraceFormat f);
QString unit(TraceFormat f);
// Smith and Polar are drawn on their own chart from complex data.
inline bool isChart(TraceFormat f) { return f == TraceFormat::Smith || f == TraceFormat::Polar; }
// Default scale per division and reference value for a format.
void defaultScale(TraceFormat f, double& perDiv, double& refValue, int& refPos);

// One value per input point. `x` is needed for group delay only, and must be
// frequency in Hz for the result to mean anything. `aperture` is the group
// delay aperture in points on each side (>= 1).
std::vector<double> apply(const std::vector<Cplx>& s, const std::vector<double>& x,
                          TraceFormat f, double z0, int aperture = 1);

double applyPoint(const Cplx& s, TraceFormat f, double z0);

// Phase in degrees with 360-degree jumps removed.
std::vector<double> unwrappedPhaseDeg(const std::vector<Cplx>& s);

// Moving average over `percent` of the trace width (0-20 is sensible).
std::vector<double> smooth(const std::vector<double>& y, double percent);

// Linear interpolation of a complex trace at stimulus value xq (clamped).
Cplx interpolate(const std::vector<double>& x, const std::vector<Cplx>& s, double xq);

// Series-equivalent text for a Smith chart readout, e.g. "50.2 +j12.1 Ohm (770 pH)".
QString smithText(const Cplx& gamma, double freqHz, double z0);

// "1.234 GHz", "-12.50 dBm", "3.2 ns", "0.451 m".
QString engineering(double value, const QString& unit, int significant = 6);
QString xText(double x, XKind kind);

} // namespace Formats
