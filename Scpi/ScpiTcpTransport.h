#pragma once

#include "ScpiTransport.h"

class QTcpSocket;

class ScpiTcpTransport : public ScpiTransport
{
    Q_OBJECT

public:
    explicit ScpiTcpTransport(QObject* parent = nullptr);
    ~ScpiTcpTransport() override;

    bool open(const ScpiEndpoint& endpoint, int timeoutMs, QString& err) override;
    void close() override;
    bool isOpen() const override;
    bool write(const QByteArray& data, QString& err) override;
    bool readReply(int timeoutMs, QByteArray& out, bool& isBlock, QString& err) override;
    void discardInput() override;

private:
    bool pump(int timeoutMs, QString& err);

    QTcpSocket* _socket = nullptr;
    QByteArray  _rx;
};
