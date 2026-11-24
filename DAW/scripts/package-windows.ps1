<#
  Firma y empaqueta Worship DAW para Windows: instalador .exe (Inno Setup), firmado.

    .\scripts\package-windows.ps1 [-BuildDir build] [-Version 0.2.0]

  Firma (opcional; sin firma, Windows SmartScreen avisa "editor desconocido"):
    - Certificado en archivo .pfx:   $env:WIN_CERT_PFX (ruta) y $env:WIN_CERT_PASSWORD
    - Certificado en el almacén / token USB (EV u OV en hardware): $env:WIN_CERT_SUBJECT (nombre del sujeto)
    - Servidor de sello de tiempo: $env:WIN_TIMESTAMP_URL (por defecto http://timestamp.digicert.com)

  Requiere Inno Setup 6 (iscc.exe) y signtool.exe (Windows SDK) si se firma.
#>
param(
    [string] $BuildDir = "build",
    [string] $Version = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$appDir = Join-Path $root "$BuildDir\WorshipDAW_artefacts\Release"
$exe = Join-Path $appDir "Worship DAW.exe"
if (-not (Test-Path $exe)) { throw "No se encontró $exe. Compila primero en Release." }
if (-not $Version) {
    $Version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'project\(WorshipDAW VERSION ([0-9.]+)').Matches[0].Groups[1].Value
}

function Find-Tool([string] $name, [string[]] $globs) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($g in $globs) {
        $hit = Get-ChildItem -Path $g -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

$timestamp = if ($env:WIN_TIMESTAMP_URL) { $env:WIN_TIMESTAMP_URL } else { "http://timestamp.digicert.com" }
$canSign = $env:WIN_CERT_PFX -or $env:WIN_CERT_SUBJECT
$signtool = $null
if ($canSign) {
    $signtool = Find-Tool "signtool.exe" @("${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe")
    if (-not $signtool) { throw "Hay certificado pero no se encontró signtool.exe (instala el Windows SDK)." }
}

function Sign-File([string] $file) {
    if (-not $canSign) { return }
    Write-Host "Firmando $file"
    $signArgs = @("sign", "/fd", "sha256", "/tr", $timestamp, "/td", "sha256", "/d", "Worship DAW")
    if ($env:WIN_CERT_PFX) { $signArgs += @("/f", $env:WIN_CERT_PFX, "/p", $env:WIN_CERT_PASSWORD) }
    else { $signArgs += @("/n", $env:WIN_CERT_SUBJECT) }
    & $signtool @signArgs $file
    if ($LASTEXITCODE -ne 0) { throw "signtool falló al firmar $file" }
    & $signtool verify /pa $file
    if ($LASTEXITCODE -ne 0) { throw "La firma de $file no se pudo verificar" }
}

Sign-File $exe

$iscc = Find-Tool "iscc.exe" @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe")
if (-not $iscc) { throw "No se encontró Inno Setup 6 (iscc.exe). Instálalo desde jrsoftware.org o con: choco install innosetup" }
$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
& $iscc "/DAppVersion=$Version" "/DSourceDir=$appDir" "/DOutputDir=$dist" (Join-Path $root "installer\WorshipDAW.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup falló" }

$installer = Join-Path $dist "WorshipDAW-$Version-Windows-Setup.exe"
Sign-File $installer
if (-not $canSign) { Write-Host "Aviso: instalador SIN firmar (Windows mostrará 'editor desconocido')." }
Write-Host "Listo: $installer"
