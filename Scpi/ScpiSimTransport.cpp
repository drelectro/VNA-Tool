#include "ScpiSimTransport.h"

#include "ScpiParse.h"
#include "Sim/SimVna.h"

#include <QThread>

ScpiSimTransport::ScpiSimTransport(QObject* parent)
    : ScpiTransport(parent)
    , _sim(std::make_unique<SimVna>())
{
}

ScpiSimTransport::~ScpiSimTransport() = default;

bool ScpiSimTransport::open(const ScpiEndpoint&, int, QString&)
{
    _rx.clear();
    _txPartial.clear();
    _open = true;
    return true;
}

void ScpiSimTransport::close()
{
    _open = false;
    _rx.clear();
}

bool ScpiSimTransport::write(const QByteArray& data, QString& err)
{
    if (!_open) {
        err = QStringLiteral("not connected");
        return false;
    }
    _txPartial.append(data);
    QByteArray line;
    while (ScpiParse::takeLine(_txPartial, line)) {
        if (_latencyMs > 0)
            QThread::msleep(static_cast<unsigned long>(_latencyMs));
        const QByteArray reply = _sim->execute(line);
        if (_withhold.loadRelaxed() > 0) {
            _withhold.fetchAndSubRelaxed(1);
            _late.append(reply);
        } else {
            _rx.append(_late);
            _late.clear();
            _rx.append(reply);
        }
    }
    return true;
}

bool ScpiSimTransport::readReply(int, QByteArray& out, bool& isBlock, QString& err)
{
    switch (ScpiParse::takeReply(_rx, out)) {
    case ScpiParse::ReplyResult::Line:  isBlock = false; return true;
    case ScpiParse::ReplyResult::Block: isBlock = true;  return true;
    case ScpiParse::ReplyResult::Malformed:
        err = QStringLiteral("malformed block header");
        return false;
    case ScpiParse::ReplyResult::NeedMoreData:
        break;
    }
    // The simulator answers synchronously, so an empty buffer means no reply
    // is coming. Report it at once rather than sleeping out the timeout.
    err = QStringLiteral("timed out waiting for a response");
    return false;
}
