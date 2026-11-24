<#
  Descarga el ASIO SDK de Steinberg y lo deja en DAW/ThirdParty/asiosdk (no se sube al repositorio).
  CMake lo detecta solo y compila Worship DAW con ASIO.

  El SDK tiene licencia doble de Steinberg (acuerdo propietario o GPLv3). Para una app de código cerrado
  corresponde el acuerdo propietario: léelo y acéptalo en https://www.steinberg.net/developers/
  antes de distribuir la app. Este script no acepta nada por ti: hay que pasarle -AcceptLicense.

  Uso (PowerShell, desde la carpeta DAW):
      .\scripts\get-asio-sdk.ps1 -AcceptLicense
      cmake -B build -G "Visual Studio 17 2022" -A x64
#>
param(
    [switch] $AcceptLicense,
    [string] $Url = "https://www.steinberg.net/asiosdk"
)

$ErrorActionPreference = "Stop"
if (-not $AcceptLicense) {
    Write-Host "El ASIO SDK es de Steinberg y tiene su propia licencia."
    Write-Host "Revisala en https://www.steinberg.net/developers/ y vuelve a ejecutar con -AcceptLicense."
    exit 1
}

$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "ThirdParty\asiosdk"
if (Test-Path (Join-Path $dest "common\asio.h")) {
    Write-Host "El ASIO SDK ya esta en $dest"
    exit 0
}

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("asiosdk-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $zip = Join-Path $tmp "asiosdk.zip"
    Write-Host "Descargando el ASIO SDK desde $Url ..."
    Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing
    Expand-Archive -Path $zip -DestinationPath $tmp -Force
    $header = Get-ChildItem -Path $tmp -Recurse -Filter "asio.h" | Where-Object { $_.Directory.Name -eq "common" } | Select-Object -First 1
    if (-not $header) { throw "El archivo descargado no contiene common\asio.h. Descarga el SDK a mano y pasa -DWDAW_ASIO_SDK=<carpeta> a CMake." }
    $sdk = $header.Directory.Parent.FullName
    New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force | Out-Null
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Copy-Item -Recurse -Path $sdk -Destination $dest
    Write-Host "Listo: ASIO SDK en $dest"
}
finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
