#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <complex>
#include <vector>

// Stateless SCPI response parsing. Free functions on caller-owned buffers, so
// the whole layer is exercised from Bench/VnaBench with no socket or thread.

namespace ScpiParse {

// Removes one complete line from the front of `buffer`, terminator stripped
// (LF or CRLF). Returns false and leaves `buffer` untouched if none is complete.
bool takeLine(QByteArray& buffer, QByteArray& lineOut);

enum class ReplyResult : quint8 { NeedMoreData, Line, Block, Malformed };

// Removes one complete reply from the front of `buffer`: a definite-length
// block (#<n><len><data>, plus the LF the instrument sends after it) when the
// buffer starts with '#', otherwise a line.
ReplyResult takeReply(QByteArray& buffer, QByteArray& out);

// True for the instrument's error replies: GATEWAY_*, SERVER_*, NO_DATA,
// GROUP_NOT_READY. The rule is "only A-Z and '_', with at least one '_'",
// which no legitimate value reply (TRUE, FREQUENCY, LIN, INT, OFF) matches.
bool isErrorToken(const QByteArray& line);

// True if the command's header ends with '?'.
bool isQuery(const QByteArray& command);

bool toDouble(const QByteArray& field, double& out);
bool toInt(const QByteArray& field, int& out);
// Accepts TRUE/FALSE, ON/OFF, 1/0 in any case.
bool toBool(const QByteArray& field, bool& out);

// Parses a VNA:TRAC:DATA? reply: [x,re,im],[x,re,im],...
bool parseTriplets(const QByteArray& line,
                   std::vector<double>& x,
                   std::vector<std::complex<double>>& values);

} // namespace ScpiParse
