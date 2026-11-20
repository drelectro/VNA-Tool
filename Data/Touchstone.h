#pragma once

#include "Instrument/VnaTypes.h"

#include <QString>

// Touchstone (.s1p / .s2p) and CSV files.

namespace Touchstone {

// Writes a 2-port file: "# Hz S RI R <z0>", columns S11 S21 S12 S22.
// Needs all four S-parameters on a frequency axis.
bool writeS2p(const QString& path, const SweepData& sweep, double z0, QString& err);

// Writes a 1-port file from `param` (S11 or S22).
bool writeS1p(const QString& path, const SweepData& sweep, SParam param, double z0, QString& err);

// Reads a 1- or 2-port file in any standard unit (Hz/kHz/MHz/GHz) and format
// (RI/MA/DB). A 1-port file fills S11 only.
bool read(const QString& path, SweepData& out, double& z0, QString& err);

// One row per point: x, then re and im of each S-parameter present.
bool writeCsv(const QString& path, const SweepData& sweep, QString& err);

} // namespace Touchstone
