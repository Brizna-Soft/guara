# Dibuja el ícono de Guara (res\app.ico): la cabeza de una guara roja, de perfil, sobre un cuadrado
# redondeado azul oscuro, con plumas amarillas y azules abajo. Lo guarda en varios tamaños, cada uno como
# imagen PNG dentro del .ico.
#
# Uso: powershell -ExecutionPolicy Bypass -File res\crear-icono.ps1 [-Vista <carpeta para las vistas previas>]
param([string]$Vista)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$salida = Join-Path $PSScriptRoot 'app.ico'
$tamanos = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256

function Color([string]$hex) { [System.Drawing.ColorTranslator]::FromHtml($hex) }
function Brocha([string]$hex) { New-Object System.Drawing.SolidBrush (Color $hex) }

$fondoColor = '#163A5F'   # azul oscuro
$rojo = '#D7263D'         # la guara
$amarillo = '#F6C431'
$azul = '#2E86DE'
$cara = '#FFFFFF'         # la parte blanca alrededor del ojo
$pico = '#EFE3C4'         # pico de arriba, color hueso
$picoBajo = '#2B2B2B'     # pico de abajo, oscuro
$ojo = '#141414'

function New-Redondeado([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

# El dibujo se piensa en un cuadro de 120 x 120 y se escala al tamaño pedido
function Draw-Guara($g, [single]$x0, [single]$y0, [single]$lado, [int]$n) {
    $k = $lado / 120.0
    function Q([single]$x, [single]$y) { New-Object System.Drawing.PointF(($x0 + $x * $k), ($y0 + $y * $k)) }
    function Poligono([string]$hex, [object[]]$puntos) {
        $g.FillPolygon((Brocha $hex), [System.Drawing.PointF[]]($puntos | ForEach-Object { Q $_[0] $_[1] }))
    }

    $cuadro = New-Redondeado $x0 $y0 $lado $lado (26 * $k)
    $g.FillPath((Brocha $fondoColor), $cuadro)
    $g.SetClip($cuadro)

    # Plumas del ala: azul abajo y amarillo encima, en diagonal
    Poligono $azul @(@(-5, 95), @(74, 103), @(70, 125), @(-5, 125))
    Poligono $amarillo @(@(-5, 80), @(78, 89), @(74, 103), @(-5, 95))

    # Cuello y cabeza, en rojo
    Poligono $rojo @(@(20, 50), @(82, 50), @(78, 89), @(-5, 80), @(-5, 60))
    $g.FillEllipse((Brocha $rojo), ($x0 + 18 * $k), ($y0 + 18 * $k), (64 * $k), (64 * $k))

    # Pico de abajo (oscuro) y pico de arriba (curvo, color hueso, con la punta hacia abajo)
    Poligono $picoBajo @(@(74, 60), @(93, 64), @(92, 80), @(77, 77))
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $p.AddBezier((Q 68 30), (Q 92 24), (Q 114 42), (Q 103 86))
    $p.AddBezier((Q 103 86), (Q 98 74), (Q 90 64), (Q 74 63))
    $p.CloseFigure()
    $g.FillPath((Brocha $pico), $p)
    if ($n -ge 32) {
        $lapiz = New-Object System.Drawing.Pen((Color '#B8A57A'), [single][Math]::Max(1.0, 1.6 * $k))
        $g.DrawPath($lapiz, $p)
    }

    # Cara blanca y ojo
    $g.FillEllipse((Brocha $cara), ($x0 + 50 * $k), ($y0 + 34 * $k), (28 * $k), (22 * $k))
    $radio = [Math]::Max(5 * $k, 0.9)
    $g.FillEllipse((Brocha $ojo), ($x0 + 62 * $k - $radio), ($y0 + 45 * $k - $radio), (2 * $radio), (2 * $radio))
    if ($n -ge 48) {
        $brillo = 1.6 * $k
        $g.FillEllipse((Brocha '#FFFFFF'), ($x0 + 63.8 * $k - $brillo), ($y0 + 43.2 * $k - $brillo), (2 * $brillo), (2 * $brillo))
    }
    $g.ResetClip()
}

function New-Imagen([int]$n) {
    $bmp = New-Object System.Drawing.Bitmap($n, $n, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $margen = [Math]::Max(0.5, $n * 0.03)
    Draw-Guara $g $margen $margen ($n - 2 * $margen) $n
    $g.Dispose()
    return $bmp
}

if ($Vista) { New-Item -ItemType Directory -Force $Vista | Out-Null }
$pngs = @()
foreach ($n in $tamanos) {
    $bmp = New-Imagen $n
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngs += , @($n, $ms.ToArray())
    if ($Vista -and $n -in 16, 24, 32, 48, 256) { $bmp.Save((Join-Path $Vista ("guara-{0}.png" -f $n)), [System.Drawing.Imaging.ImageFormat]::Png) }
    $bmp.Dispose(); $ms.Dispose()
}

# Archivo .ico: cabecera, una entrada por tamaño y después las imágenes PNG
$fs = [System.IO.File]::Create($salida)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$pngs.Count)
$offset = 6 + 16 * $pngs.Count
foreach ($p in $pngs) {
    $n = $p[0]; $datos = $p[1]
    $bw.Write([byte]($(if ($n -ge 256) { 0 } else { $n })))
    $bw.Write([byte]($(if ($n -ge 256) { 0 } else { $n })))
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$datos.Length); $bw.Write([uint32]$offset)
    $offset += $datos.Length
}
foreach ($p in $pngs) { $bw.Write([byte[]]$p[1]) }
$bw.Close(); $fs.Close()
"{0}: {1} tamaños, {2:N0} bytes" -f (Split-Path $salida -Leaf), $pngs.Count, (Get-Item $salida).Length

# Hoja de vista previa: 256, 48, 32, 24 y 16 sobre fondo claro y oscuro
if ($Vista) {
    $hoja = New-Object System.Drawing.Bitmap(720, 560)
    $gv = [System.Drawing.Graphics]::FromImage($hoja)
    $gv.Clear((Color '#F5F5F5'))
    $gv.FillRectangle((Brocha '#202020'), 0, 280, 720, 280)
    foreach ($fy in 10, 290) {
        $xx = 10
        foreach ($n in 256, 48, 32, 24, 16) {
            $img = [System.Drawing.Image]::FromFile((Join-Path $Vista ("guara-{0}.png" -f $n)))
            $gv.DrawImage($img, $xx, $fy, $n, $n)
            # también ampliado x4 (sin suavizar) para ver los píxeles de los tamaños chicos
            if ($n -le 32) {
                $gv.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
                $gv.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
                $gv.DrawImage($img, $xx, $fy + 60, $n * 4, $n * 4)
            }
            $img.Dispose()
            $xx += $(if ($n -eq 256) { 266 } elseif ($n -le 32) { [Math]::Max($n, $n * 4) + 12 } else { $n + 12 })
        }
    }
    $gv.Dispose()
    $hoja.Save((Join-Path $Vista 'vista-previa.png'), [System.Drawing.Imaging.ImageFormat]::Png)
    $hoja.Dispose()
    "Vista previa: " + (Join-Path $Vista 'vista-previa.png')
}
