#pragma once

#include "VnaTypes.h"

#include <QByteArray>
#include <QList>
#include <QString>

// Codec for the instrument's binary sweep blocks (VNA:TRAC:GROups:DATA?).
// Layout is documented in docs/DEVICE_NOTES.md; it was worked out from
// captures, not from vendor documentation, so the parser checks every size
// before reading and reports what it could not make sense of.

namespace VnaFrame {

// Parses a VGB1 group block into one SweepData per frame. `sequence` is taken
// from the frame's per-sweep counter. xKind is left as Frequency; the caller
// knows the sweep mode.
bool parseGroup(const QByteArray& block, QList<SweepData>& sweepsOut, QString& err,
                quint64* groupIdOut = nullptr);

// Builds a VGB1 block in the same layout. Used by the simulator and the bench.
QByteArray buildGroup(const QList<SweepData>& sweeps, quint64 groupId,
                      double ifbw, quint64 firstSequence);

} // namespace VnaFrame
