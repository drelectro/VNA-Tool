# make_icon.ps1 - draws the application icon and writes vna.ico and vna.png
# next to this script. Run it again after changing the drawing or the colours
# (which mirror UI/Theme.h and the first trace colour in TraceModel).
#
#   powershell -File Resources\make_icon.ps1
#
# The icon is a Smith chart with a trace on it. Each size is drawn on its own
# rather than scaled down from the largest, so line weights stay legible at
# 16 pixels, where the fine grid is left out altogether.

Add-Type -AssemblyName System.Drawing

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$sizes = 16, 24, 32, 48, 64, 128, 256

function New-IconBitmap([int]$n) {
    $bmp = New-Object System.Drawing.Bitmap $n, $n, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    $s = $n / 256.0

    # Rounded-square background.
    $radius = 44 * $s
    $rect = New-Object System.Drawing.RectangleF (4 * $s), (4 * $s), (248 * $s), (248 * $s)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $radius
    $path.AddArc($rect.X, $rect.Y, $d, $d, 180, 90)
    $path.AddArc($rect.Right - $d, $rect.Y, $d, $d, 270, 90)
    $path.AddArc($rect.Right - $d, $rect.Bottom - $d, $d, $d, 0, 90)
    $path.AddArc($rect.X, $rect.Bottom - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $bg = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0x14, 0x14, 0x1c))
    $g.FillPath($bg, $path)
    $border = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 0x3c, 0x3c, 0x58)), ([Math]::Max(1.0, 6 * $s))
    $g.DrawPath($border, $path)

    # Smith chart: unit circle centred in the square.
    $cx = 128 * $s; $cy = 128 * $s; $r = 92 * $s
    $gridColour = [System.Drawing.Color]::FromArgb(255, 0x82, 0x90, 0xc0)
    $outer = New-Object System.Drawing.Pen $gridColour, ([Math]::Max(1.0, 9 * $s))
    $fine = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(190, 0x82, 0x90, 0xc0)), ([Math]::Max(1.0, 5 * $s))

    if ($n -ge 32) {
        $clip = New-Object System.Drawing.Drawing2D.GraphicsPath
        $clip.AddEllipse($cx - $r, $cy - $r, 2 * $r, 2 * $r)
        $g.SetClip($clip)
        # Real axis, the r = 1 circle, and the x = +/-1 arcs.
        $g.DrawLine($fine, $cx - $r, $cy, $cx + $r, $cy)
        $g.DrawEllipse($fine, $cx, $cy - $r / 2, $r, $r)
        $g.DrawEllipse($fine, $cx, $cy - 2 * $r, 2 * $r, 2 * $r)
        $g.DrawEllipse($fine, $cx, $cy, 2 * $r, 2 * $r)
        $g.ResetClip()
    }
    $g.DrawEllipse($outer, $cx - $r, $cy - $r, 2 * $r, 2 * $r)

    # The trace: a resonance loop, the shape a filter or antenna draws.
    $points = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    for ($i = 0; $i -le 60; $i++) {
        $t = $i / 60.0
        $angle = (200 - 330 * $t) * [Math]::PI / 180.0
        $rho = 0.78 - 0.50 * [Math]::Sin([Math]::PI * $t)
        $x = $cx + $r * (0.10 + $rho * [Math]::Cos($angle))
        $y = $cy - $r * ($rho * [Math]::Sin($angle))
        $points.Add((New-Object System.Drawing.PointF $x, $y))
    }
    $trace = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 0xf2, 0xd0, 0x4a)), ([Math]::Max(1.6, 13 * $s))
    $trace.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $trace.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $trace.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $g.DrawLines($trace, $points.ToArray())

    # A marker at the point nearest the centre (best match).
    if ($n -ge 24) {
        $m = $points[30]
        $mr = [Math]::Max(2.0, 13 * $s)
        $dot = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0x4a, 0xc8, 0xf2))
        $g.FillEllipse($dot, $m.X - $mr, $m.Y - $mr, 2 * $mr, 2 * $mr)
    }

    $g.Dispose()
    return $bmp
}

# One PNG per size, held in memory.
$images = @()
foreach ($n in $sizes) {
    $bmp = New-IconBitmap $n
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $images += ,@($n, $ms.ToArray())
    if ($n -eq 256) { $bmp.Save((Join-Path $here 'vna.png'), [System.Drawing.Imaging.ImageFormat]::Png) }
    $bmp.Dispose()
}

# ICO container: 6-byte header, a 16-byte directory entry per image, then the
# PNG data. PNG-compressed entries are valid for every size since Vista.
$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $out
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($img in $images) {
    $n = $img[0]; $data = $img[1]
    $dim = if ($n -ge 256) { 0 } else { $n }
    $w.Write([byte]$dim); $w.Write([byte]$dim)
    $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$data.Length); $w.Write([uint32]$offset)
    $offset += $data.Length
}
foreach ($img in $images) { $w.Write([byte[]]$img[1]) }
$w.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $here 'vna.ico'), $out.ToArray())
"Wrote vna.ico ($($out.Length) bytes, sizes $($sizes -join ', ')) and vna.png"
