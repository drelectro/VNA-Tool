#include "VnaFrame.h"

#include <QtEndian>

#include <cstring>

namespace {

constexpr int kGroupHeader = 20;
constexpr int kFrameHeader = 136;

quint16 be16(const QByteArray& b, qsizetype at) { return qFromBigEndian<quint16>(b.constData() + at); }
quint32 be32(const QByteArray& b, qsizetype at) { return qFromBigEndian<quint32>(b.constData() + at); }
quint64 be64(const QByteArray& b, qsizetype at) { return qFromBigEndian<quint64>(b.constData() + at); }

double beF64(const QByteArray& b, qsizetype at)
{
    const quint64 raw = be64(b, at);
    double d;
    std::memcpy(&d, &raw, sizeof d);
    return d;
}
float beF32(const QByteArray& b, qsizetype at)
{
    const quint32 raw = be32(b, at);
    float f;
    std::memcpy(&f, &raw, sizeof f);
    return f;
}

void put16(QByteArray& b, quint16 v) { char t[2]; qToBigEndian(v, t); b.append(t, 2); }
void put32(QByteArray& b, quint32 v) { char t[4]; qToBigEndian(v, t); b.append(t, 4); }
void put64(QByteArray& b, quint64 v) { char t[8]; qToBigEndian(v, t); b.append(t, 8); }
void putF64(QByteArray& b, double d) { quint64 raw; std::memcpy(&raw, &d, sizeof raw); put64(b, raw); }
void putF32(QByteArray& b, float f) { quint32 raw; std::memcpy(&raw, &f, sizeof raw); put32(b, raw); }

bool parseFrame(const QByteArray& block, qsizetype at, qsizetype len, SweepData& out, QString& err)
{
    if (len < kFrameHeader || block.mid(at, 4) != "VTF1") {
        err = QStringLiteral("frame does not start with VTF1");
        return false;
    }
    const int headerSize = be16(block, at + 6);
    const quint32 points = be32(block, at + 80);
    const quint32 mask = be32(block, at + 84) & 0x0Fu;
    if (headerSize < kFrameHeader || points == 0 || points > 1000000) {
        err = QStringLiteral("implausible frame header (header %1, points %2)").arg(headerSize).arg(points);
        return false;
    }

    int params = 0;
    for (int i = 0; i < kSParamCount; ++i)
        params += (mask >> i) & 1u;
    const qsizetype need = headerSize + qsizetype(points) * 8 + qsizetype(params) * points * 8;
    if (len < need) {
        err = QStringLiteral("frame is %1 bytes, needs %2 for %3 points and mask %4")
                  .arg(len).arg(need).arg(points).arg(mask);
        return false;
    }

    out = SweepData{};
    out.mask = mask;
    out.sequence = be64(block, at + 64);
    out.x.resize(points);
    qsizetype p = at + headerSize;
    for (quint32 i = 0; i < points; ++i, p += 8)
        out.x[i] = beF64(block, p);

    for (int k = 0; k < kSParamCount; ++k) {
        if (!((mask >> k) & 1u))
            continue;
        auto& v = out.s[static_cast<size_t>(k)];
        v.resize(points);
        const qsizetype re = p;
        const qsizetype im = p + qsizetype(points) * 4;
        for (quint32 i = 0; i < points; ++i)
            v[i] = Cplx(beF32(block, re + qsizetype(i) * 4), beF32(block, im + qsizetype(i) * 4));
        p += qsizetype(points) * 8;
    }
    return true;
}

} // namespace

namespace VnaFrame {

bool parseGroup(const QByteArray& block, QList<SweepData>& sweepsOut, QString& err, quint64* groupIdOut)
{
    sweepsOut.clear();
    if (block.size() < kGroupHeader || !block.startsWith("VGB1")) {
        err = QStringLiteral("block does not start with VGB1");
        return false;
    }
    const int headerSize = be16(block, 6);
    const quint32 count = be32(block, 16);
    if (headerSize < kGroupHeader || count > 100000) {
        err = QStringLiteral("implausible group header");
        return false;
    }
    if (groupIdOut)
        *groupIdOut = be64(block, 8);

    qsizetype p = headerSize;
    for (quint32 i = 0; i < count; ++i) {
        if (p + 12 > block.size()) {
            err = QStringLiteral("group truncated before frame %1").arg(i);
            return false;
        }
        const qsizetype len = be32(block, p + 8);
        p += 12;
        if (p + len > block.size()) {
            err = QStringLiteral("frame %1 runs past the end of the block").arg(i);
            return false;
        }
        SweepData sweep;
        if (!parseFrame(block, p, len, sweep, err))
            return false;
        sweepsOut.append(std::move(sweep));
        p += len;
    }
    return !sweepsOut.isEmpty();
}

QByteArray buildGroup(const QList<SweepData>& sweeps, quint64 groupId,
                      double ifbw, quint64 firstSequence)
{
    QByteArray b;
    b.append("VGB1");
    put16(b, 1);
    put16(b, kGroupHeader);
    put64(b, groupId);
    put32(b, static_cast<quint32>(sweeps.size()));

    quint64 seq = firstSequence;
    for (const SweepData& s : sweeps) {
        QByteArray f;
        f.append("VTF1");
        put16(f, 1);
        put16(f, kFrameHeader);
        put32(f, 277);
        put32(f, 0x01000000u);
        f.append(QByteArray(16, '\0'));
        for (int i = 0; i < 4; ++i)
            put64(f, 0);
        put64(f, seq++);
        put64(f, 0);
        put32(f, static_cast<quint32>(s.x.size()));
        put32(f, s.mask);
        put32(f, 1);
        put32(f, 1);
        putF64(f, s.x.empty() ? 0.0 : s.x.front());
        putF64(f, s.x.empty() ? 0.0 : s.x.back());
        putF64(f, ifbw);
        putF64(f, s.x.empty() ? 0.0 : s.x.back());
        put64(f, 0);
        for (const double x : s.x)
            putF64(f, x);
        for (int k = 0; k < kSParamCount; ++k) {
            if (!((s.mask >> k) & 1u))
                continue;
            const auto& v = s.s[static_cast<size_t>(k)];
            for (const Cplx& c : v)
                putF32(f, static_cast<float>(c.real()));
            for (const Cplx& c : v)
                putF32(f, static_cast<float>(c.imag()));
        }

        put64(b, 0);
        put32(b, static_cast<quint32>(f.size()));
        b.append(f);
    }
    put32(b, 0);    // trailer; algorithm not identified, so not reproduced
    return b;
}

} // namespace VnaFrame
