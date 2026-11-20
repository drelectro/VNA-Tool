# VNA Tool

A remote front panel for the Harogic PNB-60 vector network analyzer, in Qt 6 / C++.

The PNB-60 runs Harogic's VNStudio on its built-in computer and cannot be operated
from another PC. VNStudio does have a SCPI server, but it only covers stimulus
settings and returning complex S-parameters. This application drives the instrument
through that server and does everything else itself.

## What it does

- Live sweeps of all four S-parameters, about 2.7 per second at 401 points.
- Stimulus: start/stop/center/span, points, IF bandwidth, power, linear, log, power
  and CW sweeps. Every value shown is read back from the instrument.
- Any number of plots and traces. Formats: log and linear magnitude, phase,
  unwrapped phase, real, imaginary, SWR, group delay, impedance, Smith, polar.
- Markers without a count limit: delta, coupling across traces, peak and target
  searches, tracking that follows each sweep. Bandwidth and notch search that
  updates every sweep.
- Memory traces and trace math, smoothing, PC-side averaging.
- Limit lines with pass/fail.
- Time domain (band-pass, low-pass impulse and step) with a time or distance axis,
  and time gating.
- Port extension.
- Touchstone `.s1p`/`.s2p` and CSV export, plot images, Touchstone files opened as
  comparison traces, workspaces saved to disk.
- A SCPI console on the same connection, and a built-in simulator.

## What it cannot do

These have no SCPI command on the instrument:

- **Calibration.** Calibrate on the tablet in VNStudio. The data this application
  receives is whatever VNStudio is producing. A calibration or state file saved on
  the tablet can be loaded from the Instrument panel by its path on the tablet.
- Segment sweep, external trigger, trigger output.

## Connecting

1. On the tablet, in VNStudio: **System → SCPI**, tick Enable, note the port.
2. Connect the tablet's Ethernet port to your network.
3. In VNA Tool: **File → Connect**, enter the tablet's address and that port.

VNStudio must stay running; it is the server. Putting the tablet's own sweep on
Stop (Instrument panel) makes remote updates faster, because the instrument then
sweeps on demand instead of waiting for its own sweep to come round.

## Building

Windows, Visual Studio 2026, CMake 3.21 or later, Qt 6 (set `QTDIR` to the kit, for
example `C:\Qt\6.11.1\msvc2022_64`). No other dependencies.

```
cmake --preset x64-Release
cmake --build --preset x64-Release
```

The executable is `out/build/x64-Release/Release/VNA_Tool.exe`.

## Running

```
VNA_Tool                               start disconnected
VNA_Tool --sim                         use the built-in simulator
VNA_Tool --connect 192.168.0.155:5973  connect at startup
VNA_Tool --workspace layout.vnaws      load a saved layout
VNA_Tool --preset                      start with the four-plot display preset
```

## Testing

`VnaBench` is a headless check of the core library against the simulator. Exit
code 0 means everything passed.

```
VnaBench                               simulator only
VnaBench --device 192.168.0.155:5973   also runs a pass on the instrument
```

The instrument pass changes points and IF bandwidth for a few seconds and puts
them back.

`Bench/Probe.ps1` and `Bench/ProbeBlock.ps1` send raw SCPI to the instrument;
they are how `docs/DEVICE_NOTES.md` was written.

## Layout

| Folder | Contents |
|---|---|
| `Scpi/` | Transport, session (worker thread, queue), reply parsing, command catalogue |
| `Instrument/` | `VnaDevice` (connect, stimulus, sweep loop) and the binary frame codec |
| `Sim/` | `SimVna`, a command-level stand-in for the instrument |
| `Dsp/` | Formats, FFT, time domain and gating, marker searches |
| `Data/` | Trace model and processing chain, Touchstone, workspace files |
| `UI/` | Plot widget, panels, main window |
| `Bench/` | `VnaBench` and the probe scripts |
| `docs/` | `DEVICE_NOTES.md`: how the instrument actually behaves |

See `AGENTS.md` for conventions.
