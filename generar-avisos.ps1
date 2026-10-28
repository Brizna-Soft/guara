# Arma AVISOS-DE-TERCEROS.txt con la licencia de cada pieza de software que va dentro de Guara.exe.
# Los textos salen de lo que instaló vcpkg (share\<paquete>\copyright), así que siempre corresponden a
# las versiones compiladas. Lo llama compilar.ps1 después de compilar.
#
# Uso: powershell -ExecutionPolicy Bypass -File generar-avisos.ps1 -Salida AVISOS-DE-TERCEROS.txt [-Build build]
param(
    [string]$Build,
    [Parameter(Mandatory = $true)][string]$Salida
)

$ErrorActionPreference = 'Stop'
if (-not $Build) { $Build = Join-Path $PSScriptRoot 'build' }
$share = Join-Path $Build 'vcpkg_installed\x64-windows-static-rel\share'
if (-not (Test-Path $share)) { throw "No encuentro $share (primero hay que compilar)" }

function Leer([string]$ruta) {
    $t = [IO.File]::ReadAllText($ruta, [Text.Encoding]::UTF8)
    return ($t -replace "`r`n", "`n" -replace "`r", "`n").Trim("`n", ' ')
}

# Versión que instaló vcpkg, sin el número de revisión del port ("3.3.3#1" -> "3.3.3")
function Version-Vcpkg([string]$port) {
    $f = Join-Path $share "$port\vcpkg.spdx.json"
    if (-not (Test-Path $f)) { return '' }
    $j = Get-Content -Raw -Encoding UTF8 $f | ConvertFrom-Json
    $p = $j.packages | Where-Object { $_.SPDXID -eq 'SPDXRef-port' } | Select-Object -First 1
    if (-not $p) { return '' }
    return ($p.versionInfo -replace '#\d+$', '')
}

# wxWidgets primero (es la biblioteca principal); después, el resto en orden alfabético
$paquetes = @(Get-ChildItem $share -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'copyright') } |
    Sort-Object @{ Expression = { if ($_.Name -eq 'wxwidgets') { 0 } else { 1 } } }, Name)
if (-not ($paquetes | Where-Object Name -eq 'wxwidgets')) { throw 'Falta la licencia de wxWidgets (share\wxwidgets\copyright)' }

$sb = New-Object System.Text.StringBuilder
$titulo = 'Guara - Avisos de terceros (licencias)'
[void]$sb.AppendLine($titulo)
[void]$sb.AppendLine('=' * $titulo.Length)
[void]$sb.AppendLine()
[void]$sb.AppendLine('Guara es de Brizna Soft y usa la licencia MIT (archivo LICENSE).')
[void]$sb.AppendLine('Dentro de Guara.exe va el software de terceros que se lista abajo, enlazado de forma estática.')
[void]$sb.AppendLine('Cada pieza conserva su propia licencia, cuyo texto se reproduce sin cambios (en inglés, que es el')
[void]$sb.AppendLine('texto original y el que vale). wxWidgets se usa sin modificaciones, según la excepción de su licencia.')
[void]$sb.AppendLine()
[void]$sb.AppendLine('Contenido')
[void]$sb.AppendLine('---------')
$n = 0
foreach ($p in $paquetes) {
    $n++
    $v = Version-Vcpkg $p.Name
    [void]$sb.AppendLine(('{0,2}. {1}{2}' -f $n, $p.Name, $(if ($v) { " $v" } else { '' })))
}
$n = 0
foreach ($p in $paquetes) {
    $n++
    $v = Version-Vcpkg $p.Name
    [void]$sb.AppendLine()
    [void]$sb.AppendLine()
    [void]$sb.AppendLine('=' * 78)
    [void]$sb.AppendLine(('{0}. {1}{2}' -f $n, $p.Name, $(if ($v) { " $v" } else { '' })))
    [void]$sb.AppendLine('=' * 78)
    [void]$sb.AppendLine()
    [void]$sb.AppendLine((Leer (Join-Path $p.FullName 'copyright')))
}

$texto = $sb.ToString() -replace "`r?`n", "`r`n"
$carpeta = Split-Path -Parent ([IO.Path]::GetFullPath($Salida))
New-Item -ItemType Directory -Force $carpeta | Out-Null
[IO.File]::WriteAllText([IO.Path]::GetFullPath($Salida), $texto, (New-Object System.Text.UTF8Encoding $true))
Write-Host ("Avisos de terceros: {0} piezas -> {1}" -f $paquetes.Count, $Salida)
