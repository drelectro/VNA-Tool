# PNB-60 SCPI behaviour, as observed

Probed on the real instrument (reports itself as `Harogic,VNB-60,<serial>,1.6.5`,
firmware 1.8.6, `CLIENT:INFo?` → `VNB-60,<serial>,1.0.6`) with `Bench/Probe.ps1`
and `Bench/ProbeBlock.ps1`. Where this file and the vendor manual disagree, this
file is what the code follows.

## Transport

- Raw TCP to the port shown in VNStudio under System → SCPI (manual says 5025;
  this unit was set to 5973). VNStudio must be running; it is the server.
- Commands end in LF. Replies end in LF. Keywords are case-insensitive.
- Several commands may be sent in one TCP write, one per line. `;` chaining is
  not supported (`GATEWAY_UNSUPPORTED_COMMAND`).
- A round trip takes 30–90 ms regardless of the command.
- Unit suffixes are rejected (`1GHz` → `GATEWAY_INVALID_PARAMETER`); `1e7` is accepted.

## Replies and errors

- A query always replies with one line, or with a definite-length block.
- **A set command that succeeds sends nothing.** A set command that fails sends
  one line with an error token. So the app sends every set command followed by
  `*OPC?` and reads lines until `1`; anything before the `1` is the error.
- Error tokens seen: `GATEWAY_UNSUPPORTED_COMMAND`, `GATEWAY_INVALID_PARAMETER`,
  `GATEWAY_EXECUTION_ERROR`, `SERVER_NOT_READY` (manual), `NO_DATA`,
  `GROUP_NOT_READY`, `GROUP_NO_DATA`. There is no `SYST:ERR?`.
- Out-of-range values are rejected, not clamped (`POINTS 99999`, `IFBW 1`).

## Limits (queried)

100 kHz – 6.4 GHz, IFBW 15 Hz – 75 kHz, 2048 points, −40 to +5 dBm,
PLL settling 10–255 µs.

**The points limit is not what `MAXPoints?` says.** `VNA:ACQ:POINTS` accepts 2 to
10001 and rejects 1 and 10002. `MAXPoints?` still answers 2048, and that is the
limit of the sweep-group mechanism, not of the sweep:

| Points | `GROups:INITiate` | `VNA:TRAC:DATA?` |
|---|---|---|
| ≤ 2048 | works | works |
| 2049 – 10001 | `GATEWAY_EXECUTION_ERROR`; status `ERROR,…,CONFIG_CHANGED` | works |

So the app uses groups up to 2048 points and falls back to four text queries
above that.

## Commands not in the manual (from `*LST?`)

| Command | Observed |
|---|---|
| `VNA:CALibration:LOAD? <file>` | `FALSE` for a file that does not exist. Not tried with a real file. |
| `VNA:ACQ:SWEep:SPEed?` | Points per second (2164 at 401 points, 10 kHz IFBW). |
| `VNA:ACQ:SWEep:VALID?` | `TRUE` normally, `FALSE` right after `SINGLE TRUE`. |
| `VNA:ACQ:GROups:COUNt n` / `INITiate` / `STATus?` / `ABORt` | Captures the next `n` complete sweeps. `STATus?` → `state,groupId,done,total,reason`. Works with acquisition running **or stopped** (it sweeps on demand). `ABORt` when idle → `GATEWAY_INVALID_PARAMETER`. See "Sweep groups" below. |
| `VNA:ACQ:GROups:PERiod`, `CONTinuous` | Query only so far: `0,0` and `OFF`. |
| `VNA:TRAC:GROups:DATA?` | Binary block, format below. `GROUP_NOT_READY` if no group is complete. |
| `VNA:ACQ:RAWRx TRUE|FALSE`, `RAWRx:FRAME?` | Binary block with raw receiver waves, format below. `NO_DATA` when acquisition is stopped. |
| `VNA:TRAC:PAUSE <Sxx>` / `RESUME <Sxx>` / `PAUSED? <Sxx>` | Accepted, but `PAUSED?` stayed `FALSE`. Not used. |
| `VNA:ACQ:PHASeflip:ENABle`, `PHASeflip:STATus?` | Experimental feature; status is a JSON object. Not used. |
| `DEVice:CONNect?` | Serial of the connected RF unit. |

`VNA:ACQ:RUN?` means "acquisition enabled", not "a sweep is in progress": it
stays `TRUE` after `SINGLE TRUE` and only `STOP` makes it `FALSE`.

## Sweep groups

States seen in `GROups:STATus?`: `IDLE`, `ARMED`, `COLLECTING`, `DONE`,
`ABORTED`, `ERROR`. Reasons seen: `NONE`, `ABORTED`, `CONFIG_CHANGED`.

- With the tablet sweeping, `INITiate` goes to `ARMED` until the sweep in
  progress finishes, then `COLLECTING` for the next one. Expect one to two
  sweep times per group. With the tablet's acquisition stopped the group
  sweeps at once, so remote updates are faster with the tablet on Stop.
- While `ARMED` or `COLLECTING`, `VNA:TRAC:GROups:DATA?` answers
  `GROUP_NOT_READY`. Queries are still answered in the usual 30–90 ms.
- Changing a setting during a group aborts it (`ABORTED`). Routine; start
  another. `GROups:DATA?` then answers `GROUP_NO_DATA`, not `GROUP_NOT_READY`.
- **After a group that ended in `ERROR`, `GROups:DATA?` serves the previous
  group's block again**, not an error. The group id in the block header is the
  only way to tell, so the app compares it with the last one it used.
- A sweep loop of `INITiate` + `GROups:DATA?` runs at about 2.7 sweeps per
  second for 401 points at 10 kHz IF bandwidth (185 ms sweep) with the tablet
  sweeping.

## ASCII trace data

`VNA:TRAC:DATA? S11|S21|S12|S22` → `[x,re,im],[x,re,im],…`. Any other argument,
or none, → `GATEWAY_INVALID_PARAMETER`. Values are identical to the group block
for the same sweep.

## Binary blocks

All multi-byte fields are **big-endian**. Each block ends with a 4-byte trailer
that looks like a checksum; its algorithm is not identified and it is not verified.

### `VNA:TRAC:GROups:DATA?` — `VGB1`

```
0   char[4]  "VGB1"
4   u16      version (1)
6   u16      header size (20)
8   u64      group id
16  u32      frame count
then per frame:
    u64      timestamp (unit unknown)
    u32      frame length
    ...      VTF1 frame
last u32     trailer
```

### `VTF1` frame

```
0    char[4]  "VTF1"
4    u16      version (1)
6    u16      header size (136)
8    u32      277 in every capture (meaning unknown)
12   u32      0x01000000 (unknown)
16   u8[16]   id, constant across captures
32   u64 ×6   counters; the 5th increments once per sweep
80   u32      points N
84   u32      S-parameter mask (0x0F seen: bit0 S11, bit1 S21, bit2 S12, bit3 S22)
88   u32 ×2   1, 1 (unknown)
96   f64      start
104  f64      stop
112  f64      IF bandwidth
120  f64      (equals stop in every capture)
128  u64      changes per sweep (unknown)
136  f64 ×N   stimulus values
then for each set mask bit, in order S11, S21, S12, S22:
     f32 ×N   real parts
     f32 ×N   imaginary parts
```

The S-parameter order was confirmed by stopping acquisition, running a one-sweep
group, and matching the floats against the four `VNA:TRAC:DATA?` replies.

### `VNA:ACQ:RAWRx:FRAME?` — `VRF1`

```
0   char[4]  "VRF1"
4   u32      VTF1 frame length
8   ...      VTF1 frame (as above)
    u32      wave mask (0xFF seen)
    f32 ×N re, f32 ×N im for each of 8 waves
    u32      trailer
```

Which wave is which has not been worked out.

## Still to confirm

- Whether returned data follows the tablet's Correction switch. An open port
  read S11 ≈ 1∠0° across the band, which suggests corrected data.
- What the mask and frame look like when the tablet displays fewer than four
  S-parameters, and in POWER and CW sweep modes (the app has only been run
  against the instrument in linear frequency sweep).
- `VNA:CALibration:LOAD?` and `CLIENT:SETUP:LOAD?` with a real file.
- Behaviour with a second client connected.
