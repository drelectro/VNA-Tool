#include "ScpiParse.h"

namespace ScpiParse {

bool takeLine(QByteArray& buffer, QByteArray& lineOut)
{
    const qsizetype lf = buffer.indexOf('\n');
    if (lf < 0)
        return false;
    lineOut = buffer.left(lf);
    if (lineOut.endsWith('\r'))
        lineOut.chop(1);
    buffer.remove(0, lf + 1);
    return true;
}

ReplyResult takeReply(QByteArray& buffer, QByteArray& out)
{
    if (buffer.isEmpty())
        return ReplyResult::NeedMoreData;

    if (buffer.at(0) != '#')
        return takeLine(buffer, out) ? ReplyResult::Line : ReplyResult::NeedMoreData;

    if (buffer.size() < 2)
        return ReplyResult::NeedMoreData;
    const int nDigits = buffer.at(1) - '0';
    if (nDigits < 1 || nDigits > 9)
        return ReplyResult::Malformed;
    if (buffer.size() < 2 + nDigits)
        return ReplyResult::NeedMoreData;

    bool ok = false;
    const qsizetype len = buffer.mid(2, nDigits).toLongLong(&ok);
    if (!ok || len < 0)
        return ReplyResult::Malformed;

    // The instrument follows every block with one LF. Wait for it too, so it
    // is never left behind to be read as an empty reply to the next query.
    const qsizetype total = 2 + nDigits + len + 1;
    if (buffer.size() < total)
        return ReplyResult::NeedMoreData;

    out = buffer.mid(2 + nDigits, len);
    buffer.remove(0, buffer.at(total - 1) == '\n' ? total : total - 1);
    return ReplyResult::Block;
}

bool isErrorToken(const QByteArray& line)
{
    if (line.size() < 3)
        return false;
    bool underscore = false;
    for (const char c : line) {
        if (c == '_')
            underscore = true;
        else if (c < 'A' || c > 'Z')
            return false;
    }
    return underscore;
}

bool isQuery(const QByteArray& command)
{
    const QByteArray t = command.trimmed();
    const qsizetype space = t.indexOf(' ');
    const QByteArray header = space < 0 ? t : t.left(space);
    return header.endsWith('?');
}

bool toDouble(const QByteArray& field, double& out)
{
    bool ok = false;
    const double v = field.trimmed().toDouble(&ok);
    if (ok)
        out = v;
    return ok;
}

bool toInt(const QByteArray& field, int& out)
{
    double d = 0;
    if (!toDouble(field, d))
        return false;
    out = static_cast<int>(d);
    return true;
}

bool toBool(const QByteArray& field, bool& out)
{
    const QByteArray t = field.trimmed().toUpper();
    if (t == "TRUE" || t == "ON" || t == "1") { out = true; return true; }
    if (t == "FALSE" || t == "OFF" || t == "0") { out = false; return true; }
    return false;
}

bool parseTriplets(const QByteArray& line,
                   std::vector<double>& x,
                   std::vector<std::complex<double>>& values)
{
    x.clear();
    values.clear();
    qsizetype pos = 0;
    while (true) {
        const qsizetype open = line.indexOf('[', pos);
        if (open < 0)
            break;
        const qsizetype close = line.indexOf(']', open);
        if (close < 0)
            return false;
        const QList<QByteArray> parts = line.mid(open + 1, close - open - 1).split(',');
        if (parts.size() != 3)
            return false;
        double xv = 0, re = 0, im = 0;
        if (!toDouble(parts[0], xv) || !toDouble(parts[1], re) || !toDouble(parts[2], im))
            return false;
        x.push_back(xv);
        values.emplace_back(re, im);
        pos = close + 1;
    }
    return !x.empty();
}

} // namespace ScpiParse
