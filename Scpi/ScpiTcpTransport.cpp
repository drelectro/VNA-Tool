#include "ScpiTcpTransport.h"

#include "ScpiParse.h"

#include <QElapsedTimer>
#include <QTcpSocket>

ScpiTcpTransport::ScpiTcpTransport(QObject* parent)
    : ScpiTransport(parent)
{
}

ScpiTcpTransport::~ScpiTcpTransport()
{
    close();
}

bool ScpiTcpTransport::isOpen() const
{
    return _socket && _socket->state() == QAbstractSocket::ConnectedState;
}

bool ScpiTcpTransport::open(const ScpiEndpoint& endpoint, int timeoutMs, QString& err)
{
    close();
    _rx.clear();

    // Created here, on the worker thread, so the socket shares its affinity.
    _socket = new QTcpSocket(this);
    _socket->connectToHost(endpoint.host, endpoint.port);
    if (!_socket->waitForConnected(timeoutMs)) {
        err = QStringLiteral("connect to %1 failed: %2")
                  .arg(endpoint.displayName(), _socket->errorString());
        delete _socket;
        _socket = nullptr;
        return false;
    }

    // Tiny request/response pairs: Nagle would only add latency.
    _socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    _socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    return true;
}

void ScpiTcpTransport::close()
{
    if (!_socket)
        return;
    if (_socket->state() == QAbstractSocket::ConnectedState) {
        _socket->disconnectFromHost();
        if (_socket->state() != QAbstractSocket::UnconnectedState)
            _socket->waitForDisconnected(1000);
    }
    delete _socket;
    _socket = nullptr;
    _rx.clear();
}

void ScpiTcpTransport::discardInput()
{
    _rx.clear();
    if (!_socket)
        return;
    while (_socket->waitForReadyRead(150))
        _socket->readAll();
    _socket->readAll();
}

bool ScpiTcpTransport::write(const QByteArray& data, QString& err)
{
    if (!isOpen()) {
        err = QStringLiteral("not connected");
        return false;
    }
    if (_socket->write(data) != data.size()) {
        err = QStringLiteral("short write: %1").arg(_socket->errorString());
        return false;
    }
    if (!_socket->waitForBytesWritten(2000)) {
        err = QStringLiteral("write timed out: %1").arg(_socket->errorString());
        return false;
    }
    return true;
}

bool ScpiTcpTransport::pump(int timeoutMs, QString& err)
{
    if (!isOpen()) {
        err = QStringLiteral("not connected");
        return false;
    }
    if (!_socket->waitForReadyRead(timeoutMs)) {
        err = _socket->state() != QAbstractSocket::ConnectedState
                  ? QStringLiteral("connection lost: %1").arg(_socket->errorString())
                  : QStringLiteral("timed out waiting for a response");
        return false;
    }
    _rx.append(_socket->readAll());
    return true;
}

bool ScpiTcpTransport::readReply(int timeoutMs, QByteArray& out, bool& isBlock, QString& err)
{
    QElapsedTimer deadline;
    deadline.start();

    for (;;) {
        switch (ScpiParse::takeReply(_rx, out)) {
        case ScpiParse::ReplyResult::Line:  isBlock = false; return true;
        case ScpiParse::ReplyResult::Block: isBlock = true;  return true;
        case ScpiParse::ReplyResult::Malformed:
            err = QStringLiteral("malformed block header");
            return false;
        case ScpiParse::ReplyResult::NeedMoreData:
            break;
        }

        const int remaining = timeoutMs - static_cast<int>(deadline.elapsed());
        if (remaining <= 0) {
            err = QStringLiteral("timed out waiting for a response");
            return false;
        }
        if (!pump(remaining, err))
            return false;
    }
}
