#pragma once

#include "ScpiTypes.h"

#include <QByteArray>
#include <QObject>
#include <QString>

// Byte-level transport for one SCPI session.
//
// Deliberately BLOCKING: the session owns a worker thread with nothing else on
// it, so a bounded blocking read is simpler and easier to reason about than a
// signal-driven state machine. The UI thread never touches a transport.
//
//   ScpiTcpTransport - QTcpSocket to VNStudio's SCPI server
//   ScpiSimTransport - a SimVna, so everything above this interface (framing,
//                      error tokens, block parsing, timeouts) runs identically
//                      with no hardware

class ScpiTransport : public QObject
{
    Q_OBJECT

public:
    explicit ScpiTransport(QObject* parent = nullptr) : QObject(parent) {}

    virtual bool open(const ScpiEndpoint& endpoint, int timeoutMs, QString& err) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // Writes `data` exactly as given. The caller supplies terminators, so
    // several lines can go out in one TCP write.
    virtual bool write(const QByteArray& data, QString& err) = 0;

    // Reads one reply: a line, or a definite-length block. `isBlock` tells
    // which. Returns false on timeout or transport error.
    virtual bool readReply(int timeoutMs, QByteArray& out, bool& isBlock, QString& err) = 0;

    // Drops everything buffered and anything still arriving. Used after a
    // timeout, where an orphaned reply would otherwise be taken as the answer
    // to the NEXT query and put every later reply off by one.
    virtual void discardInput() = 0;
};
