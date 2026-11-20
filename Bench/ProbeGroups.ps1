# ProbeGroups.ps1 - find out what VNA:ACQ:GROups:CONTinuous does.
#
#   .\ProbeGroups.ps1 -Address 192.168.0.155 -Port 5973 -Seconds 50
#
# WHY: with the tablet sweeping, a one-shot group (GROups:INITiate) waits for
# the sweep in progress to end and then records the NEXT one, so the app sees
# every second sweep. If continuous groups hand over every sweep, the app can
# use them instead.
#
# WHAT IT CHANGES: only how sweeps are captured. It turns GROups:CONTinuous on,
# starts a group, watches for the given time, and turns it back off - also if
# interrupted. Frequency, points, IF bandwidth and power are not touched.
# Close VNA Tool first: two clients starting groups would confuse the result.
#
# WHAT TO LOOK FOR in the output:
#   - does the status go DONE -> ARMED/COLLECTING -> DONE by itself, with the
#     group id counting up, once per sweep time?
#   - does GROups:DATA? return a new group id each sweep, and how many frames?
param(
    [string]$Address = '192.168.0.155',
    [int]$Port = 5973,
    [int]$Seconds = 50
)

$client = New-Object System.Net.Sockets.TcpClient
$client.NoDelay = $true
$client.ReceiveTimeout = 30000
$client.Connect($Address, $Port)
$stream = $client.GetStream()

function Send([string]$text) {
    $b = [Text.Encoding]::ASCII.GetBytes($text + "`n")
    $stream.Write($b, 0, $b.Length)
}
function Read-Exact([int]$count) {
    $buf = New-Object byte[] $count
    $got = 0
    while ($got -lt $count) {
        $n = $stream.Read($buf, $got, $count - $got)
        if ($n -le 0) { throw 'connection closed by the instrument' }
        $got += $n
    }
    return ,$buf
}
function Read-Line {
    $ms = New-Object System.IO.MemoryStream
    while ($true) {
        $v = $stream.ReadByte()
        if ($v -lt 0) { throw 'connection closed by the instrument' }
        if ($v -eq 10) { break }
        $ms.WriteByte([byte]$v)
    }
    return [Text.Encoding]::ASCII.GetString($ms.ToArray()).TrimEnd("`r")
}
function Query([string]$text) { Send $text; Read-Line }
function Command([string]$text) {
    Send $text
    Send '*OPC?'
    $notes = @()
    while ($true) {
        $r = Read-Line
        if ($r -eq '1') { break }
        $notes += $r
    }
    if ($notes.Count) { $notes -join ' | ' } else { 'ok' }
}
# Sends GROups:DATA? and describes the reply without printing the data.
function Describe-Data {
    Send 'VNA:TRAC:GRO:DATA?'
    $first = $stream.ReadByte()
    if ($first -ne 35) { return ([char]$first) + (Read-Line) }
    $digits = $stream.ReadByte() - 48
    $len = [int][Text.Encoding]::ASCII.GetString((Read-Exact $digits))
    $data = Read-Exact $len
    [void]$stream.ReadByte()
    # Big-endian: group id at 8 (u64), frame count at 16 (u32).
    $id = 0L; for ($i = 8; $i -lt 16; $i++) { $id = ($id -shl 8) -bor $data[$i] }
    $frames = 0; for ($i = 16; $i -lt 20; $i++) { $frames = ($frames -shl 8) -bor $data[$i] }
    return "block $len bytes, group id $id, $frames frame(s)"
}

$sw = [Diagnostics.Stopwatch]::StartNew()
function Stamp([string]$what, [string]$result) {
    '{0,6:n1} s  {1,-22} {2}' -f ($sw.ElapsedMilliseconds / 1000), $what, $result
}

try {
    Stamp 'SWE:TIME?' (Query 'VNA:ACQ:SWE:TIME?')
    Stamp 'RUN?' (Query 'VNA:ACQ:RUN?')
    Stamp 'GRO:CONT? (before)' (Query 'VNA:ACQ:GRO:CONT?')
    Stamp 'GRO:PER?' (Query 'VNA:ACQ:GRO:PER?')
    Stamp 'GRO:COUN 1' (Command 'VNA:ACQ:GRO:COUN 1')
    Stamp 'GRO:CONT ON' (Command 'VNA:ACQ:GRO:CONT ON')
    Stamp 'GRO:CONT?' (Query 'VNA:ACQ:GRO:CONT?')
    Stamp 'GRO:INIT' (Command 'VNA:ACQ:GRO:INIT')

    $lastStatus = ''
    while ($sw.ElapsedMilliseconds -lt $Seconds * 1000) {
        $status = Query 'VNA:ACQ:GRO:STAT?'
        if ($status -ne $lastStatus) {
            Stamp 'GRO:STAT?' $status
            $lastStatus = $status
            # Look at the data whenever the status changes.
            Stamp 'GRO:DATA?' (Describe-Data)
        }
        Start-Sleep -Milliseconds 500
    }
}
finally {
    try {
        Stamp 'GRO:CONT OFF' (Command 'VNA:ACQ:GRO:CONT OFF')
        Stamp 'GRO:CONT? (after)' (Query 'VNA:ACQ:GRO:CONT?')
        Stamp 'GRO:STAT? (after)' (Query 'VNA:ACQ:GRO:STAT?')
    } catch {
        "Could not switch continuous groups back off: $_"
        'Send VNA:ACQ:GRO:CONT OFF from the SCPI console to restore it.'
    }
    $client.Close()
}
