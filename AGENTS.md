# Working on VNA Tool

Conventions and hard-won facts for anyone (human or agent) changing this code.
The sibling project `D:\Development\MT8870A Tool` uses the same conventions; the
SCPI layer here was written fresh for this instrument rather than copied, because
the two instruments' protocols have little in common.

## Sources of truth, in order

1. **The instrument.** `docs/DEVICE_NOTES.md` records what it actually does.
2. The vendor manuals in `Documentation/`. They are incomplete and wrong in places
   (the points limit, the meaning of `RUN?`, a third of the command set missing).
3. The simulator, which must never be more permissive than the instrument. When
   the instrument turns out to differ, change `Sim/SimVna` in the same commit and
   add a bench check.

Never invent a command. `Scpi/ScpiCommandCatalog.cpp` tags each one with where the
knowledge came from; keep that honest.

## Instrument facts that shape the code

- **A successful set command sends nothing; a failed one sends an error token.**
  `ScpiSession` therefore sends every command followed by `*OPC?` and reads up to
  its `1`. Do not remove this: a late error line would be read as the reply to the
  next query and every reply after it would be off by one.
- There is no `SYST:ERR?`. Errors are text tokens (`GATEWAY_*`, `NO_DATA`,
  `GROUP_NOT_READY`); `ScpiParse::isErrorToken` recognises them by shape.
- Values are rejected, not clamped, so every setter reads the value back and the UI
  shows what was read.
- Each command costs 30–90 ms. Count commands in anything that runs per sweep.
- Sweeps are fetched as binary sweep groups (`VNA:TRAC:GROups:DATA?`), which give
  all four S-parameters of one sweep. This works only up to 2048 points; above
  that `VnaDevice` falls back to four text queries.
- After a failed group the instrument serves the previous group's block again.
  `VnaDevice` checks the group id so stale data is never shown as a new sweep.
- Connecting must not change anything on the instrument. `*RST`, state load and
  calibration load are sent only when the operator asks.

## Architecture rules

- Nothing in `Scpi/`, `Instrument/`, `Sim/`, `Dsp/`, `Data/` or `Utils/` may include
  a header from `UI/`. `vna_core` does not link Qt Widgets, which is what keeps
  `VnaBench` headless.
- All I/O happens on the `ScpiSession` worker thread. `VnaDevice` and everything
  above it live on the main thread and never block.
- Replies are matched to what asked for them by tag, never by position.
- Processing is pure functions (`Dsp/`, `Data/TracePipeline`) so the bench can run
  them. `TraceModel` owns state; widgets only read results and call model methods.
- After changing a `Workspace` through `TraceModel::edit()`, call `changed()` for a
  setting or `notifyStructureChanged()` when a panel needs to rebuild.

## Code style

C++20, `#pragma once`, one class per header with its `.cpp` beside it. Members
`_camelCase`, classes `PascalCase`, methods `camelCase`, slots `onXxx`, constants
`kCamelCase`, `enum class` with an explicit underlying type. No project namespace;
free-function namespaces such as `ScpiParse::`. New-style `connect`. Four-space
indent. The build is warning-clean at `/W4` and stays that way.

Each class opens with a comment giving its purpose, its threading contract and the
reasons behind non-obvious decisions. Comments explain why, not what.

## Before finishing a change

```
cmake --build --preset x64-Debug
out\build\x64-Debug\Debug\VnaBench.exe
out\build\x64-Debug\Debug\VNA_Tool.exe --sim
```

For anything touching `Scpi/` or `Instrument/`, also run
`VnaBench --device <host:port>` against the instrument if it is reachable.

A window image can be captured without a person at the keyboard:

```
VNA_Tool --sim --workspace layout.vnaws --show-dock Markers --snapshot out.png
```

## Qt gotchas met so far

- Populating a combo or spin box fires its change signal. Panels guard with a
  `_refreshing` flag or `QSignalBlocker`, or use `activated` (user-only).
- A control being typed into must not be overwritten by a refresh. `EngEdit`
  handles this itself; spin boxes are skipped while they have focus.
- `resizeDocks()` only works after the window has been laid out, hence
  `QTimer::singleShot(0, ...)`.
- Every dock and toolbar needs an `objectName`, or `saveState()` cannot restore it.
- A newly linked Qt module must be added to `_wdqt_modules` in `CMakeLists.txt`, or
  windeployqt will not re-run and the exe will fail to start.
