#pragma once

#include "Instrument/VnaTypes.h"

#include <QString>

#include <vector>

// Time-domain transform and gating of frequency-domain data.
//
// Both need a LINEAR frequency sweep. The functions return an empty result
// (and say why) for anything else.

enum class WindowType : quint8 { Rectangular, Hamming, Hann, Blackman, Kaiser };
enum class TdMode : quint8 { BandPass, LowPassImpulse, LowPassStep };

namespace TimeDomain {

QString windowName(WindowType w);
QString modeName(TdMode m);

// n window samples. `half` gives the right-hand half of a symmetric window
// (peak at index 0), which is what low-pass mode applies from DC upwards.
std::vector<double> window(WindowType type, int n, bool half);

// True if `f` is uniformly spaced; `step` receives the spacing.
bool isUniform(const std::vector<double>& f, double& step);

// The time span that is free of aliasing: 1 / frequency step.
double aliasFreeSpan(const std::vector<double>& f);

struct Result
{
    std::vector<double> t;      // seconds
    std::vector<Cplx>   h;      // band-pass: complex; low-pass: real part only
    QString             error;  // non-empty when the transform was not possible
};

// Transforms S(f) and samples the response at `points` times from tStart to
// tStop. Responses are normalised so an ideal full reflection has a peak
// (impulse) or final value (step) of 1.
//
// Low-pass modes need data on a harmonic grid (f_k = k * df, including DC).
// The measured data is resampled onto one and DC is extrapolated, so any
// sweep works, but the result is best when the start frequency is already
// close to stop / points.
Result transform(const std::vector<double>& f, const std::vector<Cplx>& s,
                 TdMode mode, WindowType windowType,
                 double tStart, double tStop, int points);

// Time gating: transform, keep (or with `notch`, remove) tStart..tStop,
// transform back. Returns S(f) on the original grid, or an empty vector with
// `error` set.
std::vector<Cplx> gate(const std::vector<double>& f, const std::vector<Cplx>& s,
                       double tStart, double tStop, bool notch, QString& error);

inline constexpr double kSpeedOfLight = 299792458.0;

} // namespace TimeDomain
