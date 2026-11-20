# ProbeBlock.ps1 - run some setup lines, then one query that returns an
# IEEE 488.2 definite-length block (#<n><len><data>), and save the payload.
#
#   .\ProbeBlock.ps1 -Setup 'VNA:ACQ:GRO:COUN 1','VNA:ACQ:GRO:INIT','!sleep 800' `
#                    -Query 'VNA:TRAC:GRO:DATA?' -OutFile group.bin
#
# Setup lines are sent followed by *OPC? so both silent successes and error
# tokens are consumed and the stream stays aligned. Lines ending in '?' are
# sent as plain queries and their reply printed.
param(
    [string]$Address = '192.168.0.155',
    [int]$Port = 5973,
    [string[]]$Setup = @(),
    [Parameter(Mandatory = $true)][string]$Query,
    [Parameter(Mandatory = $true)][string]$OutFile,
    [string[]]$Cleanup = @()
)

$client = New-Object System.Net.Sockets.TcpClient
$client.NoDelay = $true
$client.ReceiveTimeout = 5000
$client.Connect($Address, $Port)
$stream = $client.GetStream()

function Send([string]$text) {
    $b = [Text.Encoding]::ASCII.GetBytes($text + "`n")
    $stream.Write($b, 0, $b.Length)
}
function Read-Line {
    $ms = New-Object System.IO.MemoryStream
    while ($true) {
        $v = $stream.ReadByte()
        # -1 is the server closing the connection. Throw, or the callers'
        # read-until-"1" loops would spin for ever on empty lines.
        if ($v -lt 0) { throw 'connection closed by the instrument' }
        if ($v -eq 10) { break }
        $ms.WriteByte([byte]$v)
    }
    return [Text.Encoding]::ASCII.GetString($ms.ToArray()).TrimEnd("`r")
}
function Read-Exact([int]$count) {
    $buf = New-Object byte[] $count
    $got = 0
    while ($got -lt $count) {
        $n = $stream.Read($buf, $got, $count - $got)
        if ($n -le 0) { throw "connection closed after $got of $count bytes" }
        $got += $n
    }
    return ,$buf
}
function Run([string]$line) {
    if ($line -match '^!sleep\s+(\d+)') { Start-Sleep -Milliseconds ([int]$Matches[1]); return }
    if ($line.EndsWith('?')) {
        Send $line
        '{0,-36} {1}' -f $line, (Read-Line)
        return
    }
    Send $line
    Send '*OPC?'
    $notes = @()
    while ($true) {
        $r = Read-Line
        if ($r -eq '1') { break }
        $notes += $r
    }
    if ($notes.Count -eq 0) { $notes = @('ok') }
    '{0,-36} {1}' -f $line, ($notes -join ' | ')
}

try {
    foreach ($s in $Setup) { Run $s }

    Send $Query
    $first = $stream.ReadByte()
    if ($first -ne 35) {
        # Not a block: print the text reply instead.
        $rest = Read-Line
        '{0,-36} {1}{2}' -f $Query, [char]$first, $rest
    } else {
        $nDigits = $stream.ReadByte() - 48
        $lenText = [Text.Encoding]::ASCII.GetString((Read-Exact $nDigits))
        $len = [int]$lenText
        $data = Read-Exact $len
        [IO.File]::WriteAllBytes($OutFile, $data)
        $tail = $stream.ReadByte()
        '{0,-36} block of {1} bytes -> {2} (trailing byte {3})' -f $Query, $len, $OutFile, $tail
    }

    foreach ($s in $Cleanup) { Run $s }
}
finally {
    $client.Close()
}
