#pragma once

#include "ScpiTransport.h"

#include <QAtomicInt>

#include <memory>

class SimVna;

// A transport backed by the in-process SimVna. Lines written are split and
// executed; whatever the simulator replies is appended to a receive buffer
// and read back through the same framing code the TCP transport uses.
class ScpiSimTransport : public ScpiTransport
{
    Q_OBJECT

public:
    explicit ScpiSimTransport(QObject* parent = nullptr);
    ~ScpiSimTransport() override;

    bool open(const ScpiEndpoint& endpoint, int timeoutMs, QString& err) override;
    void close() override;
    bool isOpen() const override { return _open; }
    bool write(const QByteArray& data, QString& err) override;
    bool readReply(int timeoutMs, QByteArray& out, bool& isBlock, QString& err) override;
    void discardInput() override { _rx.clear(); }

    SimVna* sim() const { return _sim.get(); }

    // Milliseconds slept per command, standing in for the real instrument's
    // 30-90 ms round trip. Zero in the bench.
    void setLatencyMs(int ms) { _latencyMs = ms; }

    // Fault injection: the replies to the next `count` lines written are held
    // back (so their reads time out) and delivered ahead of the reply to the
    // line after them - what the instrument does when a reply is slower than
    // the caller's timeout. Thread-safe.
    void withholdNextReplies(int count) { _withhold.storeRelaxed(count); }

private:
    std::unique_ptr<SimVna> _sim;
    QByteArray _rx;
    QByteArray _txPartial;
    QByteArray _late;
    QAtomicInt _withhold{0};
    bool _open = false;
    int  _latencyMs = 0;
};
