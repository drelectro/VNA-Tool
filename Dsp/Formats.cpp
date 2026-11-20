#include "Formats.h"

#include <algorithm>
#include <cmath>

namespace Formats {

QString name(TraceFormat f)
{
    switch (f) {
    case TraceFormat::LogMag:         return QStringLiteral("Log Mag");
    case TraceFormat::LinMag:         return QStringLiteral("Lin Mag");
    case TraceFormat::Phase:          return QStringLiteral("Phase");
    case TraceFormat::UnwrappedPhase: return QStringLiteral("Unwrapped Phase");
    case TraceFormat::Real:           return QStringLiteral("Real");
    case TraceFormat::Imag:           return QStringLiteral("Imaginary");
    case TraceFormat::Swr:            return QStringLiteral("SWR");
    case TraceFormat::GroupDelay:     return QStringLiteral("Group Delay");
    case TraceFormat::Smith:          return QStringLiteral("Smith");
    case TraceFormat::Polar:          return QStringLiteral("Polar");
    case TraceFormat::Impedance:      return QStringLiteral("Impedance |Z|");
    }
    return QString();
}

QString unit(TraceFormat f)
{
    switch (f) {
    case TraceFormat::LogMag:         return QStringLiteral("dB");
    case TraceFormat::Phase:
    case TraceFormat::UnwrappedPhase: return QStringLiteral("°");
    case TraceFormat::GroupDelay:     return QStringLiteral("s");
    case TraceFormat::Impedance:      return QStringLiteral("Ω");
    default:                          return QStringLiteral("U");
    }
}

void defaultScale(TraceFormat f, double& perDiv, double& refValue, int& refPos)
{
    refPos = 5;
    switch (f) {
    case TraceFormat::LogMag:         perDiv = 10;   refValue = 0;   refPos = 8; break;
    case TraceFormat::LinMag:         perDiv = 0.1;  refValue = 0;   refPos = 0; break;
    case TraceFormat::Phase:          perDiv = 45;   refValue = 0;   break;
    case TraceFormat::UnwrappedPhase: perDiv = 360;  refValue = 0;   break;
    case TraceFormat::Real:
    case TraceFormat::Imag:           perDiv = 0.2;  refValue = 0;   break;
    case TraceFormat::Swr:            perDiv = 1;    refValue = 1;   refPos = 0; break;
    case TraceFormat::GroupDelay:     perDiv = 1e-9; refValue = 0;   refPos = 2; break;
    case TraceFormat::Impedance:      perDiv = 10;   refValue = 50;  break;
    case TraceFormat::Smith:
    case TraceFormat::Polar:          perDiv = 0.2;  refValue = 1;   break;
    }
}

double applyPoint(const Cplx& s, TraceFormat f, double z0)
{
    switch (f) {
    case TraceFormat::LogMag:  return 20.0 * std::log10(std::max(std::abs(s), 1e-15));
    case TraceFormat::LinMag:
    case TraceFormat::Smith:
    case TraceFormat::Polar:   return std::abs(s);
    case TraceFormat::Phase:
    case TraceFormat::UnwrappedPhase:
        return std::arg(s) * 180.0 / M_PI;
    case TraceFormat::Real:    return s.real();
    case TraceFormat::Imag:    return s.imag();
    case TraceFormat::Swr: {
        const double m = std::min(std::abs(s), 0.999999);
        return (1.0 + m) / (1.0 - m);
    }
    case TraceFormat::Impedance: {
        const Cplx den = 1.0 - s;
        if (std::abs(den) < 1e-12)
            return 1e9;
        return std::abs(z0 * (1.0 + s) / den);
    }
    case TraceFormat::GroupDelay:
        return 0.0;     // needs neighbours; see apply()
    }
    return 0.0;
}

std::vector<double> unwrappedPhaseDeg(const std::vector<Cplx>& s)
{
    std::vector<double> out(s.size());
    double offset = 0.0, prev = 0.0;
    for (size_t i = 0; i < s.size(); ++i) {
        const double p = std::arg(s[i]) * 180.0 / M_PI;
        if (i > 0) {
            const double d = p - prev;
            if (d > 180.0) offset -= 360.0;
            else if (d < -180.0) offset += 360.0;
        }
        prev = p;
        out[i] = p + offset;
    }
    return out;
}

std::vector<double> apply(const std::vector<Cplx>& s, const std::vector<double>& x,
                          TraceFormat f, double z0, int aperture)
{
    const size_t n = s.size();
    if (f == TraceFormat::UnwrappedPhase)
        return unwrappedPhaseDeg(s);

    std::vector<double> out(n, 0.0);
    if (f == TraceFormat::GroupDelay) {
        if (n < 2 || x.size() != n)
            return out;
        const std::vector<double> ph = unwrappedPhaseDeg(s);
        const int a = std::max(1, aperture);
        for (size_t i = 0; i < n; ++i) {
            const size_t lo = i >= size_t(a) ? i - size_t(a) : 0;
            const size_t hi = std::min(n - 1, i + size_t(a));
            const double df = x[hi] - x[lo];
            out[i] = df != 0.0 ? -(ph[hi] - ph[lo]) / (360.0 * df) : 0.0;
        }
        return out;
    }

    for (size_t i = 0; i < n; ++i)
        out[i] = applyPoint(s[i], f, z0);
    return out;
}

std::vector<double> smooth(const std::vector<double>& y, double percent)
{
    const int n = static_cast<int>(y.size());
    const int half = static_cast<int>(std::round(n * percent / 200.0));
    if (n < 3 || half < 1)
        return y;

    std::vector<double> prefix(size_t(n) + 1, 0.0);
    for (int i = 0; i < n; ++i)
        prefix[size_t(i) + 1] = prefix[size_t(i)] + y[size_t(i)];

    std::vector<double> out(y.size());
    for (int i = 0; i < n; ++i) {
        const int lo = std::max(0, i - half);
        const int hi = std::min(n - 1, i + half);
        out[size_t(i)] = (prefix[size_t(hi) + 1] - prefix[size_t(lo)]) / double(hi - lo + 1);
    }
    return out;
}

Cplx interpolate(const std::vector<double>& x, const std::vector<Cplx>& s, double xq)
{
    if (x.empty() || s.size() != x.size())
        return Cplx();
    if (xq <= x.front())
        return s.front();
    if (xq >= x.back())
        return s.back();
    const auto it = std::upper_bound(x.begin(), x.end(), xq);
    const size_t hi = size_t(it - x.begin());
    const size_t lo = hi - 1;
    const double span = x[hi] - x[lo];
    const double t = span != 0.0 ? (xq - x[lo]) / span : 0.0;
    return s[lo] + (s[hi] - s[lo]) * t;
}

QString engineering(double value, const QString& unitText, int significant)
{
    if (value == 0.0 || !std::isfinite(value))
        return QStringLiteral("0 %1").arg(unitText).trimmed();

    static const struct { double scale; const char* prefix; } kSteps[] = {
        {1e12, "T"}, {1e9, "G"}, {1e6, "M"}, {1e3, "k"}, {1.0, ""},
        {1e-3, "m"}, {1e-6, "µ"}, {1e-9, "n"}, {1e-12, "p"}, {1e-15, "f"},
    };
    const double mag = std::abs(value);
    for (const auto& step : kSteps) {
        if (mag >= step.scale * 0.9999999) {
            return QStringLiteral("%1 %2%3")
                .arg(QString::number(value / step.scale, 'g', significant),
                     QString::fromUtf8(step.prefix), unitText);
        }
    }
    return QStringLiteral("%1 %2").arg(QString::number(value, 'g', significant), unitText);
}

QString xText(double x, XKind kind)
{
    switch (kind) {
    case XKind::Frequency: return engineering(x, QStringLiteral("Hz"), 7);
    case XKind::Power:     return QStringLiteral("%1 dBm").arg(x, 0, 'f', 2);
    case XKind::Time:      return engineering(x, QStringLiteral("s"), 5);
    case XKind::Distance:  return engineering(x, QStringLiteral("m"), 5);
    }
    return QString::number(x);
}

QString smithText(const Cplx& gamma, double freqHz, double z0)
{
    const Cplx den = 1.0 - gamma;
    if (std::abs(den) < 1e-12)
        return QStringLiteral("open");
    const Cplx z = z0 * (1.0 + gamma) / den;
    QString reactive;
    if (freqHz > 0.0) {
        const double w = 2.0 * M_PI * freqHz;
        if (z.imag() >= 0.0)
            reactive = engineering(z.imag() / w, QStringLiteral("H"), 4);
        else
            reactive = engineering(-1.0 / (w * z.imag()), QStringLiteral("F"), 4);
    }
    return QStringLiteral("%1 %2 j%3 Ω  %4")
        .arg(z.real(), 0, 'f', 2)
        .arg(z.imag() >= 0.0 ? QLatin1Char('+') : QLatin1Char('-'))
        .arg(std::abs(z.imag()), 0, 'f', 2)
        .arg(reactive);
}

} // namespace Formats
