#pragma once

#include "Scpi/ScpiTypes.h"
#include "VnaTypes.h"

#include <QHash>
#include <QObject>
#include <QTimer>

class QThread;
class ScpiSession;

// The instrument as the rest of the application sees it.
//
// Lives on the main thread; all I/O happens on the ScpiSession's worker thread
// and comes back through queued signals. Three jobs:
//
//  * CONNECT - read identity, limits and every stimulus setting. Connecting
//    never changes anything on the instrument.
//
//  * STIMULUS - each setter sends the command followed by a read-back query,
//    and the value shown is always the one read back. The instrument rejects
//    rather than clamps, so a refused value simply leaves the old one in place
//    and the refusal is reported through message().
//
//  * ACQUISITION - sweeps are fetched through the instrument's sweep-group
//    mechanism (GROups:COUNt 1 / INITiate / STATus? / TRAC:GROups:DATA?),
//    which returns all four S-parameters of ONE sweep in one binary block.
//    Four separate VNA:TRAC:DATA? queries would each come from a different
//    sweep. A group also sweeps on demand when the tablet's acquisition is
//    stopped, so Hold on the tablet does not freeze this application.
//
// One rotating status/settings query rides along with every sweep, so changes
// made on the tablet's own screen appear here within a few seconds without
// starving the sweep loop.

class VnaDevice : public QObject
{
    Q_OBJECT

public:
    enum class State : quint8 { Disconnected, Connecting, Identifying, Ready };
    enum class RunMode : quint8 { Hold, Continuous };

    explicit VnaDevice(QObject* parent = nullptr);
    ~VnaDevice() override;

    State   state() const { return _state; }
    bool    isReady() const { return _state == State::Ready; }
    bool    isSimulated() const { return _endpoint.simulated; }
    RunMode runMode() const { return _runMode; }
    ScpiEndpoint endpoint() const { return _endpoint; }

    const DeviceInfo& info() const { return _info; }
    const Stimulus&   stimulus() const { return _stim; }
    const Health&     health() const { return _health; }
    int averaging() const { return _avgTarget; }

    // Simulated round-trip latency per command; 0 for the bench.
    void setSimLatencyMs(int ms) { _simLatencyMs = ms; }

public slots:
    void connectTo(const ScpiEndpoint& endpoint);
    void disconnectFrom();

    // --- Stimulus ---
    void setStart(double hz);
    void setStop(double hz);
    void setStartStop(double startHz, double stopHz);
    void setCenterSpan(double centerHz, double spanHz);
    void setPoints(int points);
    void setIfbw(double hz);
    void setPower(double dbm);
    void setSweepMode(SweepMode mode);
    void setPowerRange(double startDbm, double stopDbm);
    void setCwFrequency(double hz);
    void setDwell(double seconds);
    void setExternalReference(bool external);
    void refreshStimulus();

    // --- Acquisition ---
    void setRunMode(RunMode mode);
    void single();
    // Sweeps averaged here, on the PC, as a complex running average.
    void setAveraging(int count);
    void restartAveraging();
    // Which S-parameters are on screen (bit per SParam). Only matters above
    // the fast-transfer point limit, where each one costs a separate query.
    void setNeededMask(quint32 mask);

    // --- Instrument-side actions; each one is user-initiated only ---
    void preset();                              // *RST
    void setDeviceRunning(bool run);            // VNA:ACQ:RUN / STOP on the tablet
    void saveState(const QString& devicePath);
    void loadState(const QString& devicePath);
    void loadCalibration(const QString& devicePath);

    // Console: sends one raw line at User priority. The reply comes back on
    // userReply() with the same id.
    quint64 sendRaw(const QByteArray& line);

signals:
    void stateChanged(VnaDevice::State state, const QString& detail);
    void infoChanged();
    void stimulusChanged();
    void healthChanged();
    void runModeChanged();
    void sweepReady(const SweepData& sweep);
    void message(const QString& markup);        // LogWidget markup
    void traffic(const ScpiTrafficRecord& record);
    void userReply(quint64 id, const ScpiResult& result);

private slots:
    void onSessionState(ScpiSessionState state, const QString& detail);
    void onFinished(quint64 id, const ScpiResult& result);

private:
    enum class Purpose : quint8 { Identify, Settings, Set, AcqStart, AcqPoll, AcqFetch, AcqAscii, Action, User };
    enum class AcqPhase : quint8 { Idle, Starting, Waiting, Fetching };

    quint64 submit(ScpiTransaction txn, Purpose purpose, const QString& note = QString());
    void    setState(State s, const QString& detail = QString());
    void    applyReplies(const ScpiResult& result);
    bool    applyReply(const ScpiReply& reply);
    void    sendSetting(const QByteArray& command, const QList<QByteArray>& readBack,
                        const QString& key, const QString& what);
    ScpiRequest nextPollQuery();
    void    appendAllSettingQueries(ScpiTransaction& txn) const;

    void startSweep();
    void startAsciiSweep();
    void handleAsciiData(const ScpiResult& result);
    void fetchGroup();
    void waitForGroup(bool first);
    void handleGroupStarted(const ScpiResult& result);
    void handleGroupStatus(const ScpiResult& result);
    void handleGroupData(const ScpiResult& result);
    void acquisitionFailed(const QString& why);
    void accumulate(SweepData sweep);

    ScpiEndpoint _endpoint;
    ScpiSession* _session = nullptr;
    QThread*     _thread = nullptr;
    State        _state = State::Disconnected;

    DeviceInfo _info;
    Stimulus   _stim;
    Health     _health;
    bool       _freqSweepLog = false;       // VNA:SWEEPTYPE?, kept when mode is not a frequency sweep
    QString    _sweepKind = QStringLiteral("FREQUENCY");

    struct Pending
    {
        Purpose purpose = Purpose::Set;
        QString note;
    };
    QHash<quint64, Pending> _pending;

    RunMode  _runMode = RunMode::Continuous;
    AcqPhase _acqPhase = AcqPhase::Idle;
    bool     _singleRequested = false;
    bool     _groupCountSet = false;
    int      _groupRestarts = 0;
    quint64  _lastGroupId = 0;
    bool     _haveGroupId = false;
    quint32  _neededMask = 0x0F;
    bool     _asciiWarned = false;
    quint64  _sweepsStarted = 0;
    qint64   _sweepStartMs = 0;
    qint64   _nextStatusCheckMs = 0;
    QTimer   _pollTimer;
    QTimer   _idleTimer;
    qint64   _sweepDeadlineMs = 0;
    int      _pollIndex = 0;

    int       _avgTarget = 1;
    int       _avgCount = 0;
    SweepData _avgSweep;

    int _simLatencyMs = 0;
};
