// VnaBench - headless regression checks for vna_core.
//
// Runs with no hardware and no display: the instrument is the in-process
// SimVna behind the same transport interface the TCP path uses.
//
//   VnaBench [--verbose] [--out results.json]
//   exit 0 = all passed, 1 = failures, 2 = usage error

#include "BenchHarness.h"

#include "Data/Touchstone.h"
#include "Data/TraceModel.h"
#include "Data/TracePipeline.h"
#include "Data/Workspace.h"
#include "Dsp/Fft.h"
#include "Dsp/Formats.h"
#include "Dsp/MarkerSearch.h"
#include "Dsp/TimeDomain.h"
#include "Instrument/VnaDevice.h"
#include "Instrument/VnaFrame.h"
#include "Scpi/ScpiParse.h"
#include "Sim/SimVna.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include <cmath>
#include <functional>

namespace {

// Runs the event loop until `done` returns true or `timeoutMs` passes.
bool waitFor(const std::function<bool()>& done, int timeoutMs = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

// A noise-free frequency sweep of the simulated DUT.
SweepData dutSweep(double start, double stop, int points)
{
    SweepData d;
    d.mask = 0x0F;
    d.x.resize(size_t(points));
    for (int i = 0; i < points; ++i)
        d.x[size_t(i)] = start + (stop - start) * i / double(points - 1);
    for (int k = 0; k < kSParamCount; ++k) {
        d.s[size_t(k)].resize(size_t(points));
        for (int i = 0; i < points; ++i)
            d.s[size_t(k)][size_t(i)] = SimVna::dut(static_cast<SParam>(k), d.x[size_t(i)]);
    }
    return d;
}

void benchParse(Bench& b)
{
    b.group("parse");

    QByteArray buf = "hello\r\nwor";
    QByteArray line;
    b.check("takeLine strips CRLF", ScpiParse::takeLine(buf, line) && line == "hello");
    b.check("takeLine waits for a terminator", !ScpiParse::takeLine(buf, line) && buf == "wor");

    buf = "#15ABCDE\n401\n";
    QByteArray out;
    b.check("block reply", ScpiParse::takeReply(buf, out) == ScpiParse::ReplyResult::Block && out == "ABCDE");
    b.check("block consumes its trailing LF",
            ScpiParse::takeReply(buf, out) == ScpiParse::ReplyResult::Line && out == "401");

    buf = "#210ABCDE";
    b.check("partial block waits", ScpiParse::takeReply(buf, out) == ScpiParse::ReplyResult::NeedMoreData);
    buf = "#15ABCDE";
    b.check("block without its LF yet waits", ScpiParse::takeReply(buf, out) == ScpiParse::ReplyResult::NeedMoreData);
    buf = "#x5ABCDE\n";
    b.check("bad block header", ScpiParse::takeReply(buf, out) == ScpiParse::ReplyResult::Malformed);

    b.check("GATEWAY_INVALID_PARAMETER is an error", ScpiParse::isErrorToken("GATEWAY_INVALID_PARAMETER"));
    b.check("NO_DATA is an error", ScpiParse::isErrorToken("NO_DATA"));
    b.check("TRUE is not an error", !ScpiParse::isErrorToken("TRUE"));
    b.check("FREQUENCY is not an error", !ScpiParse::isErrorToken("FREQUENCY"));
    b.check("a number is not an error", !ScpiParse::isErrorToken("6.4e+09"));
    b.check("group status is not an error", !ScpiParse::isErrorToken("DONE,3,1,1,NONE"));

    b.check("isQuery plain", ScpiParse::isQuery("VNA:ACQ:POINTS?"));
    b.check("isQuery with argument", ScpiParse::isQuery("VNA:TRAC:DATA? S11"));
    b.check("isQuery false for a command", !ScpiParse::isQuery("VNA:ACQ:POINTS 201"));

    std::vector<double> x;
    std::vector<Cplx> v;
    b.check("triplets parse",
            ScpiParse::parseTriplets("[10000000,-0.396112,0.0774038],[3005000000,-0.827538,0.111542]", x, v)
                && x.size() == 2 && x[1] == 3005000000.0);
    if (v.size() == 2)
        b.checkNear("triplet value", v[1].real(), -0.827538, 1e-9);
    b.check("malformed triplets rejected", !ScpiParse::parseTriplets("[1,2],[3,4,5]", x, v));
}

void benchFrame(Bench& b)
{
    b.group("frame");

    // A block captured from the real instrument: one 3-point sweep, taken
    // with acquisition stopped. The expected values are what the instrument's
    // own ASCII VNA:TRAC:DATA? queries returned for the same sweep.
    const QByteArray real = QByteArray::fromHex(
        "5647423100010014000000000000000300000001000000be085c9cfc0000010056544631000100880000011501000000"
        "45442304cdd24c91811f66035a46a8aa00000000000000020000000000000047000000000000000d0000000000000019"
        "00000000000010b900000000000062a9000000030000000f0000000100000001416312d00000000041f65a0bc0000000"
        "40c388000000000041f65a0bc00000000000007875d90567416312d00000000041e663952800000041f65a0bc0000000"
        "becacf23bf53d981beb17e553d9e85e13de46ff1bebedd06be2726bfb8456a58bab1d5cbbf5e3b1bb6da948dbe1b92bf"
        "be261c5cb8636d64bb21cec8bf5e24b6b7ecfa56be1faa53becc57d0bf51998bbef7efca3da595d63e4a71eabe7e127f"
        "e90a84c2");

    QList<SweepData> sweeps;
    QString err;
    b.check("captured block parses", VnaFrame::parseGroup(real, sweeps, err), err);
    if (sweeps.size() == 1) {
        const SweepData& s = sweeps.first();
        b.checkEq("captured points", s.points(), 3);
        b.checkEq("captured mask", int(s.mask), 0x0F);
        b.checkNear("captured x[0]", s.x[0], 10e6, 1);
        b.checkNear("captured x[1]", s.x[1], 3005e6, 1);
        b.checkNear("captured x[2]", s.x[2], 6e9, 1);
        b.checkNear("S11[0] re", s.param(SParam::S11)[0].real(), -0.396112, 1e-6);
        b.checkNear("S11[0] im", s.param(SParam::S11)[0].imag(), 0.0774038, 1e-6);
        b.checkNear("S21[0] re", s.param(SParam::S21)[0].real(), -0.163234, 1e-6);
        b.checkNear("S21[0] im", s.param(SParam::S21)[0].imag(), -0.868089, 1e-6);
        b.checkNear("S12[0] re", s.param(SParam::S12)[0].real(), -0.162218, 1e-6);
        b.checkNear("S12[2] im", s.param(SParam::S12)[2].imag(), -0.155923, 1e-6);
        b.checkNear("S22[2] re", s.param(SParam::S22)[2].real(), -0.484251, 1e-6);
        b.checkNear("S22[2] im", s.param(SParam::S22)[2].imag(), -0.248117, 1e-6);
        b.checkEq("captured sweep counter", int(s.sequence), 0x10b9);
    } else {
        b.check("captured block has one frame", false, QString::number(sweeps.size()));
    }

    const SweepData a = dutSweep(1e9, 3e9, 51);
    const QByteArray built = VnaFrame::buildGroup({a, a}, 7, 10e3, 100);
    b.check("built block parses", VnaFrame::parseGroup(built, sweeps, err), err);
    b.checkEq("built frame count", int(sweeps.size()), 2);
    if (sweeps.size() == 2) {
        b.checkEq("built points", sweeps[1].points(), 51);
        b.checkNear("built round trip", sweeps[1].param(SParam::S21)[25].real(),
                    a.param(SParam::S21)[25].real(), 1e-6);
        b.checkEq("built sequence", int(sweeps[1].sequence), 101);
    }

    b.check("truncated block rejected", !VnaFrame::parseGroup(built.left(built.size() / 2), sweeps, err));
    b.check("garbage rejected", !VnaFrame::parseGroup("not a block at all....", sweeps, err));
}

void benchSim(Bench& b)
{
    b.group("simulator");
    SimVna sim;

    b.check("*IDN?", sim.execute("*IDN?").startsWith("Harogic,VNB-60,"));
    b.checkEq("query", sim.execute("VNA:ACQ:POINTS?"), QByteArray("401\n"));
    b.checkEq("set is silent on success", sim.execute("VNA:ACQ:POINTS 201"), QByteArray());
    b.checkEq("set took effect", sim.execute("vna:acquisition:points?"), QByteArray("201\n"));
    b.checkEq("out of range rejected", sim.execute("VNA:ACQ:POINTS 99999"), QByteArray("GATEWAY_INVALID_PARAMETER\n"));
    b.checkEq("rejected value not applied", sim.execute("VNA:ACQ:POINTS?"), QByteArray("201\n"));
    b.checkEq("unit suffix rejected", sim.execute("VNA:FREQ:START 1GHz"), QByteArray("GATEWAY_INVALID_PARAMETER\n"));
    b.checkEq("exponent accepted", sim.execute("VNA:FREQ:START 1e7"), QByteArray());
    b.checkEq("unknown command", sim.execute("FOO:BAR?"), QByteArray("GATEWAY_UNSUPPORTED_COMMAND\n"));
    b.checkEq("semicolon chaining unsupported", sim.execute("VNA:ACQ:IFBW?;VNA:ACQ:AVG?"),
              QByteArray("GATEWAY_UNSUPPORTED_COMMAND\n"));
    b.checkEq("trace query needs a parameter", sim.execute("VNA:TRAC:DATA?"), QByteArray("GATEWAY_INVALID_PARAMETER\n"));
    b.checkEq("group not ready before INIT", sim.execute("VNA:TRAC:GRO:DATA?"), QByteArray("GROUP_NOT_READY\n"));
    b.check("group idle status", sim.execute("VNA:ACQ:GRO:STAT?").startsWith("IDLE,"));
    b.checkEq("group init is silent", sim.execute("VNA:ACQ:GRO:INIT"), QByteArray());
    b.check("group done status", sim.execute("VNA:ACQ:GRO:STAT?").startsWith("DONE,1,1,1"));
    b.check("group data is a block", sim.execute("VNA:TRAC:GRO:DATA?").startsWith("#"));
    b.checkEq("IFBW below minimum rejected", sim.execute("VNA:ACQ:IFBW 1"), QByteArray("GATEWAY_INVALID_PARAMETER\n"));
    b.checkEq("points above the reported maximum accepted", sim.execute("VNA:ACQ:POINTS 2049"), QByteArray());
    b.checkEq("group refused above the reported maximum", sim.execute("VNA:ACQ:GRO:INIT"),
              QByteArray("GATEWAY_EXECUTION_ERROR\n"));
    b.check("group status reports the error", sim.execute("VNA:ACQ:GRO:STAT?").startsWith("ERROR,"));
    b.check("stale group still served after the error", sim.execute("VNA:TRAC:GRO:DATA?").startsWith("#"));
    b.checkEq("points above 10001 rejected", sim.execute("VNA:ACQ:POINTS 10002"), QByteArray("GATEWAY_INVALID_PARAMETER\n"));
    sim.execute("VNA:ACQ:POINTS 201");

    std::vector<double> x;
    std::vector<Cplx> v;
    QByteArray reply = sim.execute("VNA:TRAC:DATA? S21");
    reply.chop(1);
    b.check("ASCII trace parses", ScpiParse::parseTriplets(reply, x, v) && x.size() == 201);
}

void benchDevice(Bench& b)
{
    b.group("device (simulated)");

    VnaDevice dev;
    QStringList messages;
    int sweeps = 0;
    SweepData last;
    QObject::connect(&dev, &VnaDevice::message, [&](const QString& m) { messages << m; });
    QObject::connect(&dev, &VnaDevice::sweepReady, [&](const SweepData& s) { ++sweeps; last = s; });

    ScpiEndpoint ep;
    ep.simulated = true;
    dev.connectTo(ep);
    b.check("connects", waitFor([&] { return dev.isReady(); }));
    b.checkEq("model", dev.info().model, QStringLiteral("VNB-60"));
    b.checkEq("firmware", dev.info().firmware, QStringLiteral("1.8.6"));
    b.checkEq("max points from the instrument", dev.info().maxPoints, 2048);
    b.checkNear("max frequency from the instrument", dev.info().maxFreq, 6.4e9, 1);
    b.checkEq("initial points", dev.stimulus().points, 401);
    b.check("initial mode", dev.stimulus().mode == SweepMode::LinFreq);

    b.check("continuous sweeps arrive", waitFor([&] { return sweeps >= 3; }));
    b.checkEq("sweep has all four parameters", int(last.mask), 0x0F);
    b.checkEq("sweep points", last.points(), 401);

    int busySignals = 0;
    bool sawBusy = false;
    QObject::connect(&dev, &VnaDevice::busyChanged, [&] { ++busySignals; sawBusy |= dev.isBusy(); });
    b.check("not busy before a setting", !dev.isBusy());
    dev.setPoints(201);
    b.check("busy while a setting is unconfirmed", dev.isBusy() && dev.busyNote() == QLatin1String("Points"),
            dev.busyNote());
    b.check("points read back", waitFor([&] { return dev.stimulus().points == 201; }));
    b.check("busy clears once confirmed", waitFor([&] { return !dev.isBusy(); }) && sawBusy && busySignals >= 2,
            QString::number(busySignals));
    // A superseded setting must not leave the flag stuck on.
    for (const int n : {210, 220, 230, 201})
        dev.setPoints(n);
    b.check("busy clears after coalesced settings",
            waitFor([&] { return !dev.isBusy() && dev.stimulus().points == 201; }), QString::number(dev.stimulus().points));
    b.check("next sweep has new points", waitFor([&] { return last.points() == 201; }));

    messages.clear();
    dev.setPoints(99999);
    b.check("refused setting is reported", waitFor([&] { return !messages.isEmpty(); }),
            messages.join(" | "));
    b.check("refusal names the error", messages.join(' ').contains("GATEWAY_INVALID_PARAMETER"),
            messages.join(" | "));
    b.checkEq("refused setting leaves the value", dev.stimulus().points, 201);

    dev.setCenterSpan(2.4e9, 1e9);
    b.check("centre/span becomes start/stop",
            waitFor([&] { return dev.stimulus().start == 1.9e9 && dev.stimulus().stop == 2.9e9; }),
            QString("%1..%2").arg(dev.stimulus().start).arg(dev.stimulus().stop));

    // Entries outside the instrument range land on the nearest end of it.
    messages.clear();
    dev.setStart(1.0);
    b.check("start below the minimum becomes the minimum",
            waitFor([&] { return dev.stimulus().start == dev.info().minFreq; }),
            QString::number(dev.stimulus().start));
    dev.setStop(50e9);
    b.check("stop above the maximum becomes the maximum",
            waitFor([&] { return dev.stimulus().stop == dev.info().maxFreq; }),
            QString::number(dev.stimulus().stop));
    b.check("clamped entries are not refused", messages.isEmpty(), messages.join(" | "));

    // Three settings sent back to back, as the stimulus presets do.
    dev.setPower(-30);
    dev.setIfbw(1e3);
    dev.setPoints(801);
    b.check("three settings in a row all take",
            waitFor([&] { return dev.stimulus().power == -30 && dev.stimulus().ifbw == 1e3
                              && dev.stimulus().points == 801; }),
            QString("%1 dBm, %2 Hz, %3").arg(dev.stimulus().power).arg(dev.stimulus().ifbw).arg(dev.stimulus().points));
    dev.setPower(0);
    dev.setIfbw(10e3);
    dev.setPoints(201);
    waitFor([&] { return dev.stimulus().points == 201 && dev.stimulus().ifbw == 10e3; });

    // Moving the whole range above the old stop: start must not be sent first.
    messages.clear();
    dev.setStartStop(4e9, 5e9);
    b.check("range moved upwards",
            waitFor([&] { return dev.stimulus().start == 4e9 && dev.stimulus().stop == 5e9; }),
            messages.join(" | "));
    b.check("no refusal while moving the range", messages.isEmpty(), messages.join(" | "));
    dev.setStartStop(1.9e9, 2.9e9);
    waitFor([&] { return dev.stimulus().start == 1.9e9; });

    dev.setSweepMode(SweepMode::LogFreq);
    b.check("log sweep", waitFor([&] { return dev.stimulus().mode == SweepMode::LogFreq; }));
    b.check("log sweep flagged on data", waitFor([&] { return last.logSpacing; }));
    dev.setSweepMode(SweepMode::Power);
    b.check("power sweep", waitFor([&] { return dev.stimulus().mode == SweepMode::Power; }));
    b.check("power sweep axis", waitFor([&] { return last.xKind == XKind::Power; }));
    dev.setSweepMode(SweepMode::LinFreq);
    b.check("back to linear", waitFor([&] { return dev.stimulus().mode == SweepMode::LinFreq && last.xKind == XKind::Frequency && !last.logSpacing; }));

    dev.setRunMode(VnaDevice::RunMode::Hold);
    waitFor([] { return false; }, 150);
    const int held = sweeps;
    waitFor([] { return false; }, 200);
    b.checkEq("hold stops sweeps", sweeps, held);
    dev.single();
    b.check("single delivers one sweep", waitFor([&] { return sweeps == held + 1; }));
    waitFor([] { return false; }, 150);
    b.checkEq("single delivers only one", sweeps, held + 1);

    // Above the fast-transfer limit the sweep comes through the text query.
    dev.setAveraging(1);
    dev.setNeededMask(0x03);
    messages.clear();
    dev.setPoints(3001);
    dev.setRunMode(VnaDevice::RunMode::Continuous);
    b.check("sweeps continue above 2048 points", waitFor([&] { return last.points() == 3001; }, 15000),
            QString("%1 | %2").arg(last.points()).arg(messages.join(" | ")));
    b.checkEq("text fallback fetches only what is on screen", int(last.mask), 0x03);
    b.check("the slower transfer is announced once", messages.filter("fast transfer").size() == 1,
            messages.join(" | "));
    dev.setNeededMask(0x0F);
    dev.setPoints(201);
    b.check("fast transfer resumes at 201 points",
            waitFor([&] { return last.points() == 201 && last.mask == 0x0F; }, 15000),
            QString("%1 mask %2 | %3").arg(last.points()).arg(last.mask).arg(messages.join(" | ")));
    dev.setRunMode(VnaDevice::RunMode::Hold);
    waitFor([] { return false; }, 400);

    dev.setAveraging(4);
    dev.single();
    b.check("averaging runs to its count", waitFor([&] { return last.averages == 4; }),
            QString::number(last.averages));

    quint64 rawId = 0;
    ScpiResult rawResult;
    QObject::connect(&dev, &VnaDevice::userReply, [&](quint64 id, const ScpiResult& r) {
        if (id == rawId)
            rawResult = r;
    });
    rawId = dev.sendRaw("FOO:BAR?");
    b.check("console reply arrives", waitFor([&] { return !rawResult.replies.isEmpty(); }));
    b.check("console sees the error token",
            !rawResult.ok && rawResult.firstError() == QLatin1String("GATEWAY_UNSUPPORTED_COMMAND"),
            rawResult.firstError());

    // A reply that arrives after its timeout (the instrument busy with a slow
    // sweep) must not end the connection, must not leave the setting unread,
    // and must not be taken as the answer to the next query.
    waitFor([] { return false; }, 300);
    messages.clear();
    dev.simWithholdReplies(2);          // the command and its *OPC?
    dev.setPoints(301);
    b.check("setting still read back after a late reply",
            waitFor([&] { return dev.stimulus().points == 301; }), messages.join(" | "));
    b.check("still connected after a timeout", dev.isReady());
    b.check("slow confirmation is reported as such, not as a refusal",
            waitFor([&] { return !messages.filter("slow to confirm").isEmpty(); }) && messages.filter("refused").isEmpty(),
            messages.join(" | "));
    rawResult = ScpiResult{};
    rawId = dev.sendRaw("VNA:ACQ:IFBW?");
    b.check("next query gets its own answer, not the late one",
            waitFor([&] { return !rawResult.replies.isEmpty(); }) && rawResult.replies.value(0).text == "10000",
            QString::fromLatin1(rawResult.replies.value(0).text));

    // Taking over pauses the tablet's sweep; giving it back restarts it.
    b.check("tablet sweep running before take-over", dev.stimulus().deviceRunning);
    dev.setTakeOver(true);
    b.check("take-over pauses the tablet sweep", waitFor([&] { return !dev.stimulus().deviceRunning; }));
    const int beforeTakeOver = sweeps;
    dev.single();
    b.check("sweeps still arrive with the tablet paused", waitFor([&] { return sweeps > beforeTakeOver; }));
    dev.setTakeOver(false);
    b.check("turning take-over off restarts the tablet sweep", waitFor([&] { return dev.stimulus().deviceRunning; }));
    waitFor([&] { return !dev.isBusy(); });

    // The stimulus presets go in as one transaction.
    dev.applyPreset(5, 100, 401);
    b.check("preset to a narrow IF bandwidth",
            waitFor([&] { return dev.stimulus().power == 5 && dev.stimulus().ifbw == 100 && dev.stimulus().points == 401; }),
            QString("%1 dBm, %2 Hz, %3").arg(dev.stimulus().power).arg(dev.stimulus().ifbw).arg(dev.stimulus().points));
    dev.applyPreset(0, 10e3, 201);
    b.check("preset back to a wide IF bandwidth",
            waitFor([&] { return dev.stimulus().power == 0 && dev.stimulus().ifbw == 10e3 && dev.stimulus().points == 201; }),
            QString("%1 dBm, %2 Hz, %3").arg(dev.stimulus().power).arg(dev.stimulus().ifbw).arg(dev.stimulus().points));

    dev.disconnectFrom();
    b.check("disconnects", dev.state() == VnaDevice::State::Disconnected);
}

// Optional pass against a real instrument: VnaBench --device host:port.
// Changes points and IF bandwidth briefly and puts both back.
void benchHardware(Bench& b, const QString& host, quint16 port)
{
    b.group("device (hardware)");

    VnaDevice dev;
    QStringList messages;
    int sweeps = 0;
    SweepData last;
    QObject::connect(&dev, &VnaDevice::message, [&](const QString& m) { messages << m; });
    QObject::connect(&dev, &VnaDevice::sweepReady, [&](const SweepData& s) { ++sweeps; last = s; });

    ScpiEndpoint ep;
    ep.host = host;
    ep.port = port;
    dev.connectTo(ep);
    if (!b.check("connects", waitFor([&] { return dev.isReady(); }, 10000), messages.join(" | ")))
        return;

    const Stimulus original = dev.stimulus();
    b.check("identity read", !dev.info().model.isEmpty() && !dev.info().serial.isEmpty());
    b.check("limits read", dev.info().maxPoints >= 201 && dev.info().maxFreq > dev.info().minFreq);

    b.check("sweeps arrive", waitFor([&] { return sweeps >= 3; }, 30000), messages.join(" | "));
    b.checkEq("sweep matches the points setting", last.points(), original.points);
    b.check("sweep has data", last.mask != 0, QString::number(last.mask));

    const int newPoints = original.points == 201 ? 301 : 201;
    dev.setPoints(newPoints);
    b.check("points read back", waitFor([&] { return dev.stimulus().points == newPoints; }, 10000));
    b.check("sweep follows the new points", waitFor([&] { return last.points() == newPoints; }, 30000),
            QString("%1 | %2").arg(last.points()).arg(messages.join(" | ")));

    messages.clear();
    dev.setPoints(20001);
    b.check("out-of-range points refused and reported", waitFor([&] { return !messages.isEmpty(); }, 10000));
    b.checkEq("refused value not applied", dev.stimulus().points, newPoints);

    const double newIfbw = original.ifbw == 1000.0 ? 3000.0 : 1000.0;
    dev.setIfbw(newIfbw);
    b.check("IF bandwidth read back", waitFor([&] { return dev.stimulus().ifbw == newIfbw; }, 10000));

    // A setting changed mid-sweep aborts the instrument's group; the loop
    // must carry on without reporting a failure.
    messages.clear();
    const int before = sweeps;
    b.check("sweeps continue after settings changes", waitFor([&] { return sweeps >= before + 3; }, 60000),
            messages.join(" | "));
    b.check("no acquisition errors", messages.filter("Acquisition").isEmpty(), messages.join(" | "));

    dev.setIfbw(original.ifbw);
    dev.setPoints(original.points);
    b.check("settings restored",
            waitFor([&] { return dev.stimulus().points == original.points && dev.stimulus().ifbw == original.ifbw; }, 10000),
            QString("%1 points, %2 Hz").arg(dev.stimulus().points).arg(dev.stimulus().ifbw));
    dev.disconnectFrom();
}

void benchFormats(Bench& b)
{
    b.group("formats");

    b.checkNear("log mag of 0.1", Formats::applyPoint(Cplx(0.1, 0), TraceFormat::LogMag, 50), -20.0, 1e-9);
    b.checkNear("phase of j", Formats::applyPoint(Cplx(0, 1), TraceFormat::Phase, 50), 90.0, 1e-9);
    b.checkNear("SWR of 0.5", Formats::applyPoint(Cplx(0.5, 0), TraceFormat::Swr, 50), 3.0, 1e-9);
    b.checkNear("impedance of gamma 1/3", Formats::applyPoint(Cplx(1.0 / 3.0, 0), TraceFormat::Impedance, 50), 100.0, 1e-6);

    // An ideal 2 ns delay line: flat group delay, steadily falling phase.
    const int n = 201;
    std::vector<double> f(n);
    std::vector<Cplx> s(n);
    for (int i = 0; i < n; ++i) {
        f[size_t(i)] = 1e9 + 1e7 * i;
        s[size_t(i)] = std::exp(Cplx(0, -2.0 * M_PI * f[size_t(i)] * 2e-9));
    }
    const std::vector<double> gd = Formats::apply(s, f, TraceFormat::GroupDelay, 50, 2);
    b.checkNear("group delay mid", gd[100], 2e-9, 1e-12);
    b.checkNear("group delay at the edge", gd[0], 2e-9, 1e-12);
    const std::vector<double> ph = Formats::unwrappedPhaseDeg(s);
    b.checkNear("unwrapped phase span", ph.back() - ph.front(), -360.0 * 2e-9 * 2e9, 1e-6);

    const std::vector<double> flat(50, 3.0);
    b.checkNear("smoothing keeps a flat trace flat", Formats::smooth(flat, 10)[0], 3.0, 1e-12);
    std::vector<double> spike(51, 0.0);
    spike[25] = 10.0;
    b.check("smoothing spreads a spike", Formats::smooth(spike, 10)[25] < 5.0);

    b.checkEq("engineering GHz", Formats::engineering(2.4e9, "Hz"), QStringLiteral("2.4 GHz"));
    b.checkEq("engineering ns", Formats::engineering(3.2e-9, "s"), QStringLiteral("3.2 ns"));
}

void benchFft(Bench& b)
{
    b.group("fft");
    std::vector<Cplx> a(64);
    for (size_t i = 0; i < a.size(); ++i)
        a[i] = std::exp(Cplx(0, 2.0 * M_PI * 5.0 * double(i) / 64.0));
    std::vector<Cplx> orig = a;
    Fft::transform(a, false);
    b.checkNear("tone lands in bin 5", std::abs(a[5]), 64.0, 1e-9);
    b.checkNear("other bins empty", std::abs(a[6]), 0.0, 1e-9);
    Fft::transform(a, true);
    b.checkNear("inverse round trip", std::abs(a[17] - orig[17]), 0.0, 1e-12);
    b.checkEq("nextPow2", int(Fft::nextPow2(1025)), 2048);
}

void benchTimeDomain(Bench& b)
{
    b.group("time domain");

    // Full reflection 3 ns away (round trip), 10 MHz - 6 GHz.
    const int n = 600;
    std::vector<double> f(n);
    std::vector<Cplx> open(n), two(n);
    for (int i = 0; i < n; ++i) {
        f[size_t(i)] = 10e6 * (i + 1);
        const Cplx near = 0.5 * std::exp(Cplx(0, -2.0 * M_PI * f[size_t(i)] * 3e-9));
        const Cplx far = 0.3 * std::exp(Cplx(0, -2.0 * M_PI * f[size_t(i)] * 9e-9));
        open[size_t(i)] = std::exp(Cplx(0, -2.0 * M_PI * f[size_t(i)] * 3e-9));
        two[size_t(i)] = near + far;
    }

    b.checkNear("alias-free span", TimeDomain::aliasFreeSpan(f), 100e-9, 1e-12);

    auto peakOf = [](const TimeDomain::Result& r, double& t) {
        double best = -1;
        for (size_t i = 0; i < r.h.size(); ++i)
            if (std::abs(r.h[i]) > best) { best = std::abs(r.h[i]); t = r.t[i]; }
        return best;
    };

    double tPeak = 0;
    TimeDomain::Result r = TimeDomain::transform(f, open, TdMode::BandPass, WindowType::Hamming, -5e-9, 20e-9, 2001);
    b.check("band-pass transform runs", r.error.isEmpty(), r.error);
    double peak = peakOf(r, tPeak);
    b.checkNear("band-pass peak time", tPeak, 3e-9, 0.05e-9);
    b.checkNear("band-pass peak height", peak, 1.0, 0.02);

    r = TimeDomain::transform(f, open, TdMode::LowPassImpulse, WindowType::Hamming, -5e-9, 20e-9, 2001);
    peak = peakOf(r, tPeak);
    b.checkNear("low-pass impulse peak time", tPeak, 3e-9, 0.05e-9);
    b.checkNear("low-pass impulse peak height", peak, 1.0, 0.02);

    r = TimeDomain::transform(f, open, TdMode::LowPassStep, WindowType::Hamming, -5e-9, 20e-9, 2001);
    b.checkNear("step is zero before the reflection", r.h[400].real(), 0.0, 0.02);    // t = 0
    b.checkNear("step settles at +1 after an open", r.h[1600].real(), 1.0, 0.02);     // t = 15 ns

    std::vector<Cplx> shorted(open);
    for (auto& v : shorted)
        v = -v;
    r = TimeDomain::transform(f, shorted, TdMode::LowPassStep, WindowType::Hamming, -5e-9, 20e-9, 2001);
    b.checkNear("step settles at -1 after a short", r.h[1600].real(), -1.0, 0.02);

    std::vector<double> logf(f);
    for (int i = 0; i < n; ++i)
        logf[size_t(i)] = 1e7 * std::pow(600.0, i / double(n - 1));
    r = TimeDomain::transform(logf, open, TdMode::BandPass, WindowType::Hamming, 0, 10e-9, 201);
    b.check("log sweep refused", !r.error.isEmpty());

    // Gate around the near reflection: the far one should go, the near stay.
    QString err;
    const std::vector<Cplx> gated = TimeDomain::gate(f, two, 1e-9, 5e-9, false, err);
    b.check("gate runs", !gated.empty(), err);
    if (!gated.empty()) {
        double worst = 0;
        for (int i = 60; i < n - 60; ++i)       // away from the band edges
            worst = std::max(worst, std::abs(std::abs(gated[size_t(i)]) - 0.5));
        b.check("gate leaves only the near reflection (mid-band)", worst < 0.02, QString::number(worst));

        const std::vector<Cplx> notched = TimeDomain::gate(f, two, 1e-9, 5e-9, true, err);
        worst = 0;
        for (int i = 60; i < n - 60; ++i)
            worst = std::max(worst, std::abs(std::abs(notched[size_t(i)]) - 0.3));
        b.check("notch gate leaves only the far reflection (mid-band)", worst < 0.02, QString::number(worst));
    }
}

void benchMarkers(Bench& b)
{
    b.group("marker search");

    const SweepData d = dutSweep(2.0e9, 2.8e9, 801);
    const std::vector<double> y = Formats::apply(d.param(SParam::S21), d.x, TraceFormat::LogMag, 50);

    const int peak = MarkerSearch::maxIndex(y);
    b.checkNear("pass-band peak is near 2.4 GHz", d.x[size_t(peak)], 2.4e9, 60e6);
    b.checkNear("pass-band loss", y[size_t(peak)], 20.0 * std::log10(0.93), 0.01);

    const MarkerSearch::Bandwidth bw = MarkerSearch::bandwidth(d.x, y, peak, -3.0, false);
    b.check("bandwidth found", bw.valid);
    b.checkNear("3 dB bandwidth of the 200 MHz filter", bw.bandwidth, 200e6, 2e6);
    b.checkNear("bandwidth centre", bw.center, 2.4e9, 5e6);
    b.checkNear("Q", bw.q, 12.0, 0.3);

    const MarkerSearch::Bandwidth none = MarkerSearch::bandwidth(d.x, y, peak, -300.0, false);
    b.check("unreachable level gives no bandwidth", !none.valid);

    // Notch: the same filter's return loss dips in the pass-band.
    std::vector<double> vee(101);
    std::vector<double> vx(101);
    for (int i = 0; i <= 100; ++i) {
        vx[size_t(i)] = i;
        vee[size_t(i)] = std::abs(i - 50) * 0.5 - 20.0;
    }
    const MarkerSearch::Bandwidth nb = MarkerSearch::bandwidth(vx, vee, MarkerSearch::minIndex(vee), 3.0, true);
    b.check("notch found", nb.valid);
    b.checkNear("notch width", nb.bandwidth, 12.0, 1e-9);

    const std::vector<double> two = {0, 1, 5, 1, 0, 2, 9, 2, 0, 0.2, 0.4, 0.2, 0};
    const std::vector<int> peaks = MarkerSearch::findPeaks(two, 1.0);
    b.checkEq("two prominent peaks", int(peaks.size()), 2);
    b.checkEq("next peak right", MarkerSearch::nextPeak(two, 2, +1, 1.0), 6);
    b.checkEq("next peak left", MarkerSearch::nextPeak(two, 6, -1, 1.0), 2);
    b.checkEq("no peak further right", MarkerSearch::nextPeak(two, 6, +1, 1.0), -1);
    b.checkEq("small bump counts at low prominence", int(MarkerSearch::findPeaks(two, 0.1).size()), 3);

    const std::vector<double> cx = MarkerSearch::crossings(vx, vee, -15.0);
    b.checkEq("two crossings", int(cx.size()), 2);
    if (cx.size() == 2) {
        b.checkNear("left crossing", cx[0], 40.0, 1e-9);
        b.checkNear("right crossing", cx[1], 60.0, 1e-9);
    }
    b.checkNear("interpolate", MarkerSearch::interpolate(vx, vee, 40.5), -15.25, 1e-9);
    b.checkEq("nearest index", MarkerSearch::nearestIndex(vx, 40.4), 40);
}

void benchPipeline(Bench& b)
{
    b.group("trace pipeline");

    const SweepData d = dutSweep(10e6, 6e9, 600);
    Workspace ws = WorkspaceIo::defaultWorkspace();
    TraceConfig t = ws.traces[1];      // S21 log mag

    TraceResult r = TracePipeline::compute(t, ws, &d);
    b.check("basic trace valid", r.valid && r.y.size() == 600, r.note);

    SweepData partial = d;
    partial.mask = 0x01;
    r = TracePipeline::compute(t, ws, &partial);
    b.check("unmeasured parameter is reported", !r.valid && r.note.contains("S21"), r.note);

    // Port extension: removing the DUT's own 1.2 ns through delay flattens
    // the S21 phase in the pass-band.
    t.format = TraceFormat::GroupDelay;
    r = TracePipeline::compute(t, ws, &d);
    const int mid = MarkerSearch::nearestIndex(r.x, 2.4e9);
    const double before = r.y[size_t(mid)];
    ws.ext[0].enabled = true;
    ws.ext[0].delay = 0.6e-9;
    ws.ext[1].enabled = true;
    ws.ext[1].delay = 0.6e-9;
    r = TracePipeline::compute(t, ws, &d);
    b.checkNear("port extension removes 1.2 ns from S21", before - r.y[size_t(mid)], 1.2e-9, 0.02e-9);
    ws.ext[0].enabled = ws.ext[1].enabled = false;

    // Memory and math: data / memory of the same sweep is 0 dB everywhere.
    t.format = TraceFormat::LogMag;
    b.check("memory capture", TracePipeline::memorySource(t, ws, &d, t.memoryX, t.memory));
    t.math = MathOp::DivMem;
    r = TracePipeline::compute(t, ws, &d);
    b.checkNear("data/memory is 0 dB", r.y[300], 0.0, 1e-9);
    b.check("memory trace is produced", r.memY.size() == r.y.size());

    const SweepData other = dutSweep(10e6, 3e9, 600);
    r = TracePipeline::compute(t, ws, &other);
    b.check("stale memory is flagged, not applied", r.note.contains("Memory") && r.memY.empty(), r.note);
    t.math = MathOp::Off;
    t.memory.clear();
    t.memoryX.clear();

    // Time domain on S11 finds the simulated cable fault at 0.45 m.
    TraceConfig s11 = ws.traces[0];
    s11.format = TraceFormat::LinMag;
    s11.td.enabled = true;
    s11.td.mode = TdMode::BandPass;
    s11.td.start = 0;
    s11.td.stop = 6e-9;
    s11.td.distanceAxis = true;
    s11.td.velocityFactor = 0.7;
    r = TracePipeline::compute(s11, ws, &d);
    b.check("time-domain trace valid", r.valid && r.xKind == XKind::Distance, r.note);
    if (r.valid) {
        const int p = MarkerSearch::maxIndex(r.y);
        b.checkNear("fault distance", r.x[size_t(p)], 0.45, 0.01);
        b.checkNear("fault reflection", r.y[size_t(p)], 0.08, 0.01);
    }

    // Limit test.
    TraceConfig lim = ws.traces[1];
    lim.limitTest = true;
    lim.limits.append(LimitSegment{2.35e9, -3.0, 2.45e9, -3.0, false});   // must stay above -3 dB
    r = TracePipeline::compute(lim, ws, &d);
    b.check("limit passes in the pass-band", r.limitChecked && r.limitPass);
    lim.limits.append(LimitSegment{1.0e9, -60.0, 2.0e9, -60.0, true});    // must stay below -60 dB
    r = TracePipeline::compute(lim, ws, &d);
    b.check("limit fails where the skirt is above -60 dB", r.limitChecked && !r.limitPass);

    // Markers and delta.
    TraceConfig mk = ws.traces[1];
    mk.markers.append(Marker{1, 2.4e9, MarkerTrack::None, 0});
    mk.markers.append(Marker{2, 2.6e9, MarkerTrack::None, 0});
    r = TracePipeline::compute(mk, ws, &d);
    b.checkEq("two marker readouts", int(r.markers.size()), 2);
    mk.deltaRef = 1;
    r = TracePipeline::compute(mk, ws, &d);
    b.check("delta marker is marked relative", r.markers.size() == 2 && r.markers[1].relative && !r.markers[0].relative);
    b.check("delta text", r.markers.size() == 2 && r.markers[1].text.contains(QChar(0x394)), r.markers.value(1).text);
}

void benchModel(Bench& b)
{
    b.group("trace model");

    TraceModel model;
    model.setWorkspace(WorkspaceIo::defaultWorkspace());
    model.setLiveSweep(dutSweep(2.0e9, 2.8e9, 801));
    b.checkEq("default traces", model.traceCount(), 2);
    b.checkEq("needed mask is S11+S21", int(model.neededMask()), 0x03);

    const int number = model.addMarker(1, 2.1e9);
    b.checkEq("first marker is M1", number, 1);
    b.check("search max", model.searchMarker(1, 1, TraceModel::Search::Max));
    b.checkNear("marker at the peak", model.workspace().traces[1].markers[0].x, 2.4e9, 60e6);
    b.check("target search right (-3 dB)", model.searchMarker(1, 1, TraceModel::Search::TargetRight,
                                                              20.0 * std::log10(0.93) - 3.0));
    b.checkNear("upper band edge", model.workspace().traces[1].markers[0].x, 2.5e9, 3e6);

    // A tracking marker follows new data.
    model.edit().traces[1].markers[0].track = MarkerTrack::Max;
    model.setLiveSweep(dutSweep(2.3e9, 2.5e9, 201));
    b.checkNear("tracking marker follows the sweep", model.workspace().traces[1].markers[0].x, 2.4e9, 60e6);

    model.edit().coupledMarkers = true;
    model.addMarker(0, 2.35e9);
    model.moveMarker(1, 1, 2.45e9);
    b.checkNear("coupled marker follows", model.workspace().traces[0].markers[0].x, 2.45e9, 1);

    model.autoScale(1);
    const TraceConfig& t = model.workspace().traces[1];
    const TraceResult& r = model.result(1);
    const double top = t.refValue + (10 - t.refPos) * t.perDiv;
    const double bottom = t.refValue - t.refPos * t.perDiv;
    const auto [lo, hi] = std::minmax_element(r.y.begin(), r.y.end());
    b.check("autoscale contains the trace", *lo >= bottom && *hi <= top,
            QString("%1..%2 in %3..%4").arg(*lo).arg(*hi).arg(bottom).arg(top));

    const int added = model.addTrace(SParam::S22, TraceFormat::Smith, 0);
    b.checkEq("added trace becomes active", model.activeIndex(), added);
    b.checkEq("needed mask grows", int(model.neededMask()), 0x0B);
    model.removeTrace(added);
    b.checkEq("removed", model.traceCount(), 2);

    // Closing plots takes their traces with them.
    model.setWorkspace(WorkspaceIo::fourPlotPreset());
    b.checkEq("preset has four plots", model.workspace().plotCount, 4);
    b.checkEq("preset has five traces", model.traceCount(), 5);
    b.check("preset plot 1 is S11 and S21 log mag",
            model.workspace().traces[0].param == SParam::S11 && model.workspace().traces[1].param == SParam::S21
                && model.workspace().traces[1].plot == 0 && model.workspace().traces[1].format == TraceFormat::LogMag);
    b.check("preset plot 2 is S11 Smith", model.workspace().traces[2].plot == 1
                && model.workspace().traces[2].format == TraceFormat::Smith);
    b.check("preset plot 3 is S11 SWR", model.workspace().traces[3].plot == 2
                && model.workspace().traces[3].format == TraceFormat::Swr
                && model.workspace().traces[3].param == SParam::S11);
    b.check("preset plot 4 is S22 log mag", model.workspace().traces[4].plot == 3
                && model.workspace().traces[4].param == SParam::S22);
    model.removePlot(1);
    b.checkEq("closing a plot leaves three", model.workspace().plotCount, 3);
    b.checkEq("its trace is removed, not moved", model.traceCount(), 4);
    b.check("later plots move down", model.workspace().traces[2].format == TraceFormat::Swr
                && model.workspace().traces[2].plot == 1 && model.workspace().traces[3].plot == 2);
    model.setPlotCount(1);
    b.checkEq("reducing the plot count removes the other plots' traces", model.traceCount(), 2);
    b.check("the survivors are the first plot's", model.workspace().traces[0].plot == 0
                && model.workspace().traces[1].plot == 0);
    model.setWorkspace(WorkspaceIo::defaultWorkspace());
    model.setLiveSweep(dutSweep(2.0e9, 2.8e9, 801));

    model.dataToMemory(0);
    b.check("data to memory stores and shows", !model.workspace().traces[0].memory.empty()
                                                   && model.workspace().traces[0].show == ShowWhat::DataAndMemory);
}

void benchFiles(Bench& b)
{
    b.group("files");
    QTemporaryDir dir;
    if (!b.check("temp dir", dir.isValid()))
        return;

    const SweepData d = dutSweep(1e9, 3e9, 101);
    QString err;
    const QString s2p = dir.filePath("t.s2p");
    b.check("write s2p", Touchstone::writeS2p(s2p, d, 50, err), err);
    SweepData back;
    double z0 = 0;
    b.check("read s2p", Touchstone::read(s2p, back, z0, err), err);
    b.checkEq("s2p points", back.points(), 101);
    b.checkNear("s2p z0", z0, 50, 1e-9);
    if (back.points() == 101) {
        b.checkNear("s2p frequency", back.x[50], d.x[50], 1);
        b.checkNear("s2p S21", std::abs(back.param(SParam::S21)[50] - d.param(SParam::S21)[50]), 0, 1e-8);
        b.checkNear("s2p S22", std::abs(back.param(SParam::S22)[50] - d.param(SParam::S22)[50]), 0, 1e-8);
    }

    const QString s1p = dir.filePath("t.s1p");
    b.check("write s1p", Touchstone::writeS1p(s1p, d, SParam::S11, 50, err), err);
    b.check("read s1p", Touchstone::read(s1p, back, z0, err) && back.mask == 0x01, err);

    // A file in MHz / dB-angle, as other tools write them.
    const QString db = dir.filePath("db.s1p");
    {
        QFile f(db);
        if (f.open(QIODevice::WriteOnly))
            f.write("! comment\n# MHz S DB R 75\n100 -20 90 ! trailing comment\n200 0 180\n");
    }
    b.check("read MHz/DB file", Touchstone::read(db, back, z0, err), err);
    if (back.points() == 2) {
        b.checkNear("MHz scaling", back.x[1], 200e6, 1);
        b.checkNear("dB/angle to complex", back.s[0][0].imag(), 0.1, 1e-9);
        b.checkNear("reference impedance", z0, 75, 1e-9);
    }

    SweepData partial = d;
    partial.mask = 0x03;
    b.check("s2p refuses a partial sweep", !Touchstone::writeS2p(dir.filePath("p.s2p"), partial, 50, err));
    b.check("csv", Touchstone::writeCsv(dir.filePath("t.csv"), d, err), err);

    Workspace ws = WorkspaceIo::defaultWorkspace();
    ws.plotCount = 3;
    ws.traces[0].markers.append(Marker{2, 1.5e9, MarkerTrack::Max, -6});
    ws.traces[0].limits.append(LimitSegment{1e9, -10, 2e9, -12, true});
    ws.traces[0].memoryX = d.x;
    ws.traces[0].memory = d.param(SParam::S11);
    ws.traces[1].td.enabled = true;
    ws.traces[1].format = TraceFormat::GroupDelay;
    ws.ext[1].enabled = true;
    ws.ext[1].delay = 1e-10;
    ws.files.append(d);
    const QString wsPath = dir.filePath("w.vnaws");
    b.check("save workspace", WorkspaceIo::save(wsPath, ws, err), err);
    Workspace w2;
    b.check("load workspace", WorkspaceIo::load(wsPath, w2, err), err);
    b.checkEq("workspace plots", w2.plotCount, 3);
    b.checkEq("workspace traces", int(w2.traces.size()), 2);
    if (w2.traces.size() == 2) {
        b.check("workspace marker", w2.traces[0].markers.size() == 1 && w2.traces[0].markers[0].number == 2
                                        && w2.traces[0].markers[0].track == MarkerTrack::Max);
        b.check("workspace limit", w2.traces[0].limits.size() == 1 && w2.traces[0].limits[0].y2 == -12);
        b.check("workspace memory", w2.traces[0].memory.size() == 101);
        b.check("workspace format", w2.traces[1].format == TraceFormat::GroupDelay && w2.traces[1].td.enabled);
    }
    b.check("workspace port extension", w2.ext[1].enabled && w2.ext[1].delay == 1e-10);
    b.check("workspace files", w2.files.size() == 1 && w2.files[0].points() == 101);
    b.check("non-workspace JSON refused", !WorkspaceIo::fromJson(QJsonObject{{"format", "other"}}, w2, err));
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QString jsonOut;
    QString device;
    bool verbose = false;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--verbose") {
            verbose = true;
        } else if (args[i] == "--out" && i + 1 < args.size()) {
            jsonOut = args[++i];
        } else if (args[i] == "--device" && i + 1 < args.size()) {
            device = args[++i];
        } else {
            QTextStream(stderr) << "usage: VnaBench [--verbose] [--out results.json] [--device host:port]\n";
            return 2;
        }
    }

    Bench b("VnaBench");
    b.setVerbose(verbose);

    benchParse(b);
    benchFrame(b);
    benchSim(b);
    benchFormats(b);
    benchFft(b);
    benchTimeDomain(b);
    benchMarkers(b);
    benchPipeline(b);
    benchModel(b);
    benchFiles(b);
    benchDevice(b);

    if (!device.isEmpty()) {
        const qsizetype colon = device.lastIndexOf(':');
        benchHardware(b, colon > 0 ? device.left(colon) : device,
                      colon > 0 ? device.mid(colon + 1).toUShort() : kDefaultScpiPort);
    }

    return b.finish(jsonOut);
}
