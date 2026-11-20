# Probe.ps1 - send SCPI lines to the PNB-60 (VNStudio SCPI server) and print
# each reply with its round-trip time.
#
#   .\Probe.ps1 -Address 192.168.0.155 -Port 5973 -Commands '*IDN?','VNA:ACQ:POINTS?'
#
# Special pseudo-commands:
#   !sleep <ms>     wait
#   !raw <text>     send <text> with "\n" escapes expanded and NO terminator added
#                   (for testing several commands in one TCP write)
#
# A line that gets no reply within -TimeoutMs prints <no reply>; that is how
# "set commands are silent" is observed, so it is not an error.
param(
    [string]$Address = '192.168.0.155',
    [int]$Port = 5973,
    [int]$TimeoutMs = 1500,
    [int]$MaxShow = 300,
    [Parameter(Mandatory = $true)][string[]]$Commands
)

$client = New-Object System.Net.Sockets.TcpClient
$client.NoDelay = $true
$client.Connect($Address, $Port)
$stream = $client.GetStream()

function Read-Reply([int]$timeoutMs, [int]$expectLines = 1) {
    $sb = New-Object System.Text.StringBuilder
    $buf = New-Object byte[] 262144
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $lines = 0
    while ($sw.ElapsedMilliseconds -lt $timeoutMs) {
        if ($stream.DataAvailable) {
            $n = $stream.Read($buf, 0, $buf.Length)
            $text = [Text.Encoding]::ASCII.GetString($buf, 0, $n)
            [void]$sb.Append($text)
            $lines = ($sb.ToString().ToCharArray() | Where-Object { $_ -eq "`n" }).Count
            if ($lines -ge $expectLines) { break }
        } else {
            Start-Sleep -Milliseconds 2
        }
    }
    return $sb.ToString()
}

function Show([string]$label, [string]$reply, [long]$ms) {
    if ($reply.Length -eq 0) { $shown = '<no reply>' }
    else {
        $shown = $reply.Replace("`r", '\r').Replace("`n", '\n')
        if ($shown.Length -gt $MaxShow) {
            $head = [int]($MaxShow * 0.7)
            $shown = $shown.Substring(0, $head) + " ...[$($reply.Length) chars]... " + $shown.Substring($shown.Length - 60)
        }
    }
    '{0,-44} [{1,5} ms] {2}' -f $label, $ms, $shown
}

try {
    foreach ($cmd in $Commands) {
        if ($cmd -match '^!sleep\s+(\d+)') {
            Start-Sleep -Milliseconds ([int]$Matches[1])
            continue
        }
        $sw = [Diagnostics.Stopwatch]::StartNew()
        if ($cmd -match '^!raw\s+(.*)$') {
            $payload = $Matches[1].Replace('\n', "`n")
            $expect = ($payload.ToCharArray() | Where-Object { $_ -eq "`n" }).Count
            $bytes = [Text.Encoding]::ASCII.GetBytes($payload)
            $stream.Write($bytes, 0, $bytes.Length)
            $reply = Read-Reply $TimeoutMs $expect
        } else {
            $bytes = [Text.Encoding]::ASCII.GetBytes($cmd + "`n")
            $stream.Write($bytes, 0, $bytes.Length)
            $reply = Read-Reply $TimeoutMs 1
        }
        Show $cmd $reply $sw.ElapsedMilliseconds
    }
}
finally {
    $client.Close()
}
