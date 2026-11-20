#include "Touchstone.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include <cmath>

namespace {

QString num(double v) { return QString::number(v, 'g', 10); }

bool openForWrite(QFile& f, QString& err)
{
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        err = QStringLiteral("Could not write %1: %2").arg(f.fileName(), f.errorString());
        return false;
    }
    return true;
}

void header(QTextStream& out, const SweepData& sweep, double z0)
{
    out << "! PNB-60, saved by VNA Tool " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    if (sweep.averages > 1)
        out << "! " << sweep.averages << " sweeps averaged\n";
    out << "# Hz S RI R " << num(z0) << "\n";
}

Cplx fromPair(double a, double b, const QString& format)
{
    if (format == QLatin1String("RI"))
        return Cplx(a, b);
    const double mag = format == QLatin1String("DB") ? std::pow(10.0, a / 20.0) : a;
    const double ph = b * M_PI / 180.0;
    return Cplx(mag * std::cos(ph), mag * std::sin(ph));
}

} // namespace

namespace Touchstone {

bool writeS2p(const QString& path, const SweepData& sweep, double z0, QString& err)
{
    if (sweep.xKind != XKind::Frequency) {
        err = QStringLiteral("Touchstone needs a frequency sweep");
        return false;
    }
    if (sweep.mask != 0x0F) {
        err = QStringLiteral("An .s2p file needs all four S-parameters");
        return false;
    }
    QFile f(path);
    if (!openForWrite(f, err))
        return false;
    QTextStream out(&f);
    header(out, sweep, z0);
    out << "! freq  S11re S11im  S21re S21im  S12re S12im  S22re S22im\n";
    for (size_t i = 0; i < sweep.x.size(); ++i) {
        out << num(sweep.x[i]);
        for (const SParam p : {SParam::S11, SParam::S21, SParam::S12, SParam::S22}) {
            const Cplx c = sweep.param(p)[i];
            out << ' ' << num(c.real()) << ' ' << num(c.imag());
        }
        out << '\n';
    }
    return true;
}

bool writeS1p(const QString& path, const SweepData& sweep, SParam param, double z0, QString& err)
{
    if (sweep.xKind != XKind::Frequency) {
        err = QStringLiteral("Touchstone needs a frequency sweep");
        return false;
    }
    if (!sweep.has(param)) {
        err = QStringLiteral("%1 was not measured").arg(sParamName(param));
        return false;
    }
    QFile f(path);
    if (!openForWrite(f, err))
        return false;
    QTextStream out(&f);
    header(out, sweep, z0);
    out << "! freq  " << sParamName(param) << "re " << sParamName(param) << "im\n";
    const auto& v = sweep.param(param);
    for (size_t i = 0; i < sweep.x.size(); ++i)
        out << num(sweep.x[i]) << ' ' << num(v[i].real()) << ' ' << num(v[i].imag()) << '\n';
    return true;
}

bool read(const QString& path, SweepData& out, double& z0, QString& err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        err = QStringLiteral("Could not read %1: %2").arg(path, f.errorString());
        return false;
    }

    const QString suffix = QFileInfo(path).suffix().toLower();
    const int ports = suffix == QLatin1String("s1p") ? 1 : suffix == QLatin1String("s2p") ? 2 : 0;
    if (ports == 0) {
        err = QStringLiteral("Only .s1p and .s2p files are supported");
        return false;
    }

    double unit = 1e9;                  // Touchstone default is GHz
    QString format = QStringLiteral("MA");
    z0 = 50.0;

    out = SweepData{};
    out.label = QFileInfo(path).fileName();
    out.mask = ports == 1 ? 0x01 : 0x0F;

    static const QRegularExpression ws(QStringLiteral("\\s+"));
    QList<double> values;
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine();
        const qsizetype bang = line.indexOf(QLatin1Char('!'));
        if (bang >= 0)
            line.truncate(bang);
        line = line.trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1Char('#'))) {
            const QStringList opt = line.mid(1).toUpper().split(ws, Qt::SkipEmptyParts);
            for (qsizetype i = 0; i < opt.size(); ++i) {
                const QString& o = opt[i];
                if (o == QLatin1String("HZ")) unit = 1.0;
                else if (o == QLatin1String("KHZ")) unit = 1e3;
                else if (o == QLatin1String("MHZ")) unit = 1e6;
                else if (o == QLatin1String("GHZ")) unit = 1e9;
                else if (o == QLatin1String("RI") || o == QLatin1String("MA") || o == QLatin1String("DB")) format = o;
                else if (o == QLatin1String("R") && i + 1 < opt.size()) z0 = opt[++i].toDouble();
                else if (o != QLatin1String("S")) {
                    err = QStringLiteral("Unsupported Touchstone option '%1'").arg(o);
                    return false;
                }
            }
            continue;
        }

        for (const QString& tok : line.split(ws, Qt::SkipEmptyParts)) {
            bool ok = false;
            const double v = tok.toDouble(&ok);
            if (!ok) {
                err = QStringLiteral("Not a number: '%1'").arg(tok);
                return false;
            }
            values.append(v);
        }
    }

    const int perRow = ports == 1 ? 3 : 9;
    if (values.isEmpty() || values.size() % perRow != 0) {
        err = QStringLiteral("Expected %1 values per frequency, file has %2 values")
                  .arg(perRow).arg(values.size());
        return false;
    }

    const qsizetype rows = values.size() / perRow;
    out.x.reserve(size_t(rows));
    for (qsizetype r = 0; r < rows; ++r) {
        const qsizetype base = r * perRow;
        out.x.push_back(values[base] * unit);
        if (ports == 1) {
            out.s[0].push_back(fromPair(values[base + 1], values[base + 2], format));
        } else {
            // 2-port column order is S11 S21 S12 S22, which is also the
            // SParam index order.
            for (int k = 0; k < 4; ++k)
                out.s[size_t(k)].push_back(fromPair(values[base + 1 + 2 * k], values[base + 2 + 2 * k], format));
        }
    }
    return true;
}

bool writeCsv(const QString& path, const SweepData& sweep, QString& err)
{
    QFile f(path);
    if (!openForWrite(f, err))
        return false;
    QTextStream out(&f);

    switch (sweep.xKind) {
    case XKind::Frequency: out << "Frequency (Hz)"; break;
    case XKind::Power:     out << "Power (dBm)"; break;
    case XKind::Time:      out << "Time (s)"; break;
    case XKind::Distance:  out << "Distance (m)"; break;
    }
    for (int k = 0; k < kSParamCount; ++k) {
        const SParam p = static_cast<SParam>(k);
        if (sweep.has(p))
            out << ',' << sParamName(p) << " re," << sParamName(p) << " im," << sParamName(p) << " dB,"
                << sParamName(p) << " deg";
    }
    out << '\n';

    for (size_t i = 0; i < sweep.x.size(); ++i) {
        out << num(sweep.x[i]);
        for (int k = 0; k < kSParamCount; ++k) {
            const SParam p = static_cast<SParam>(k);
            if (!sweep.has(p))
                continue;
            const Cplx c = sweep.param(p)[i];
            out << ',' << num(c.real()) << ',' << num(c.imag()) << ','
                << num(20.0 * std::log10(std::max(std::abs(c), 1e-15))) << ','
                << num(std::arg(c) * 180.0 / M_PI);
        }
        out << '\n';
    }
    return true;
}

} // namespace Touchstone
