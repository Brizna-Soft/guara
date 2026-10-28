# Arma el paquete para publicar, después de compilar.ps1 (para una versión pública, compilar.ps1 -Publicacion):
#
#   publicar\Guara-<versión>-x64.zip          Guara.exe, LICENSE, AVISOS-DE-TERCEROS.txt y LEEME.txt
#   publicar\Guara-<versión>-x64.zip.sha256   su huella SHA256 (formato de sha256sum)
#
# Uso: powershell -ExecutionPolicy Bypass -File empaquetar.ps1 [-Salida <carpeta>]
param([string]$Salida)

$ErrorActionPreference = 'Stop'
$codigo = $PSScriptRoot
if (-not $Salida) { $Salida = Join-Path $codigo 'publicar' }
$version = (Get-Content (Join-Path $codigo 'version.txt') -TotalCount 1).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "version.txt debe tener el formato 1.2.3 (tiene '$version')" }

$exe = Join-Path $codigo 'programa\Guara.exe'
$archivos = @(
    $exe,
    (Join-Path $codigo 'LICENSE'),
    (Join-Path $codigo 'AVISOS-DE-TERCEROS.txt'),
    (Join-Path $codigo 'paquete\LEEME.txt')
)
foreach ($f in $archivos) { if (-not (Test-Path $f)) { throw "Falta el archivo $f (primero hay que compilar)" } }

$versionExe = (Get-Item $exe).VersionInfo.ProductVersion
if ($versionExe -ne $version) {
    Write-Warning "Este Guara.exe es de prueba (versión $versionExe). Para publicar, compila con: compilar.ps1 -Publicacion"
}

New-Item -ItemType Directory -Force $Salida | Out-Null
$nombre = "Guara-$version-x64.zip"
$zip = Join-Path ([IO.Path]::GetFullPath($Salida)) $nombre
if (Test-Path $zip) { Remove-Item $zip -Force }

Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archivoZip = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($f in $archivos) {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archivoZip, $f, (Split-Path $f -Leaf),
            [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally {
    $archivoZip.Dispose()
}

$huella = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
[IO.File]::WriteAllText("$zip.sha256", "$huella  $nombre`n", (New-Object System.Text.UTF8Encoding $false))
Write-Host "Paquete: $zip"
Write-Host "SHA256:  $huella"
