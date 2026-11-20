#include "ScpiSession.h"

#include "ScpiParse.h"
#include "ScpiTransport.h"

#include <QElapsedTimer>
#include <QThread>
#include <QTimer>

namespace {
constexpr int kConnectTimeoutMs = 5000;
constexpr int kMaxConsecutiveTimeouts = 3;

void registerTypes()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    qRegisterMetaType<ScpiEndpoint>();
    qRegisterMetaType<ScpiTransaction>();
    qRegisterMetaType<ScpiResult>();
    qRegisterMetaType<ScpiTrafficRecord>();
    qRegisterMetaType<ScpiSessionState>();
}
} // namespace

ScpiSession::ScpiSession(ScpiTransport* transport, ScpiEndpoint endpoint, QObject* parent)
    : QObject(parent)
    , _transport(transport)
    , _endpoint(std::move(endpoint))
{
    registerTypes();
    _transport->setParent(this);
}

ScpiSession::~ScpiSession() = default;

ScpiSession* ScpiSession::createOnOwnThread(const ScpiEndpoint& endpoint,
                                            ScpiTransport* transport,
                                            QThread*& threadOut)
{
    registerTypes();
    auto* thread = new QThread;
    thread->setObjectName(QStringLiteral("scpi-%1").arg(endpoint.displayName()));
    auto* session = new ScpiSession(transport, endpoint);
    session->moveToThread(thread);
    QObject::connect(thread, &QThread::finished, session, &QObject::deleteLater);
    thread->start();
    threadOut = thread;
    return session;
}

ScpiSessionState ScpiSession::state() const
{
    return static_cast<ScpiSessionState>(_state.loadRelaxed());
}

void ScpiSession::setState(ScpiSessionState s, const QString& detail)
{
    _state.storeRelaxed(static_cast<int>(s));
    emit stateChanged(s, detail);
}

quint64 ScpiSession::enqueue(const ScpiTransaction& transaction)
{
    const quint64 id = static_cast<quint64>(_nextId.fetchAndAddRelaxed(1));
    QMetaObject::invokeMethod(this, "submitInternal", Qt::QueuedConnection,
                              Q_ARG(ScpiTransaction, transaction), Q_ARG(quint64, id));
    return id;
}

void ScpiSession::open()
{
    setState(ScpiSessionState::Connecting, _endpoint.displayName());
    QString err;
    if (!_transport->open(_endpoint, kConnectTimeoutMs, err)) {
        emitTraffic(ScpiTrafficDirection::Error, err.toUtf8(), nullptr);
        setState(ScpiSessionState::Disconnected, err);
        return;
    }
    _consecutiveTimeouts = 0;
    setState(ScpiSessionState::Ready, _endpoint.displayName());
    processQueue();
}

void ScpiSession::close()
{
    if (state() == ScpiSessionState::Disconnected)
        return;
    setState(ScpiSessionState::Closing);
    failAll(QStringLiteral("session closed"));
    _transport->close();
    setState(ScpiSessionState::Disconnected, QStringLiteral("closed"));
}

void ScpiSession::failAll(const QString& reason)
{
    const QList<Queued> dropped = _queue;
    _queue.clear();
    for (const Queued& q : dropped) {
        ScpiResult r;
        r.id = q.id;
        r.cancelled = true;
        for (const ScpiRequest& req : q.txn.requests) {
            ScpiReply reply;
            reply.tag = req.tag;
            reply.error = reason;
            r.replies.append(reply);
        }
        emit finished(q.id, r);
    }
}

void ScpiSession::submitInternal(const ScpiTransaction& transaction, quint64 id)
{
    if (state() == ScpiSessionState::Disconnected || state() == ScpiSessionState::Closing) {
        ScpiResult r;
        r.id = id;
        r.cancelled = true;
        emit finished(id, r);
        return;
    }

    // A newer transaction with the same key supersedes any unsent older one.
    if (!transaction.coalesceKey.isEmpty()) {
        for (int i = _queue.size() - 1; i >= 0; --i) {
            if (_queue[i].txn.coalesceKey == transaction.coalesceKey) {
                ScpiResult r;
                r.id = _queue[i].id;
                r.cancelled = true;
                emit finished(r.id, r);
                _queue.removeAt(i);
            }
        }
    }

    // Ordered by priority, first-in-first-out within a priority.
    int at = _queue.size();
    for (int i = 0; i < _queue.size(); ++i) {
        if (static_cast<int>(_queue[i].txn.priority) > static_cast<int>(transaction.priority)) {
            at = i;
            break;
        }
    }
    _queue.insert(at, Queued{transaction, id});

    if (!_processing)
        QTimer::singleShot(0, this, &ScpiSession::processQueue);
}

void ScpiSession::processQueue()
{
    if (_processing || state() != ScpiSessionState::Ready)
        return;
    _processing = true;

    // One transaction per pass, then back to the event loop, so close() and
    // newly submitted higher-priority work are seen between transactions.
    if (!_queue.isEmpty()) {
        const Queued q = _queue.takeFirst();
        const ScpiResult result = runTransaction(q.txn, q.id);
        emit finished(q.id, result);

        if (_consecutiveTimeouts >= kMaxConsecutiveTimeouts) {
            const QString why = QStringLiteral("instrument stopped responding");
            emitTraffic(ScpiTrafficDirection::Error, why.toUtf8(), nullptr);
            failAll(why);
            _transport->close();
            setState(ScpiSessionState::Disconnected, why);
        }
    }

    _processing = false;
    if (!_queue.isEmpty() && state() == ScpiSessionState::Ready)
        QTimer::singleShot(0, this, &ScpiSession::processQueue);
}

ScpiResult ScpiSession::runTransaction(const ScpiTransaction& txn, quint64 id)
{
    ScpiResult result;
    result.id = id;
    result.ok = true;

    for (const ScpiRequest& req : txn.requests) {
        const ScpiReply reply = runRequest(req, txn);
        result.replies.append(reply);
        if (!reply.ok)
            result.ok = false;
        if (reply.timedOut) {
            // Whatever follows would be read against a misaligned stream.
            for (qsizetype i = result.replies.size(); i < txn.requests.size(); ++i) {
                ScpiReply skipped;
                skipped.tag = txn.requests[i].tag;
                skipped.error = QStringLiteral("skipped after a timeout");
                result.replies.append(skipped);
            }
            break;
        }
    }
    return result;
}

ScpiReply ScpiSession::runRequest(const ScpiRequest& req, const ScpiTransaction& txn)
{
    ScpiReply reply;
    reply.tag = req.tag;

    QElapsedTimer timer;
    timer.start();

    const bool isCommand = req.kind == ScpiKind::Command;
    QByteArray payload = req.text + '\n';
    if (isCommand)
        payload += "*OPC?\n";

    emitTraffic(ScpiTrafficDirection::Tx, req.text, &txn);

    QString err;
    if (!_transport->write(payload, err)) {
        reply.error = err;
        reply.timedOut = true;
        ++_consecutiveTimeouts;
        emitTraffic(ScpiTrafficDirection::Error, err.toUtf8(), &txn);
        return reply;
    }

    QByteArray data;
    bool isBlock = false;

    if (isCommand) {
        // Read up to the "1" from *OPC?. Anything before it is the command's
        // error reply.
        reply.ok = true;
        for (;;) {
            if (!_transport->readReply(req.timeoutMs, data, isBlock, err)) {
                reply.ok = false;
                reply.timedOut = true;
                reply.error = err;
                break;
            }
            if (!isBlock && data == "1")
                break;
            reply.ok = false;
            reply.error = isBlock ? QStringLiteral("unexpected block") : QString::fromLatin1(data);
            emitTraffic(ScpiTrafficDirection::Error, data.left(200), &txn, timer.nsecsElapsed() / 1000);
        }
    } else {
        if (!_transport->readReply(req.timeoutMs, data, isBlock, err)) {
            reply.timedOut = true;
            reply.error = err;
        } else if (isBlock) {
            reply.isBlock = true;
            reply.block = data;
            reply.ok = true;
            emitTraffic(ScpiTrafficDirection::Rx,
                        QByteArrayLiteral("<") + QByteArray::number(data.size()) + " byte block>",
                        &txn, timer.nsecsElapsed() / 1000);
        } else {
            reply.text = data;
            if (ScpiParse::isErrorToken(data)) {
                reply.error = QString::fromLatin1(data);
                emitTraffic(ScpiTrafficDirection::Error, data, &txn, timer.nsecsElapsed() / 1000);
            } else {
                reply.ok = true;
                emitTraffic(ScpiTrafficDirection::Rx, data, &txn, timer.nsecsElapsed() / 1000);
            }
        }
    }

    if (reply.timedOut) {
        ++_consecutiveTimeouts;
        emitTraffic(ScpiTrafficDirection::Error, reply.error.toUtf8(), &txn);
        _transport->discardInput();
    } else {
        _consecutiveTimeouts = 0;
    }

    reply.elapsedUs = timer.nsecsElapsed() / 1000;
    return reply;
}

void ScpiSession::emitTraffic(ScpiTrafficDirection dir, const QByteArray& text,
                              const ScpiTransaction* txn, qint64 elapsedUs)
{
    ScpiTrafficRecord rec;
    rec.direction = dir;
    rec.text = text;
    rec.elapsedUs = elapsedUs;
    if (txn) {
        rec.fromUser = txn->fromUser;
        rec.background = txn->priority == ScpiPriority::Background;
    }
    emit traffic(rec);
}
