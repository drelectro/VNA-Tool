#include "VnaDevice.h"

#include "Scpi/ScpiParse.h"
#include "Scpi/ScpiSession.h"
#include "Scpi/ScpiSimTransport.h"
#include "Scpi/ScpiTcpTransport.h"
#include "VnaFrame.h"

#include <QDateTime>
#include <QThread>

#include <cmath>

namespace {

QString red(const QString& t)   { return QStringLiteral("[color=#e88080]%1[/color]").arg(t); }
QString amber(const QString& t) { return QStringLiteral("[color=#e8c060]%1[/color]").arg(t); }

QByteArray fmt(double v) { return QByteArray::number(v, 'g', 12); }

// Every query whose reply applyReply() understands. The tag IS the query text.
const QList<QByteArray>& settingQueries()
{
    static const QList<QByteArray> k = {
        "VNA:SWEEP?", "VNA:SWEEPTYPE?", "VNA:FREQ:START?", "VNA:FREQ:STOP?",
        "VNA:ACQ:POINTS?", "VNA:ACQ:IFBW?", "VNA:STIM:LVL?", "VNA:POW:START?",
        "VNA:POW:STOP?", "VNA:STIM:FREQ?", "VNA:ACQ:DWELL?", "VNA:ACQ:RUN?",
        "DEV:REF:IN?",
    };
    return k;
}
const QList<QByteArray>& healthQueries()
{
    static const QList<QByteArray> k = {
        "DEV:STA:UNLO?", "DEV:STA:ADCOVER?", "DEV:STA:UNLEV?", "DEV:INF:TEMP?",
        "VNA:ACQ:SWE:TIME?",
    };
    return k;
}

ScpiRequest tagged(const QByteArray& query)
{
    return scpiQuery(query, QString::fromLatin1(query));
}

} // namespace

VnaDevice::VnaDevice(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<SweepData>();

    _pollTimer.setSingleShot(true);
    connect(&_pollTimer, &QTimer::timeout, this, &VnaDevice::fetchGroup);

    // While holding there is no sweep to piggyback on, so poll on a timer.
    _idleTimer.setInterval(700);
    connect(&_idleTimer, &QTimer::timeout, this, [this] {
        if (_state != State::Ready || _acqPhase != AcqPhase::Idle)
            return;
        ScpiTransaction txn;
        txn.priority = ScpiPriority::Background;
        txn.coalesceKey = QStringLiteral("idle-poll");
        txn.add(nextPollQuery());
        submit(txn, Purpose::Settings);
    });
}

VnaDevice::~VnaDevice()
{
    disconnectFrom();
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void VnaDevice::connectTo(const ScpiEndpoint& endpoint)
{
    disconnectFrom();
    _endpoint = endpoint;

    ScpiTransport* transport = nullptr;
    if (endpoint.simulated) {
        auto* sim = new ScpiSimTransport;
        sim->setLatencyMs(_simLatencyMs);
        transport = sim;
    } else {
        transport = new ScpiTcpTransport;
    }

    _session = ScpiSession::createOnOwnThread(endpoint, transport, _thread);
    connect(_session, &ScpiSession::stateChanged, this, &VnaDevice::onSessionState, Qt::QueuedConnection);
    connect(_session, &ScpiSession::finished, this, &VnaDevice::onFinished, Qt::QueuedConnection);
    connect(_session, &ScpiSession::traffic, this, &VnaDevice::traffic, Qt::QueuedConnection);

    setState(State::Connecting, endpoint.displayName());
    QMetaObject::invokeMethod(_session, "open", Qt::QueuedConnection);
}

void VnaDevice::disconnectFrom()
{
    _pollTimer.stop();
    _idleTimer.stop();
    _acqPhase = AcqPhase::Idle;
    _pending.clear();

    if (_session) {
        disconnect(_session, nullptr, this, nullptr);
        QMetaObject::invokeMethod(_session, "close", Qt::BlockingQueuedConnection);
        _thread->quit();
        _thread->wait(3000);
        delete _thread;         // the session deletes itself on thread finish
        _thread = nullptr;
        _session = nullptr;
    }
    if (_state != State::Disconnected)
        setState(State::Disconnected, QStringLiteral("disconnected"));
}

void VnaDevice::setState(State s, const QString& detail)
{
    _state = s;
    emit stateChanged(s, detail);
}

void VnaDevice::onSessionState(ScpiSessionState state, const QString& detail)
{
    if (state == ScpiSessionState::Ready && _state == State::Connecting) {
        setState(State::Identifying, detail);

        ScpiTransaction txn;
        txn.priority = ScpiPriority::Critical;
        for (const char* q : {"*IDN?", "CLIENT:INFo?", "DEV:INF:FWREV?", "DEV:INF:HWREV?",
                              "DEV:INF:LIM:MINF?", "DEV:INF:LIM:MAXF?", "DEV:INF:LIM:MINIFBW?",
                              "DEV:INF:LIM:MAXIFBW?", "DEV:INF:LIM:MAXP?", "DEV:INF:LIM:MINPOW?",
                              "DEV:INF:LIM:MAXPOW?"})
            txn.add(tagged(q));
        appendAllSettingQueries(txn);
        for (const QByteArray& q : healthQueries())
            txn.add(tagged(q));
        submit(txn, Purpose::Identify);
    } else if (state == ScpiSessionState::Disconnected && _state != State::Disconnected) {
        _pollTimer.stop();
        _idleTimer.stop();
        _acqPhase = AcqPhase::Idle;
        _pending.clear();
        emit message(red(QStringLiteral("Connection lost: %1").arg(detail)));
        setState(State::Disconnected, detail);
    }
}

quint64 VnaDevice::submit(ScpiTransaction txn, Purpose purpose, const QString& note)
{
    if (!_session)
        return 0;
    const quint64 id = _session->enqueue(txn);
    _pending.insert(id, Pending{purpose, note});
    return id;
}

void VnaDevice::appendAllSettingQueries(ScpiTransaction& txn) const
{
    for (const QByteArray& q : settingQueries())
        txn.add(tagged(q));
}

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------

void VnaDevice::onFinished(quint64 id, const ScpiResult& result)
{
    const auto it = _pending.constFind(id);
    if (it == _pending.constEnd())
        return;
    const Pending p = it.value();
    _pending.erase(it);

    if (p.purpose == Purpose::User) {
        emit userReply(id, result);
        return;
    }
    if (result.cancelled)
        return;     // superseded by a newer value for the same setting

    switch (p.purpose) {
    case Purpose::Identify: {
        if (const ScpiReply* idn = result.find(QStringLiteral("*IDN?")); idn && idn->ok) {
            const QList<QByteArray> f = idn->text.split(',');
            _info.manufacturer   = QString::fromLatin1(f.value(0)).trimmed();
            _info.model          = QString::fromLatin1(f.value(1)).trimmed();
            _info.serial         = QString::fromLatin1(f.value(2)).trimmed();
            _info.serviceVersion = QString::fromLatin1(f.value(3)).trimmed();
        } else {
            emit message(red(QStringLiteral("No answer to *IDN? - is this VNStudio's SCPI port?")));
            disconnectFrom();
            return;
        }
        applyReplies(result);
        emit infoChanged();
        emit stimulusChanged();
        emit healthChanged();
        setState(State::Ready, _endpoint.displayName());
        emit message(QStringLiteral("Connected to [b]%1 %2[/b], serial %3, firmware %4")
                         .arg(_info.manufacturer, _info.model, _info.serial, _info.firmware));
        _idleTimer.start();
        if (_runMode == RunMode::Continuous)
            startSweep();
        break;
    }
    case Purpose::Settings:
        applyReplies(result);
        break;
    case Purpose::Set:
        for (const ScpiReply& r : result.replies) {
            if (!r.ok && r.tag.isEmpty())
                emit message(red(QStringLiteral("%1 refused: %2").arg(p.note, r.error)));
        }
        applyReplies(result);
        // The read-back may equal the old value (a refused setting), and the
        // panel still has the operator's text in it - so always notify.
        emit stimulusChanged();
        restartAveraging();
        break;
    case Purpose::Action:
        for (const ScpiReply& r : result.replies) {
            if (!r.ok)
                emit message(red(QStringLiteral("%1 failed: %2").arg(p.note, r.error)));
            else if (r.tag == QLatin1String("action") && r.text.toUpper() == "FALSE")
                emit message(red(QStringLiteral("%1 failed: the instrument answered FALSE").arg(p.note)));
            else if (r.tag == QLatin1String("action"))
                emit message(QStringLiteral("%1: %2").arg(p.note, QString::fromLatin1(r.text)));
        }
        if (result.ok && result.find(QStringLiteral("action")) == nullptr)
            emit message(QStringLiteral("%1 done").arg(p.note));
        applyReplies(result);
        emit stimulusChanged();
        restartAveraging();
        break;
    case Purpose::AcqStart:
        handleGroupStarted(result);
        break;
    case Purpose::AcqPoll:
        handleGroupStatus(result);
        break;
    case Purpose::AcqFetch:
        handleGroupData(result);
        break;
    case Purpose::AcqAscii:
        handleAsciiData(result);
        break;
    case Purpose::User:
        break;
    }
}

void VnaDevice::applyReplies(const ScpiResult& result)
{
    const Stimulus beforeStim = _stim;
    const Health beforeHealth = _health;
    for (const ScpiReply& r : result.replies)
        if (r.ok && !r.tag.isEmpty())
            applyReply(r);

    const bool stimChanged = beforeStim.mode != _stim.mode || beforeStim.start != _stim.start
        || beforeStim.stop != _stim.stop || beforeStim.points != _stim.points
        || beforeStim.ifbw != _stim.ifbw || beforeStim.power != _stim.power
        || beforeStim.powerStart != _stim.powerStart || beforeStim.powerStop != _stim.powerStop
        || beforeStim.cwFreq != _stim.cwFreq || beforeStim.dwell != _stim.dwell
        || beforeStim.deviceRunning != _stim.deviceRunning || beforeStim.externalRef != _stim.externalRef;
    if (stimChanged)
        emit stimulusChanged();

    if (beforeHealth.unlocked != _health.unlocked || beforeHealth.adcOverload != _health.adcOverload
        || beforeHealth.unlevelled != _health.unlevelled || beforeHealth.temperatureC != _health.temperatureC
        || beforeHealth.sweepTimeS != _health.sweepTimeS) {
        if (_health.unlocked && !beforeHealth.unlocked)
            emit message(amber(QStringLiteral("PLL unlocked")));
        if (_health.adcOverload && !beforeHealth.adcOverload)
            emit message(amber(QStringLiteral("ADC overload - reduce source power or add attenuation")));
        if (_health.unlevelled && !beforeHealth.unlevelled)
            emit message(amber(QStringLiteral("Source unlevelled")));
        emit healthChanged();
    }
}

bool VnaDevice::applyReply(const ScpiReply& r)
{
    const QString& t = r.tag;
    const QByteArray text = r.text.trimmed();
    double d = 0;
    bool b = false;

    auto resolveMode = [this] {
        if (_sweepKind == QLatin1String("POWER")) _stim.mode = SweepMode::Power;
        else if (_sweepKind == QLatin1String("CW")) _stim.mode = SweepMode::CW;
        else _stim.mode = _freqSweepLog ? SweepMode::LogFreq : SweepMode::LinFreq;
    };

    if (t == QLatin1String("VNA:SWEEP?")) { _sweepKind = QString::fromLatin1(text.toUpper()); resolveMode(); }
    else if (t == QLatin1String("VNA:SWEEPTYPE?")) { _freqSweepLog = text.toUpper() == "LOG"; resolveMode(); }
    else if (t == QLatin1String("VNA:FREQ:START?")) { if (ScpiParse::toDouble(text, d)) _stim.start = d; }
    else if (t == QLatin1String("VNA:FREQ:STOP?")) { if (ScpiParse::toDouble(text, d)) _stim.stop = d; }
    else if (t == QLatin1String("VNA:ACQ:POINTS?")) { if (ScpiParse::toDouble(text, d)) _stim.points = int(d); }
    else if (t == QLatin1String("VNA:ACQ:IFBW?")) { if (ScpiParse::toDouble(text, d)) _stim.ifbw = d; }
    else if (t == QLatin1String("VNA:STIM:LVL?")) { if (ScpiParse::toDouble(text, d)) _stim.power = d; }
    else if (t == QLatin1String("VNA:POW:START?")) { if (ScpiParse::toDouble(text, d)) _stim.powerStart = d; }
    else if (t == QLatin1String("VNA:POW:STOP?")) { if (ScpiParse::toDouble(text, d)) _stim.powerStop = d; }
    else if (t == QLatin1String("VNA:STIM:FREQ?")) { if (ScpiParse::toDouble(text, d)) _stim.cwFreq = d; }
    else if (t == QLatin1String("VNA:ACQ:DWELL?")) { if (ScpiParse::toDouble(text, d)) _stim.dwell = d; }
    else if (t == QLatin1String("VNA:ACQ:RUN?")) { if (ScpiParse::toBool(text, b)) _stim.deviceRunning = b; }
    else if (t == QLatin1String("DEV:REF:IN?")) { _stim.externalRef = text.toUpper() == "EXT"; }
    else if (t == QLatin1String("DEV:STA:UNLO?")) { if (ScpiParse::toBool(text, b)) _health.unlocked = b; }
    else if (t == QLatin1String("DEV:STA:ADCOVER?")) { if (ScpiParse::toBool(text, b)) _health.adcOverload = b; }
    else if (t == QLatin1String("DEV:STA:UNLEV?")) { if (ScpiParse::toBool(text, b)) _health.unlevelled = b; }
    else if (t == QLatin1String("DEV:INF:TEMP?")) { if (ScpiParse::toDouble(text, d)) _health.temperatureC = d; }
    else if (t == QLatin1String("VNA:ACQ:SWE:TIME?")) { if (ScpiParse::toDouble(text, d)) _health.sweepTimeS = d; }
    else if (t == QLatin1String("CLIENT:INFo?")) { _info.clientVersion = QString::fromLatin1(text.split(',').value(2)); }
    else if (t == QLatin1String("DEV:INF:FWREV?")) { _info.firmware = QString::fromLatin1(text); }
    else if (t == QLatin1String("DEV:INF:HWREV?")) { _info.hardware = QString::fromLatin1(text); }
    else if (t == QLatin1String("DEV:INF:LIM:MINF?")) { if (ScpiParse::toDouble(text, d)) _info.minFreq = d; }
    else if (t == QLatin1String("DEV:INF:LIM:MAXF?")) { if (ScpiParse::toDouble(text, d)) _info.maxFreq = d; }
    else if (t == QLatin1String("DEV:INF:LIM:MINIFBW?")) { if (ScpiParse::toDouble(text, d)) _info.minIfbw = d; }
    else if (t == QLatin1String("DEV:INF:LIM:MAXIFBW?")) { if (ScpiParse::toDouble(text, d)) _info.maxIfbw = d; }
    else if (t == QLatin1String("DEV:INF:LIM:MAXP?")) { if (ScpiParse::toDouble(text, d)) _info.maxPoints = int(d); }
    else if (t == QLatin1String("DEV:INF:LIM:MINPOW?")) { if (ScpiParse::toDouble(text, d)) _info.minPower = d; }
    else if (t == QLatin1String("DEV:INF:LIM:MAXPOW?")) { if (ScpiParse::toDouble(text, d)) _info.maxPower = d; }
    else {
        return false;
    }
    return true;
}

ScpiRequest VnaDevice::nextPollQuery()
{
    // Health queries are interleaved two-to-one with settings, since a PLL
    // unlock matters sooner than a setting changed on the tablet.
    const QList<QByteArray>& h = healthQueries();
    const QList<QByteArray>& s = settingQueries();
    const int i = _pollIndex++;
    if (i % 3 != 2)
        return tagged(h[(i - i / 3) % h.size()]);
    return tagged(s[(i / 3) % s.size()]);
}

// ---------------------------------------------------------------------------
// Stimulus
// ---------------------------------------------------------------------------

void VnaDevice::sendSetting(const QByteArray& command, const QList<QByteArray>& readBack,
                            const QString& key, const QString& what)
{
    if (!isReady())
        return;
    ScpiTransaction txn;
    txn.coalesceKey = key;
    txn.add(scpiCommand(command));
    for (const QByteArray& q : readBack)
        txn.add(tagged(q));
    txn.add(tagged("VNA:ACQ:SWE:TIME?"));
    submit(txn, Purpose::Set, what);
}

void VnaDevice::setStart(double hz)
{
    // An entry outside the instrument's range becomes the nearest end of it.
    // The instrument would refuse the value outright and leave the old one,
    // which is rarely what someone typing "0" or "10G" meant.
    hz = std::clamp(hz, _info.minFreq, _info.maxFreq);
    sendSetting("VNA:FREQ:START " + fmt(hz), {"VNA:FREQ:START?"}, QStringLiteral("start"),
                QStringLiteral("Start frequency"));
}

void VnaDevice::setStop(double hz)
{
    hz = std::clamp(hz, _info.minFreq, _info.maxFreq);
    sendSetting("VNA:FREQ:STOP " + fmt(hz), {"VNA:FREQ:STOP?"}, QStringLiteral("stop"),
                QStringLiteral("Stop frequency"));
}

void VnaDevice::setStartStop(double startHz, double stopHz)
{
    startHz = std::clamp(startHz, _info.minFreq, _info.maxFreq);
    stopHz = std::clamp(stopHz, _info.minFreq, _info.maxFreq);
    if (!isReady() || !(stopHz > startHz))
        return;
    // The instrument refuses start >= stop, so the order of the two commands
    // depends on which way the range is moving.
    ScpiTransaction txn;
    txn.coalesceKey = QStringLiteral("startstop");
    const QByteArray a = "VNA:FREQ:START " + fmt(startHz);
    const QByteArray b = "VNA:FREQ:STOP " + fmt(stopHz);
    if (startHz >= _stim.stop) { txn.add(scpiCommand(b)); txn.add(scpiCommand(a)); }
    else                       { txn.add(scpiCommand(a)); txn.add(scpiCommand(b)); }
    txn.add(tagged("VNA:FREQ:START?"));
    txn.add(tagged("VNA:FREQ:STOP?"));
    txn.add(tagged("VNA:ACQ:SWE:TIME?"));
    submit(txn, Purpose::Set, QStringLiteral("Frequency range"));
}

void VnaDevice::setCenterSpan(double centerHz, double spanHz)
{
    // No centre/span commands exist; they are start/stop by another name.
    setStartStop(centerHz - spanHz / 2.0, centerHz + spanHz / 2.0);
}

void VnaDevice::setPoints(int points)
{
    sendSetting("VNA:ACQ:POINTS " + QByteArray::number(points), {"VNA:ACQ:POINTS?"},
                QStringLiteral("points"), QStringLiteral("Points"));
}

void VnaDevice::setIfbw(double hz)
{
    sendSetting("VNA:ACQ:IFBW " + fmt(hz), {"VNA:ACQ:IFBW?"}, QStringLiteral("ifbw"),
                QStringLiteral("IF bandwidth"));
}

void VnaDevice::setPower(double dbm)
{
    sendSetting("VNA:STIM:LVL " + fmt(dbm), {"VNA:STIM:LVL?"}, QStringLiteral("power"),
                QStringLiteral("Source power"));
}

void VnaDevice::setSweepMode(SweepMode mode)
{
    if (!isReady())
        return;
    ScpiTransaction txn;
    txn.coalesceKey = QStringLiteral("mode");
    switch (mode) {
    case SweepMode::LinFreq:
        txn.add(scpiCommand("VNA:SWEEP FREQUENCY"));
        txn.add(scpiCommand("VNA:SWEEPTYPE LIN"));
        break;
    case SweepMode::LogFreq:
        txn.add(scpiCommand("VNA:SWEEP FREQUENCY"));
        txn.add(scpiCommand("VNA:SWEEPTYPE LOG"));
        break;
    case SweepMode::Power:
        txn.add(scpiCommand("VNA:SWEEP POWER"));
        break;
    case SweepMode::CW:
        txn.add(scpiCommand("VNA:SWEEP CW"));
        break;
    }
    appendAllSettingQueries(txn);
    txn.add(tagged("VNA:ACQ:SWE:TIME?"));
    submit(txn, Purpose::Set, QStringLiteral("Sweep mode"));
}

void VnaDevice::setPowerRange(double startDbm, double stopDbm)
{
    if (!isReady())
        return;
    ScpiTransaction txn;
    txn.coalesceKey = QStringLiteral("powrange");
    txn.add(scpiCommand("VNA:POW:START " + fmt(startDbm)));
    txn.add(scpiCommand("VNA:POW:STOP " + fmt(stopDbm)));
    txn.add(tagged("VNA:POW:START?"));
    txn.add(tagged("VNA:POW:STOP?"));
    submit(txn, Purpose::Set, QStringLiteral("Power sweep range"));
}

void VnaDevice::setCwFrequency(double hz)
{
    sendSetting("VNA:STIM:FREQ " + fmt(hz), {"VNA:STIM:FREQ?"}, QStringLiteral("cwfreq"),
                QStringLiteral("Power-sweep frequency"));
}

void VnaDevice::setDwell(double seconds)
{
    sendSetting("VNA:ACQ:DWELL " + fmt(seconds), {"VNA:ACQ:DWELL?"}, QStringLiteral("dwell"),
                QStringLiteral("Dwell time"));
}

void VnaDevice::setExternalReference(bool external)
{
    sendSetting(external ? "DEV:REF:IN EXT" : "DEV:REF:IN INT", {"DEV:REF:IN?"},
                QStringLiteral("ref"), QStringLiteral("Reference source"));
}

void VnaDevice::refreshStimulus()
{
    if (!isReady())
        return;
    ScpiTransaction txn;
    txn.coalesceKey = QStringLiteral("refresh");
    appendAllSettingQueries(txn);
    for (const QByteArray& q : healthQueries())
        txn.add(tagged(q));
    submit(txn, Purpose::Settings);
}

// ---------------------------------------------------------------------------
// Instrument-side actions
// ---------------------------------------------------------------------------

void VnaDevice::preset()
{
    if (!isReady())
        return;
    ScpiTransaction txn;
    txn.add(scpiCommand("*RST"));
    appendAllSettingQueries(txn);
    submit(txn, Purpose::Action, QStringLiteral("Preset"));
}

void VnaDevice::setDeviceRunning(bool run)
{
    if (!isReady())
        return;
    ScpiTransaction txn;
    txn.add(scpiCommand(run ? "VNA:ACQ:RUN" : "VNA:ACQ:STOP"));
    txn.add(tagged("VNA:ACQ:RUN?"));
    submit(txn, Purpose::Action, run ? QStringLiteral("Instrument acquisition start")
                                     : QStringLiteral("Instrument acquisition stop"));
}

void VnaDevice::saveState(const QString& devicePath)
{
    if (!isReady())
        return;
    ScpiTransaction txn;
    txn.add(scpiCommand("CLIENT:SETUP:SAVE \"" + devicePath.toUtf8() + "\""));
    submit(txn, Purpose::Action, QStringLiteral("Save state to %1").arg(devicePath));
}

void VnaDevice::loadState(const QString& devicePath)
{
    if (!isReady())
        return;
    ScpiTransaction txn;
    txn.add(scpiQuery("CLIENT:SETUP:LOAD? \"" + devicePath.toUtf8() + "\"", QStringLiteral("action"), 15000));
    appendAllSettingQueries(txn);
    submit(txn, Purpose::Action, QStringLiteral("Load state %1").arg(devicePath));
}

void VnaDevice::loadCalibration(const QString& devicePath)
{
    if (!isReady())
        return;
    ScpiTransaction txn;
    txn.add(scpiQuery("VNA:CAL:LOAD? \"" + devicePath.toUtf8() + "\"", QStringLiteral("action"), 15000));
    appendAllSettingQueries(txn);
    submit(txn, Purpose::Action, QStringLiteral("Load calibration %1").arg(devicePath));
}

quint64 VnaDevice::sendRaw(const QByteArray& line)
{
    if (!_session)
        return 0;
    ScpiTransaction txn;
    txn.fromUser = true;
    ScpiRequest req(line, ScpiParse::isQuery(line) ? ScpiKind::Query : ScpiKind::Command);
    req.timeoutMs = 5000;
    txn.add(req);
    return submit(txn, Purpose::User);
}

// ---------------------------------------------------------------------------
// Acquisition
// ---------------------------------------------------------------------------

void VnaDevice::setRunMode(RunMode mode)
{
    if (_runMode == mode)
        return;
    _runMode = mode;
    emit runModeChanged();
    if (mode == RunMode::Continuous && isReady() && _acqPhase == AcqPhase::Idle)
        startSweep();
}

void VnaDevice::single()
{
    if (_runMode != RunMode::Hold) {
        _runMode = RunMode::Hold;
        emit runModeChanged();
    }
    if (!isReady())
        return;
    restartAveraging();
    if (_acqPhase == AcqPhase::Idle)
        startSweep();
    else
        _singleRequested = true;
}

void VnaDevice::setAveraging(int count)
{
    _avgTarget = std::max(1, count);
    restartAveraging();
}

void VnaDevice::restartAveraging()
{
    _avgCount = 0;
    _avgSweep = SweepData{};
}

void VnaDevice::startSweep()
{
    if (!isReady() || _acqPhase != AcqPhase::Idle)
        return;
    _singleRequested = false;

    // Sweep groups fail above the point count the instrument reports as its
    // maximum (2048), although it accepts settings up to 10001.
    if (_stim.points > _info.maxPoints) {
        startAsciiSweep();
        return;
    }
    _asciiWarned = false;
    _acqPhase = AcqPhase::Starting;

    ScpiTransaction txn;
    txn.priority = ScpiPriority::Background;
    if (!_groupCountSet)
        txn.add(scpiCommand("VNA:ACQ:GRO:COUN 1", QStringLiteral("groupcount")));
    txn.add(scpiCommand("VNA:ACQ:GRO:INIT", QStringLiteral("init")));
    // Every command costs the instrument about 60 ms, so the status and
    // settings poll rides along only on every fourth sweep.
    if (_sweepsStarted++ % 4 == 0)
        txn.add(nextPollQuery());
    submit(txn, Purpose::AcqStart);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double expected = std::max(_health.sweepTimeS, 0.05);
    _sweepStartMs = now;
    // While the tablet is sweeping, a group waits (ARMED) for the sweep in
    // progress to finish and then captures the next one, so up to two sweep
    // times is normal. Generous beyond that: 2048 points at 15 Hz IF
    // bandwidth takes minutes.
    _sweepDeadlineMs = now + qint64(expected * 4000.0) + 15000;
    _nextStatusCheckMs = now + qint64(expected * 2500.0) + 1000;
}

void VnaDevice::startAsciiSweep()
{
    if (!_asciiWarned) {
        _asciiWarned = true;
        emit message(amber(QStringLiteral(
            "More than %1 points: the instrument's fast transfer is not available, so each "
            "S-parameter is read separately as text. Updates are slower and the parameters "
            "come from different sweeps.").arg(_info.maxPoints)));
    }
    _acqPhase = AcqPhase::Fetching;

    ScpiTransaction txn;
    txn.priority = ScpiPriority::Background;
    for (int k = 0; k < kSParamCount; ++k) {
        if (!((_neededMask >> k) & 1u))
            continue;
        const QString name = sParamName(static_cast<SParam>(k));
        txn.add(scpiQuery("VNA:TRAC:DATA? " + name.toLatin1(), name, 30000));
    }
    if (_sweepsStarted++ % 4 == 0)
        txn.add(nextPollQuery());
    submit(txn, Purpose::AcqAscii);
}

void VnaDevice::handleAsciiData(const ScpiResult& result)
{
    applyReplies(result);

    SweepData sweep;
    for (int k = 0; k < kSParamCount; ++k) {
        const ScpiReply* r = result.find(sParamName(static_cast<SParam>(k)));
        if (!r || !r->ok)
            continue;
        std::vector<double> x;
        std::vector<Cplx> values;
        if (!ScpiParse::parseTriplets(r->text, x, values))
            continue;
        // A setting changed between two of the queries: keep what agrees
        // with the first one and drop the rest for this round.
        if (!sweep.x.empty() && (x.size() != sweep.x.size() || x.back() != sweep.x.back()))
            continue;
        if (sweep.x.empty())
            sweep.x = std::move(x);
        sweep.s[size_t(k)] = std::move(values);
        sweep.mask |= 1u << k;
    }

    if (sweep.mask == 0) {
        acquisitionFailed(QStringLiteral("no trace data: %1").arg(result.firstError()));
        return;
    }

    switch (_stim.mode) {
    case SweepMode::LinFreq: sweep.xKind = XKind::Frequency; break;
    case SweepMode::LogFreq: sweep.xKind = XKind::Frequency; sweep.logSpacing = true; break;
    case SweepMode::Power:   sweep.xKind = XKind::Power; break;
    case SweepMode::CW:      sweep.xKind = XKind::Time; break;
    }
    sweep.sequence = _sweepsStarted;
    accumulate(std::move(sweep));

    _acqPhase = AcqPhase::Idle;
    if (_runMode == RunMode::Continuous || _singleRequested
        || (_avgTarget > 1 && _avgCount < _avgTarget)) {
        // The text query returns whatever sweep the instrument last finished,
        // so asking again sooner than one sweep time only re-reads it.
        const int wait = std::clamp(int(_health.sweepTimeS * 1000.0), 50, 5000);
        QTimer::singleShot(wait, this, &VnaDevice::startSweep);
    }
}

void VnaDevice::setNeededMask(quint32 mask)
{
    _neededMask = (mask & 0x0Fu) ? (mask & 0x0Fu) : 0x0Fu;
}

void VnaDevice::fetchGroup()
{
    if (!isReady() || _acqPhase != AcqPhase::Waiting)
        return;
    _acqPhase = AcqPhase::Fetching;
    ScpiTransaction txn;
    txn.priority = ScpiPriority::Background;
    txn.add(scpiQuery("VNA:TRAC:GRO:DATA?", QStringLiteral("data"), 20000));
    submit(txn, Purpose::AcqFetch);
}

void VnaDevice::waitForGroup(bool first)
{
    _acqPhase = AcqPhase::Waiting;
    const double sweepMs = _health.sweepTimeS * 1000.0;
    // First look after half a sweep; after that, a fraction of the sweep time.
    const int interval = first ? std::clamp(int(sweepMs * 0.5), 0, 2000)
                               : std::clamp(int(sweepMs / 8.0), 10, 250);
    _pollTimer.start(interval);
}

void VnaDevice::handleGroupStarted(const ScpiResult& result)
{
    applyReplies(result);

    if (const ScpiReply* gc = result.find(QStringLiteral("groupcount")))
        _groupCountSet = gc->ok;
    if (const ScpiReply* init = result.find(QStringLiteral("init")); !init || !init->ok) {
        acquisitionFailed(QStringLiteral("could not start a sweep: %1")
                              .arg(init ? init->error : QStringLiteral("?")));
        return;
    }
    waitForGroup(true);
}

void VnaDevice::handleGroupStatus(const ScpiResult& result)
{
    const ScpiReply* stat = result.find(QStringLiteral("stat"));
    if (!stat || !stat->ok) {
        acquisitionFailed(QStringLiteral("no sweep status: %1").arg(stat ? stat->error : QStringLiteral("?")));
        return;
    }

    // state,groupId,done,total,reason - ARMED, COLLECTING, DONE, ABORTED, IDLE.
    const QByteArray stateName = stat->text.split(',').value(0).trimmed().toUpper();
    // DONE here means the block already fetched belonged to an older group;
    // a newer one is not finished, so keep waiting like the other two.
    if (stateName == "ARMED" || stateName == "COLLECTING" || stateName == "DONE") {
        _nextStatusCheckMs = QDateTime::currentMSecsSinceEpoch() + 1000;
        waitForGroup(false);
        return;
    }

    // The instrument aborts a group whenever a setting changes under it,
    // which is routine: start another without fuss, unless it keeps happening.
    if (++_groupRestarts > 5) {
        acquisitionFailed(QStringLiteral("sweep %1").arg(QString::fromLatin1(stat->text)));
        return;
    }
    _acqPhase = AcqPhase::Idle;
    startSweep();
}

void VnaDevice::handleGroupData(const ScpiResult& result)
{
    const ScpiReply* data = result.find(QStringLiteral("data"));
    if (!data || data->timedOut) {
        acquisitionFailed(QStringLiteral("no sweep data: %1").arg(data ? data->error : QStringLiteral("?")));
        return;
    }

    if (!data->isBlock) {
        // GROUP_NOT_READY: the sweep is still running. Keep asking; look at
        // the group status now and then in case it was aborted.
        // GROUP_NO_DATA: the group was aborted (a setting changed under it)
        // and left nothing. The status query confirms it and restarts.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const bool notReady = data->error == QLatin1String("GROUP_NOT_READY");
        const bool noData = data->error == QLatin1String("GROUP_NO_DATA");
        if ((!notReady && !noData) || now > _sweepDeadlineMs) {
            acquisitionFailed(QStringLiteral("sweep did not complete (%1)").arg(data->error));
        } else if (noData || now > _nextStatusCheckMs) {
            ScpiTransaction txn;
            txn.priority = ScpiPriority::Background;
            txn.add(scpiQuery("VNA:ACQ:GRO:STAT?", QStringLiteral("stat")));
            submit(txn, Purpose::AcqPoll);
        } else {
            waitForGroup(false);
        }
        return;
    }

    QList<SweepData> sweeps;
    QString err;
    quint64 groupId = 0;
    if (!VnaFrame::parseGroup(data->block, sweeps, err, &groupId)) {
        acquisitionFailed(QStringLiteral("could not decode sweep data: %1").arg(err));
        return;
    }

    // After a group that ended in an error the instrument keeps serving the
    // PREVIOUS group's block. Showing that as a new sweep would freeze the
    // display on old data with no sign anything was wrong, so ask what
    // happened instead.
    if (_haveGroupId && groupId == _lastGroupId) {
        ScpiTransaction txn;
        txn.priority = ScpiPriority::Background;
        txn.add(scpiQuery("VNA:ACQ:GRO:STAT?", QStringLiteral("stat")));
        submit(txn, Purpose::AcqPoll);
        return;
    }
    _lastGroupId = groupId;
    _haveGroupId = true;
    _groupRestarts = 0;

    for (SweepData& s : sweeps) {
        switch (_stim.mode) {
        case SweepMode::LinFreq: s.xKind = XKind::Frequency; break;
        case SweepMode::LogFreq: s.xKind = XKind::Frequency; s.logSpacing = true; break;
        case SweepMode::Power:   s.xKind = XKind::Power; break;
        case SweepMode::CW:      s.xKind = XKind::Time; break;
        }
        accumulate(std::move(s));
    }

    _acqPhase = AcqPhase::Idle;
    if (_runMode == RunMode::Continuous || _singleRequested
        || (_avgTarget > 1 && _avgCount < _avgTarget))
        startSweep();
}

void VnaDevice::accumulate(SweepData sweep)
{
    if (_avgTarget <= 1) {
        sweep.averages = 1;
        emit sweepReady(sweep);
        return;
    }

    // A change of axis invalidates the running average.
    const bool compatible = _avgCount > 0 && _avgSweep.x.size() == sweep.x.size()
        && _avgSweep.mask == sweep.mask && !sweep.x.empty()
        && _avgSweep.x.front() == sweep.x.front() && _avgSweep.x.back() == sweep.x.back();
    if (!compatible) {
        _avgSweep = sweep;
        _avgCount = 1;
    } else {
        // Cumulative mean up to the target count, then exponential with the
        // same weight, so the display keeps tracking a drifting DUT.
        _avgCount = std::min(_avgCount + 1, _avgTarget);
        const double w = 1.0 / _avgCount;
        for (int k = 0; k < kSParamCount; ++k) {
            auto& acc = _avgSweep.s[size_t(k)];
            const auto& in = sweep.s[size_t(k)];
            for (size_t i = 0; i < acc.size() && i < in.size(); ++i)
                acc[i] += (in[i] - acc[i]) * w;
        }
        _avgSweep.sequence = sweep.sequence;
    }
    _avgSweep.averages = _avgCount;
    emit sweepReady(_avgSweep);
}

void VnaDevice::acquisitionFailed(const QString& why)
{
    emit message(amber(QStringLiteral("Acquisition: %1").arg(why)));
    _acqPhase = AcqPhase::Idle;
    _groupCountSet = false;
    _groupRestarts = 0;
    if (_runMode == RunMode::Continuous && isReady())
        QTimer::singleShot(1000, this, [this] {
            if (_runMode == RunMode::Continuous)
                startSweep();
        });
}
