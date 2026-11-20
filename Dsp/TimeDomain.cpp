#include "TimeDomain.h"

#include "Fft.h"
#include "Formats.h"

#include <algorithm>
#include <cmath>

namespace {

// Zeroth-order modified Bessel function of the first kind, by its series.
double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

// Window value at position u in [-1, 1] (0 = centre).
double windowAt(WindowType type, double u)
{
    const double a = std::clamp(u, -1.0, 1.0);
    const double c = M_PI * a;      // cos(c) is +1 at the centre, -1 at the edges
    switch (type) {
    case WindowType::Rectangular: return 1.0;
    case WindowType::Hamming:     return 0.54 + 0.46 * std::cos(c);
    case WindowType::Hann:        return 0.5 + 0.5 * std::cos(c);
    case WindowType::Blackman:    return 0.42 + 0.5 * std::cos(c) + 0.08 * std::cos(2.0 * c);
    case WindowType::Kaiser: {
        constexpr double beta = 6.0;
        return besselI0(beta * std::sqrt(std::max(0.0, 1.0 - a * a))) / besselI0(beta);
    }
    }
    return 1.0;
}

// Linear interpolation into a periodic array at fractional index `pos`.
Cplx samplePeriodic(const std::vector<Cplx>& a, double pos)
{
    const double n = double(a.size());
    pos = std::fmod(pos, n);
    if (pos < 0)
        pos += n;
    const size_t i0 = size_t(pos) % a.size();
    const size_t i1 = (i0 + 1) % a.size();
    const double t = pos - std::floor(pos);
    return a[i0] + (a[i1] - a[i0]) * t;
}

} // namespace

namespace TimeDomain {

QString windowName(WindowType w)
{
    switch (w) {
    case WindowType::Rectangular: return QStringLiteral("Rectangular");
    case WindowType::Hamming:     return QStringLiteral("Hamming");
    case WindowType::Hann:        return QStringLiteral("Hann");
    case WindowType::Blackman:    return QStringLiteral("Blackman");
    case WindowType::Kaiser:      return QStringLiteral("Kaiser (β=6)");
    }
    return QString();
}

QString modeName(TdMode m)
{
    switch (m) {
    case TdMode::BandPass:       return QStringLiteral("Band-pass");
    case TdMode::LowPassImpulse: return QStringLiteral("Low-pass impulse");
    case TdMode::LowPassStep:    return QStringLiteral("Low-pass step");
    }
    return QString();
}

std::vector<double> window(WindowType type, int n, bool half)
{
    std::vector<double> w(size_t(std::max(n, 0)));
    for (int i = 0; i < n; ++i) {
        // Half: index 0 is the centre and the last sample approaches the edge.
        // Full: symmetric, with the edge samples half a step inside +/-1 so
        // that no sample is ever exactly zero (gating divides by the window).
        const double u = half ? double(i) / double(n)
                              : (2.0 * (i + 0.5) / double(n)) - 1.0;
        w[size_t(i)] = windowAt(type, u);
    }
    return w;
}

bool isUniform(const std::vector<double>& f, double& step)
{
    if (f.size() < 3)
        return false;
    step = (f.back() - f.front()) / double(f.size() - 1);
    if (!(step > 0.0))
        return false;
    for (size_t i = 1; i < f.size(); ++i)
        if (std::abs((f[i] - f[i - 1]) - step) > step * 1e-3)
            return false;
    return true;
}

double aliasFreeSpan(const std::vector<double>& f)
{
    double step = 0.0;
    return isUniform(f, step) ? 1.0 / step : 0.0;
}

Result transform(const std::vector<double>& f, const std::vector<Cplx>& s,
                 TdMode mode, WindowType windowType,
                 double tStart, double tStop, int points)
{
    Result r;
    double df = 0.0;
    if (f.size() != s.size() || !isUniform(f, df)) {
        r.error = QStringLiteral("Time domain needs a linear frequency sweep");
        return r;
    }
    if (!(tStop > tStart) || points < 2) {
        r.error = QStringLiteral("Time domain stop must be greater than start");
        return r;
    }

    const int n = static_cast<int>(f.size());
    std::vector<Cplx> td;
    double dt = 0.0;

    if (mode == TdMode::BandPass) {
        const std::vector<double> w = window(windowType, n, false);
        const size_t nfft = Fft::nextPow2(size_t(n) * 8);
        td.assign(nfft, Cplx());
        double wsum = 0.0;
        for (int k = 0; k < n; ++k) {
            td[size_t(k)] = s[size_t(k)] * w[size_t(k)];
            wsum += w[size_t(k)];
        }
        Fft::transform(td, true);
        const double scale = double(nfft) / wsum;
        for (auto& v : td)
            v *= scale;
        dt = 1.0 / (double(nfft) * df);
    } else {
        // Harmonic grid: n points at k * dfh, k = 1..n, plus DC.
        const double dfh = f.back() / double(n);
        // DC by extrapolating magnitude and phase of the first two points. A
        // real response has phase 0 or 180 degrees at DC, and extrapolating
        // in polar form gets there for a delayed open or short; extrapolating
        // the real part alone overshoots, because cos() is not linear.
        const double ratio = f[0] / (f[1] - f[0]);
        double dPhase = std::arg(s[1]) - std::arg(s[0]);
        if (dPhase > M_PI) dPhase -= 2.0 * M_PI;
        else if (dPhase < -M_PI) dPhase += 2.0 * M_PI;
        const double phase0 = std::arg(s[0]) - dPhase * ratio;
        const double mag0 = std::max(0.0, std::abs(s[0]) - (std::abs(s[1]) - std::abs(s[0])) * ratio);
        const Cplx dc(mag0 * (std::cos(phase0) >= 0.0 ? 1.0 : -1.0), 0.0);

        std::vector<Cplx> grid(size_t(n) + 1);
        grid[0] = dc;
        for (int k = 1; k <= n; ++k) {
            const double fk = k * dfh;
            if (fk < f.front()) {
                const double u = fk / f.front();
                const double mag = mag0 + (std::abs(s[0]) - mag0) * u;
                const double ph = phase0 + (std::arg(s[0]) - phase0) * u;
                grid[size_t(k)] = std::polar(mag, ph);
            } else {
                grid[size_t(k)] = Formats::interpolate(f, s, fk);
            }
        }

        const std::vector<double> w = window(windowType, n + 1, true);
        const size_t nfft = Fft::nextPow2(size_t(2 * n + 1) * 8);
        td.assign(nfft, Cplx());
        double wsum = w[0];
        td[0] = grid[0] * w[0];
        for (int k = 1; k <= n; ++k) {
            const Cplx v = grid[size_t(k)] * w[size_t(k)];
            td[size_t(k)] = v;
            td[nfft - size_t(k)] = std::conj(v);
            wsum += 2.0 * w[size_t(k)];
        }
        Fft::transform(td, true);
        dt = 1.0 / (double(nfft) * dfh);

        if (mode == TdMode::LowPassImpulse) {
            const double scale = double(nfft) / wsum;
            for (auto& v : td)
                v = Cplx(v.real() * scale, 0.0);
        } else {
            // Step = running sum of the impulse response, started at the most
            // negative time. The full-period sum equals the DC value, so an
            // ideal open settles at +1 and a short at -1.
            std::vector<Cplx> step(nfft);
            double acc = 0.0;
            const size_t half = nfft / 2;
            for (size_t i = 0; i < nfft; ++i) {
                const size_t idx = (half + i) % nfft;
                acc += td[idx].real();
                step[idx] = Cplx(acc, 0.0);
            }
            td.swap(step);
        }
    }

    r.t.resize(size_t(points));
    r.h.resize(size_t(points));
    for (int i = 0; i < points; ++i) {
        const double t = tStart + (tStop - tStart) * double(i) / double(points - 1);
        r.t[size_t(i)] = t;
        r.h[size_t(i)] = samplePeriodic(td, t / dt);
    }
    return r;
}

std::vector<Cplx> gate(const std::vector<double>& f, const std::vector<Cplx>& s,
                       double tStart, double tStop, bool notch, QString& error)
{
    double df = 0.0;
    if (f.size() != s.size() || !isUniform(f, df)) {
        error = QStringLiteral("Gating needs a linear frequency sweep");
        return {};
    }
    if (!(tStop > tStart)) {
        error = QStringLiteral("Gate stop must be greater than start");
        return {};
    }

    const int n = static_cast<int>(f.size());
    // A gentle Kaiser window keeps the edge samples well away from zero, so
    // dividing it back out afterwards does not blow up the band edges.
    std::vector<double> w(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k) {
        const double u = (2.0 * (k + 0.5) / double(n)) - 1.0;
        w[size_t(k)] = besselI0(3.0 * std::sqrt(std::max(0.0, 1.0 - u * u))) / besselI0(3.0);
    }

    const size_t nfft = Fft::nextPow2(size_t(n) * 4);
    std::vector<Cplx> td(nfft, Cplx());
    for (int k = 0; k < n; ++k)
        td[size_t(k)] = s[size_t(k)] * w[size_t(k)];
    Fft::transform(td, true);

    const double dt = 1.0 / (double(nfft) * df);
    const double period = double(nfft) * dt;
    // Raised-cosine gate edges about two resolution cells wide.
    const double edge = std::min(2.0 / (double(n) * df), 0.25 * (tStop - tStart));

    for (size_t i = 0; i < nfft; ++i) {
        double t = double(i) * dt;
        if (i >= nfft / 2)
            t -= period;        // upper half of the array is negative time
        double g;
        if (t <= tStart - edge || t >= tStop + edge) g = 0.0;
        else if (t < tStart + edge) g = 0.5 - 0.5 * std::cos(M_PI * (t - (tStart - edge)) / (2.0 * edge));
        else if (t > tStop - edge)  g = 0.5 - 0.5 * std::cos(M_PI * ((tStop + edge) - t) / (2.0 * edge));
        else g = 1.0;
        td[i] *= notch ? 1.0 - g : g;
    }

    Fft::transform(td, false);
    std::vector<Cplx> out(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k)
        out[size_t(k)] = td[size_t(k)] / w[size_t(k)];
    return out;
}

} // namespace TimeDomain
