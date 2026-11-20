#pragma once

#include "Instrument/VnaTypes.h"

#include <QByteArray>
#include <QList>

#include <random>

// A command-level stand-in for VNStudio's SCPI server on a PNB-60.
//
// RULE: never more permissive than the real instrument. Every behaviour here
// was observed on hardware (docs/DEVICE_NOTES.md): silent success on set
// commands, error tokens, rejected rather than clamped out-of-range values,
// no unit suffixes, the VGB1 block layout. When the instrument turns out to
// differ, tighten this in the same change.
//
// The device under test is synthetic: a 2.4 GHz band-pass filter between the
// ports, plus a small reflection 0.45 m down the Port 1 cable so time-domain
// and gating have something to find.

class SimVna
{
public:
    SimVna();

    // Executes one command line (no terminator). Returns the bytes the
    // instrument would send back, terminator included; empty for a set
    // command that succeeded.
    QByteArray execute(const QByteArray& line);

    // The simulated DUT, noise-free. Exposed so the bench can compare.
    static Cplx dut(SParam p, double freqHz);

    void setNoiseEnabled(bool on) { _noise = on; }

private:
    QByteArray handleQuery(const QByteArray& header, const QByteArray& arg);
    QByteArray handleCommand(const QByteArray& header, const QByteArray& arg);
    SweepData  makeSweep();
    std::vector<double> stimulusAxis() const;

    Stimulus _st;
    int      _avg = 1;
    bool     _single = false;
    int      _pllSettle = 10;
    bool     _rawRx = false;

    int      _groupCount = 1;
    quint64  _groupId = 0;
    bool     _groupDone = false;
    bool     _groupError = false;
    QList<SweepData> _group;
    quint64  _sweepSeq = 1000;

    bool         _noise = true;
    std::mt19937 _rng{12345};
};
