#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>

// Value types shared by the SCPI transport, the instrument model and the UI.
// Everything here is a plain value type so it can cross thread boundaries
// through queued signal/slot connections.

// VNStudio's documented default. The port is user-settable under
// System -> SCPI on the instrument, so it is always entered explicitly.
inline constexpr quint16 kDefaultScpiPort = 5025;

struct ScpiEndpoint
{
    QString host;                       // "192.168.0.155", or "sim" when simulated
    quint16 port = kDefaultScpiPort;
    bool    simulated = false;

    bool isValid() const { return simulated || !host.isEmpty(); }

    QString displayName() const
    {
        return simulated ? QStringLiteral("Simulator")
                         : QStringLiteral("%1:%2").arg(host).arg(port);
    }
};

enum class ScpiKind : quint8 {
    // A set command. The instrument is SILENT on success and sends one line
    // holding an error token on failure, so the session appends "*OPC?" and
    // reads until its "1" arrives; anything before that is the error.
    Command,
    // One reply expected: an LF-terminated line or a definite-length block.
    Query,
};

enum class ScpiPriority : quint8 {
    Critical   = 0,
    User       = 1,     // anything the operator asked for
    Background = 2,     // sweep acquisition and status polling
};

struct ScpiRequest
{
    QByteArray text;                    // no terminator
    ScpiKind   kind = ScpiKind::Command;
    int        timeoutMs = 3000;
    QString    tag;                     // lets the sender recognise the reply

    ScpiRequest() = default;
    ScpiRequest(QByteArray t, ScpiKind k, QString tagText = QString(), int timeout = 3000)
        : text(std::move(t)), kind(k), timeoutMs(timeout), tag(std::move(tagText)) {}
};

inline ScpiRequest scpiCommand(QByteArray text, QString tag = QString())
{
    return ScpiRequest(std::move(text), ScpiKind::Command, std::move(tag));
}
inline ScpiRequest scpiQuery(QByteArray text, QString tag = QString(), int timeoutMs = 3000)
{
    return ScpiRequest(std::move(text), ScpiKind::Query, std::move(tag), timeoutMs);
}

struct ScpiReply
{
    QString    tag;
    QByteArray text;                    // line reply, terminator stripped
    QByteArray block;                   // payload of a definite-length block
    bool       isBlock  = false;
    bool       ok       = false;        // false on timeout or an error token
    bool       timedOut = false;
    QString    error;                   // error token or transport error
    qint64     elapsedUs = 0;
};

struct ScpiTransaction
{
    QList<ScpiRequest> requests;
    ScpiPriority       priority = ScpiPriority::User;

    // When non-empty, submitting this removes any UNSENT queued transaction
    // with the same key. Keeps a dragged control from queueing 30 round trips.
    QString coalesceKey;

    // True for traffic the console should show as typed by the operator.
    bool fromUser = false;

    ScpiTransaction() = default;
    explicit ScpiTransaction(ScpiRequest r) { requests.append(std::move(r)); }
    ScpiTransaction& add(ScpiRequest r) { requests.append(std::move(r)); return *this; }
};

struct ScpiResult
{
    quint64          id = 0;
    QList<ScpiReply> replies;           // one per request, commands included
    bool             ok = false;        // every request succeeded
    bool             cancelled = false; // coalesced away or dropped at close

    const ScpiReply* find(const QString& tag) const
    {
        for (const ScpiReply& r : replies)
            if (r.tag == tag)
                return &r;
        return nullptr;
    }
    QString firstError() const
    {
        for (const ScpiReply& r : replies)
            if (!r.ok)
                return r.error;
        return QString();
    }
};

enum class ScpiTrafficDirection : quint8 { Tx, Rx, Info, Error };

struct ScpiTrafficRecord
{
    ScpiTrafficDirection direction = ScpiTrafficDirection::Info;
    QByteArray text;
    qint64     elapsedUs = 0;
    bool       fromUser = false;
    bool       background = false;      // acquisition/polling; hidden by default
};

enum class ScpiSessionState : quint8 {
    Disconnected,
    Connecting,
    Ready,
    Closing,
};

QString scpiSessionStateName(ScpiSessionState s);

Q_DECLARE_METATYPE(ScpiEndpoint)
Q_DECLARE_METATYPE(ScpiTransaction)
Q_DECLARE_METATYPE(ScpiResult)
Q_DECLARE_METATYPE(ScpiTrafficRecord)
Q_DECLARE_METATYPE(ScpiSessionState)
