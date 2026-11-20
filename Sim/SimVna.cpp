#include "SimVna.h"

#include "Instrument/VnaFrame.h"
#include "Scpi/ScpiParse.h"

#include <cmath>

namespace {

const QByteArray kUnsupported = "GATEWAY_UNSUPPORTED_COMMAND\n";
const QByteArray kInvalid     = "GATEWAY_INVALID_PARAMETER\n";

constexpr double kMinFreq = 100e3, kMaxFreq = 6.4e9;
constexpr double kMinIfbw = 15, kMaxIfbw = 75e3;
// The instrument reports 2048 from MAXPoints? yet accepts up to 10001. Sweep
// groups, however, only work up to the reported figure.
constexpr int    kMaxPoints = 2048;
constexpr int    kAcceptedPoints = 10001;
constexpr double kMinPower = -40, kMaxPower = 5;

// Matches one header node against a pattern node written long-form with the
// short form in capitals, e.g. "ACQuisition" accepts ACQ and ACQUISITION.
bool nodeMatches(const QByteArray& pattern, const QByteArray& node)
{
    QByteArray shortForm;
    for (const char c : pattern)
        if (!(c >= 'a' && c <= 'z'))
            shortForm.append(c);
    const QByteArray n = node.toUpper();
    return n == shortForm || n == pattern.toUpper();
}

bool headerMatches(const char* pattern, const QByteArray& header)
{
    const QList<QByteArray> p = QByteArray(pattern).split(':');
    const QList<QByteArray> h = header.split(':');
    if (p.size() != h.size())
        return false;
    for (qsizetype i = 0; i < p.size(); ++i)
        if (!nodeMatches(p[i], h[i]))
            return false;
    return true;
}

QByteArray num(double v) { return QByteArray::number(v, 'g', 12) + '\n'; }
QByteArray boolean(bool b) { return b ? "TRUE\n" : "FALSE\n"; }

// Numeric argument with no unit suffix, as the instrument requires.
bool numericArg(const QByteArray& arg, double& out)
{
    return !arg.isEmpty() && ScpiParse::toDouble(arg, out);
}

} // namespace

SimVna::SimVna() = default;

Cplx SimVna::dut(SParam p, double f)
{
    using namespace std::complex_literals;
    constexpr double f0 = 2.4e9, bw = 200e6;
    // Third-order Butterworth band-pass through the usual low-pass mapping.
    const double w = (f / f0 - f0 / std::max(f, 1.0)) / (bw / f0);
    const Cplx s(0.0, w);
    const Cplx den = (s + 1.0) * (s * s + s + 1.0);
    const Cplx h21 = 1.0 / den;
    const Cplx h11 = (s * s * s) / den;

    const double tau = 1.2e-9;                  // through delay
    const Cplx thru = 0.93 * h21 * std::exp(Cplx(0.0, -2.0 * M_PI * f * tau));

    switch (p) {
    case SParam::S21:
    case SParam::S12:
        return thru;
    case SParam::S11: {
        // Filter input sits 0.9 m of 0.7-velocity cable away (round trip
        // 8.57 ns) and there is a small discontinuity at 0.45 m (4.29 ns).
        const double tFilter = 2.0 * 0.9 / (0.7 * 299792458.0);
        const double tFault  = 2.0 * 0.45 / (0.7 * 299792458.0);
        return 0.95 * h11 * std::exp(Cplx(0.0, -2.0 * M_PI * f * tFilter))
             + 0.08 * std::exp(Cplx(0.0, -2.0 * M_PI * f * tFault));
    }
    case SParam::S22:
        return 0.95 * h11 * std::exp(Cplx(0.0, -2.0 * M_PI * f * 0.6e-9));
    }
    return Cplx();
}

std::vector<double> SimVna::stimulusAxis() const
{
    const int n = _st.points;
    std::vector<double> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double t = n > 1 ? double(i) / (n - 1) : 0.0;
        switch (_st.mode) {
        case SweepMode::LinFreq: x[size_t(i)] = _st.start + t * (_st.stop - _st.start); break;
        case SweepMode::LogFreq: x[size_t(i)] = _st.start * std::pow(_st.stop / _st.start, t); break;
        case SweepMode::Power:   x[size_t(i)] = _st.powerStart + t * (_st.powerStop - _st.powerStart); break;
        case SweepMode::CW:      x[size_t(i)] = t * n / 2164.0; break;
        }
    }
    return x;
}

SweepData SimVna::makeSweep()
{
    SweepData d;
    d.x = stimulusAxis();
    d.mask = 0x0F;
    d.sequence = _sweepSeq++;

    const double sigma = _noise ? 3e-6 * std::sqrt(_st.ifbw / 1000.0) : 0.0;
    std::normal_distribution<double> g(0.0, 1.0);

    for (int k = 0; k < kSParamCount; ++k) {
        auto& v = d.s[size_t(k)];
        v.resize(d.x.size());
        for (size_t i = 0; i < d.x.size(); ++i) {
            double f = d.x[i];
            if (_st.mode == SweepMode::Power)
                f = _st.cwFreq;
            else if (_st.mode == SweepMode::CW)
                f = 0.5 * (_st.start + _st.stop);
            Cplx c = dut(static_cast<SParam>(k), f);
            if (sigma > 0)
                c += Cplx(sigma * g(_rng), sigma * g(_rng));
            v[i] = c;
        }
    }
    return d;
}

QByteArray SimVna::execute(const QByteArray& rawLine)
{
    const QByteArray line = rawLine.trimmed();
    if (line.isEmpty())
        return QByteArray();
    if (line.contains(';'))
        return kUnsupported;

    const qsizetype space = line.indexOf(' ');
    const QByteArray header = space < 0 ? line : line.left(space);
    const QByteArray arg = space < 0 ? QByteArray() : line.mid(space + 1).trimmed();

    if (header.endsWith('?'))
        return handleQuery(header.left(header.size() - 1), arg);
    return handleCommand(header, arg);
}

QByteArray SimVna::handleQuery(const QByteArray& h, const QByteArray& arg)
{
    const QByteArray serial = "SIM000000001";

    if (h.toUpper() == "*IDN") return "Harogic,VNB-60," + serial + ",1.6.5\n";
    if (h.toUpper() == "*OPC") return "1\n";
    if (h.toUpper() == "*ESR") return "0\n";
    if (h.toUpper() == "*LST") {
        const QByteArray list = "*IDN?\n*RST\n*OPC?\n*ESR?\n*LST?\n";
        return "#" + QByteArray::number(QByteArray::number(list.size()).size())
             + QByteArray::number(list.size()) + list + "\n";
    }

    if (headerMatches("DEVice:CONNect", h)) return serial + "\n";
    if (headerMatches("CLIENT:INFo", h)) return "VNB-60," + serial + ",1.0.6\n";
    if (headerMatches("CLIENT:SETUP:LOAD", h))
        return arg.isEmpty() ? kInvalid : QByteArray("GATEWAY_EXECUTION_ERROR\n");
    if (headerMatches("VNA:CALibration:LOAD", h))
        return arg.isEmpty() ? kInvalid : QByteArray("FALSE\n");

    if (headerMatches("DEVice:CONFiguration:PLLSETTlingdelay", h)) return num(_pllSettle);
    if (headerMatches("DEVice:REFerence:IN", h)) return _st.externalRef ? "EXT\n" : "INT\n";
    if (headerMatches("DEVice:STAtus:UNLOcked", h)) return boolean(false);
    if (headerMatches("DEVice:STAtus:ADCOVERload", h)) return boolean(false);
    if (headerMatches("DEVice:STAtus:UNLEVel", h)) return boolean(false);
    if (headerMatches("DEVice:INFo:FWREVision", h)) return "1.8.6\n";
    if (headerMatches("DEVice:INFo:HWREVision", h)) return "HAR0 Rev.3\n";
    if (headerMatches("DEVice:INFo:LIMits:MINFrequency", h)) return num(kMinFreq);
    if (headerMatches("DEVice:INFo:LIMits:MAXFrequency", h)) return num(kMaxFreq);
    if (headerMatches("DEVice:INFo:LIMits:MINIFBW", h)) return num(kMinIfbw);
    if (headerMatches("DEVice:INFo:LIMits:MAXIFBW", h)) return num(kMaxIfbw);
    if (headerMatches("DEVice:INFo:LIMits:MAXPoints", h)) return num(kMaxPoints);
    if (headerMatches("DEVice:INFo:LIMits:MINPOWer", h)) return num(kMinPower);
    if (headerMatches("DEVice:INFo:LIMits:MAXPOWer", h)) return num(kMaxPower);
    if (headerMatches("DEVice:INFo:TEMPeratures", h)) return "31\n";

    if (headerMatches("VNA:SWEEP", h)) {
        switch (_st.mode) {
        case SweepMode::Power: return "POWER\n";
        case SweepMode::CW:    return "CW\n";
        default:               return "FREQUENCY\n";
        }
    }
    if (headerMatches("VNA:SWEEPTYPE", h)) return _st.mode == SweepMode::LogFreq ? "LOG\n" : "LIN\n";
    if (headerMatches("VNA:FREQuency:START", h)) return num(_st.start);
    if (headerMatches("VNA:FREQuency:STOP", h)) return num(_st.stop);
    if (headerMatches("VNA:POWer:START", h)) return num(_st.powerStart);
    if (headerMatches("VNA:POWer:STOP", h)) return num(_st.powerStop);
    if (headerMatches("VNA:STIMulus:LVL", h)) return num(_st.power);
    if (headerMatches("VNA:STIMulus:FREQuency", h)) return num(_st.cwFreq);
    if (headerMatches("VNA:ACQuisition:IFBW", h)) return num(_st.ifbw);
    if (headerMatches("VNA:ACQuisition:DWELLtime", h)) return num(_st.dwell);
    if (headerMatches("VNA:ACQuisition:POINTS", h)) return num(_st.points);
    if (headerMatches("VNA:ACQuisition:AVG", h)) return num(_avg);
    if (headerMatches("VNA:ACQuisition:RUN", h)) return boolean(_st.deviceRunning);
    if (headerMatches("VNA:ACQuisition:SINGLE", h)) return boolean(_single);
    if (headerMatches("VNA:ACQuisition:FREQuency", h)) return num(_st.stop);
    // Fitted to the instrument: 401 points take 0.185 s at 10 kHz and 11.7 s
    // at 100 Hz. Only the reported figure; the simulator still answers at once.
    const double perPoint = 2.9 / _st.ifbw + 0.00017;
    if (headerMatches("VNA:ACQuisition:SWEep:TIME", h)) return num(_st.points * perPoint);
    if (headerMatches("VNA:ACQuisition:SWEep:SPEed", h)) return num(1.0 / perPoint);
    if (headerMatches("VNA:ACQuisition:SWEep:VALID", h)) return boolean(true);
    if (headerMatches("VNA:ACQuisition:RAWRx", h)) return boolean(_rawRx);
    if (headerMatches("VNA:ACQuisition:RAWRx:FRAME", h)) return "NO_DATA\n";
    if (headerMatches("VNA:ACQuisition:GROups:COUNt", h)) return num(_groupCount);
    if (headerMatches("VNA:ACQuisition:GROups:PERiod", h)) return "0,0\n";
    if (headerMatches("VNA:ACQuisition:GROups:CONTinuous", h)) return "OFF\n";
    if (headerMatches("VNA:ACQuisition:GROups:STATus", h)) {
        if (_groupError)
            return "ERROR," + QByteArray::number(_groupId) + ",0," + QByteArray::number(_groupCount)
                 + ",CONFIG_CHANGED\n";
        if (!_groupDone)
            return "IDLE,0,0," + QByteArray::number(_groupCount) + ",NONE\n";
        return "DONE," + QByteArray::number(_groupId) + "," + QByteArray::number(_group.size())
             + "," + QByteArray::number(_group.size()) + ",NONE\n";
    }
    if (headerMatches("VNA:TRACe:GROups:DATA", h)) {
        if (!_groupDone)
            return "GROUP_NOT_READY\n";
        const QByteArray block = VnaFrame::buildGroup(_group, _groupId, _st.ifbw, _group.first().sequence);
        const QByteArray len = QByteArray::number(block.size());
        return "#" + QByteArray::number(len.size()) + len + block + "\n";
    }
    if (headerMatches("VNA:TRACe:PAUSED", h)) {
        SParam p;
        return sParamFromName(QString::fromLatin1(arg), p) ? boolean(false) : kInvalid;
    }
    if (headerMatches("VNA:TRACe:DATA", h)) {
        SParam p;
        if (!sParamFromName(QString::fromLatin1(arg), p))
            return kInvalid;
        const SweepData d = makeSweep();
        QByteArray out;
        const auto& v = d.param(p);
        for (size_t i = 0; i < d.x.size(); ++i) {
            if (i)
                out += ',';
            out += '[' + QByteArray::number(d.x[i], 'g', 10) + ','
                 + QByteArray::number(v[i].real(), 'g', 6) + ','
                 + QByteArray::number(v[i].imag(), 'g', 6) + ']';
        }
        return out + '\n';
    }
    return kUnsupported;
}

QByteArray SimVna::handleCommand(const QByteArray& h, const QByteArray& arg)
{
    double v = 0;
    const QByteArray upperArg = arg.toUpper();

    if (h.toUpper() == "*RST") {
        _st = Stimulus{};
        _groupDone = false;
        return QByteArray();
    }
    if (headerMatches("CLIENT:SETUP:SAVE", h))
        return arg.isEmpty() ? kInvalid : QByteArray();

    if (headerMatches("DEVice:CONFiguration:PLLSETTlingdelay", h)) {
        if (!numericArg(arg, v) || v < 10 || v > 255) return kInvalid;
        _pllSettle = int(v);
        return QByteArray();
    }
    if (headerMatches("DEVice:REFerence:IN", h)) {
        if (upperArg == "INT") _st.externalRef = false;
        else if (upperArg == "EXT") _st.externalRef = true;
        else return kInvalid;
        return QByteArray();
    }
    if (headerMatches("VNA:SWEEP", h)) {
        if (upperArg == "FREQUENCY") { if (_st.mode != SweepMode::LogFreq) _st.mode = SweepMode::LinFreq; }
        else if (upperArg == "POWER") _st.mode = SweepMode::Power;
        else if (upperArg == "CW") _st.mode = SweepMode::CW;
        else return kInvalid;
        return QByteArray();
    }
    if (headerMatches("VNA:SWEEPTYPE", h)) {
        if (upperArg != "LIN" && upperArg != "LOG") return kInvalid;
        // Only meaningful in frequency mode; silently ignored otherwise.
        if (_st.mode == SweepMode::LinFreq || _st.mode == SweepMode::LogFreq)
            _st.mode = upperArg == "LOG" ? SweepMode::LogFreq : SweepMode::LinFreq;
        return QByteArray();
    }
    if (headerMatches("VNA:FREQuency:START", h)) {
        if (!numericArg(arg, v) || v < kMinFreq || v >= _st.stop) return kInvalid;
        _st.start = v;
        return QByteArray();
    }
    if (headerMatches("VNA:FREQuency:STOP", h)) {
        if (!numericArg(arg, v) || v > kMaxFreq || v <= _st.start) return kInvalid;
        _st.stop = v;
        return QByteArray();
    }
    if (headerMatches("VNA:POWer:START", h)) {
        if (!numericArg(arg, v) || v < kMinPower || v > kMaxPower) return kInvalid;
        _st.powerStart = v;
        return QByteArray();
    }
    if (headerMatches("VNA:POWer:STOP", h)) {
        if (!numericArg(arg, v) || v < kMinPower || v > kMaxPower) return kInvalid;
        _st.powerStop = v;
        return QByteArray();
    }
    if (headerMatches("VNA:STIMulus:LVL", h)) {
        if (!numericArg(arg, v) || v < kMinPower || v > kMaxPower) return kInvalid;
        _st.power = v;
        return QByteArray();
    }
    if (headerMatches("VNA:STIMulus:FREQuency", h)) {
        if (!numericArg(arg, v) || v < kMinFreq || v > kMaxFreq) return kInvalid;
        _st.cwFreq = v;
        return QByteArray();
    }
    if (headerMatches("VNA:ACQuisition:IFBW", h)) {
        if (!numericArg(arg, v) || v < kMinIfbw || v > kMaxIfbw) return kInvalid;
        _st.ifbw = v;
        return QByteArray();
    }
    if (headerMatches("VNA:ACQuisition:DWELLtime", h)) {
        if (!numericArg(arg, v) || v < 0) return kInvalid;
        _st.dwell = v;
        return QByteArray();
    }
    if (headerMatches("VNA:ACQuisition:POINTS", h)) {
        if (!numericArg(arg, v) || v < 2 || v > kAcceptedPoints) return kInvalid;
        _st.points = int(v);
        return QByteArray();
    }
    if (headerMatches("VNA:ACQuisition:AVG", h)) {
        if (!numericArg(arg, v) || v < 1 || v > 99) return kInvalid;
        _avg = int(v);
        return QByteArray();
    }
    if (headerMatches("VNA:ACQuisition:RUN", h)) { _st.deviceRunning = true; return QByteArray(); }
    if (headerMatches("VNA:ACQuisition:STOP", h)) { _st.deviceRunning = false; return QByteArray(); }
    if (headerMatches("VNA:ACQuisition:SINGLE", h)) {
        bool b = false;
        if (!ScpiParse::toBool(arg, b)) return kInvalid;
        _single = b;
        return QByteArray();
    }
    if (headerMatches("VNA:ACQuisition:RAWRx", h)) {
        bool b = false;
        if (!ScpiParse::toBool(arg, b)) return kInvalid;
        _rawRx = b;
        return QByteArray();
    }
    if (headerMatches("VNA:ACQuisition:GROups:COUNt", h)) {
        if (!numericArg(arg, v) || v < 1) return kInvalid;
        _groupCount = int(v);
        return QByteArray();
    }
    if (headerMatches("VNA:ACQuisition:GROups:INITiate", h)) {
        if (_st.points > kMaxPoints) {
            // The previous group stays readable, as on the instrument.
            _groupError = true;
            return "GATEWAY_EXECUTION_ERROR\n";
        }
        _groupError = false;
        _group.clear();
        for (int i = 0; i < _groupCount; ++i)
            _group.append(makeSweep());
        ++_groupId;
        _groupDone = true;
        return QByteArray();
    }
    if (headerMatches("VNA:ACQuisition:GROups:ABORt", h))
        return kInvalid;    // nothing is ever mid-flight here; matches the idle instrument
    if (headerMatches("VNA:TRACe:PAUSE", h) || headerMatches("VNA:TRACe:RESUME", h)) {
        SParam p;
        return sParamFromName(QString::fromLatin1(arg), p) ? QByteArray() : kInvalid;
    }
    return kUnsupported;
}
