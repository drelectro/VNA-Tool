#pragma once

#include "ScpiTypes.h"

#include <QAtomicInt>
#include <QList>
#include <QMutex>
#include <QObject>

class QThread;
class ScpiTransport;

// One SCPI conversation with the instrument.
//
// THREADING CONTRACT
// A session and its transport live on a dedicated worker thread. The only
// thread-safe entry points are enqueue(), state() and the queued slots. The UI
// thread never touches a socket and never blocks.
//
// Transactions run one at a time, each to completion; between transactions the
// event loop runs, so close() stays responsive.
//
// WHY COMMANDS ARE FOLLOWED BY *OPC?
// The instrument says nothing when a set command succeeds and sends an error
// token when it fails. Without a marker there is no way to tell "succeeded"
// from "error line still in flight", and a late error line would be read as
// the answer to the next query. Sending *OPC? after every command and reading
// up to its "1" makes both outcomes explicit and keeps the stream aligned.

class ScpiSession : public QObject
{
    Q_OBJECT

public:
    // Takes ownership of `transport`.
    ScpiSession(ScpiTransport* transport, ScpiEndpoint endpoint, QObject* parent = nullptr);
    ~ScpiSession() override;

    // Creates a thread, moves the session and transport onto it and starts it.
    // The caller owns the thread and must quit()/wait() it.
    static ScpiSession* createOnOwnThread(const ScpiEndpoint& endpoint,
                                          ScpiTransport* transport,
                                          QThread*& threadOut);

    // --- Thread-safe ---
    ScpiSessionState state() const;
    ScpiEndpoint     endpoint() const { return _endpoint; }

    // Queues a transaction and returns its id. The result arrives on
    // finished(); a transaction that is coalesced away or dropped at close
    // still reports finished() with `cancelled` set.
    quint64 enqueue(const ScpiTransaction& transaction);

public slots:
    void open();
    void close();

signals:
    void stateChanged(ScpiSessionState state, const QString& detail);
    void finished(quint64 id, const ScpiResult& result);
    void traffic(const ScpiTrafficRecord& record);

private slots:
    void submitInternal(const ScpiTransaction& transaction, quint64 id);
    void processQueue();

private:
    struct Queued
    {
        ScpiTransaction txn;
        quint64 id = 0;
    };

    void setState(ScpiSessionState s, const QString& detail = QString());
    ScpiResult runTransaction(const ScpiTransaction& txn, quint64 id);
    ScpiReply  runRequest(const ScpiRequest& req, const ScpiTransaction& txn);
    void emitTraffic(ScpiTrafficDirection dir, const QByteArray& text,
                     const ScpiTransaction* txn, qint64 elapsedUs = 0);
    void failAll(const QString& reason);

    ScpiTransport* _transport = nullptr;
    ScpiEndpoint   _endpoint;

    QList<Queued> _queue;               // worker thread only
    QAtomicInt    _state{static_cast<int>(ScpiSessionState::Disconnected)};
    QAtomicInt    _nextId{1};
    bool          _processing = false;
    int           _consecutiveTimeouts = 0;
};
