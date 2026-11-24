<#
.SYNOPSIS
    Baut die Living-World-Mod im vorhandenen ZHMModSDK-Baum und installiert sie (optional) ins Spiel.

.DESCRIPTION
    - Spiegelt den Quellcode aus .\Mod nach <SDK>\Mods\LivingWorld
    - traegt die Mod in die SDK-CMakeLists ein (nlohmann-json und whisper.cpp kommen per CPM aus den Quellarchiven)
    - baut LivingWorld.dll und LivingWorldTests.exe mit MSVC (VS Build Tools 2022) + Ninja
    - mit -Install: kopiert die DLL nach <Spiel>\Retail\mods und aktiviert sie in mods.ini
      (die CheatMenu-Mod und alles andere bleibt unangetastet)

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Install
#>
param(
    # Dauerhafter Ort fuer den SDK-Baum (nicht %TEMP% - dort loescht Windows alte Dateien automatisch).
    [string]$SdkRoot = 'D:\HitmanModding\ZHMModSDK',
    [string]$GamePath = '',
    [switch]$Install,
    [switch]$RunTests
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ModSource = Join-Path $ProjectRoot 'Mod'

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

# ---------------------------------------------------------------------------------------------------------------------
# 1) SDK-Baum pruefen
# ---------------------------------------------------------------------------------------------------------------------
if (-not (Test-Path (Join-Path $SdkRoot 'ZHMModSDK\Include\IPluginInterface.h'))) {
    throw "ZHMModSDK-Quellbaum nicht gefunden unter '$SdkRoot'. Mit -SdkRoot angeben."
}
$BuildDir = Join-Path $SdkRoot '_build\x64-Release'

# exFAT-Laufwerke speichern keine Besitzer; Git verweigert dann Repos ("dubious ownership").
# Nur fuer diesen Prozess erlauben - die globale Git-Konfiguration bleibt unberuehrt.
$env:GIT_CONFIG_COUNT = '1'
$env:GIT_CONFIG_KEY_0 = 'safe.directory'
$env:GIT_CONFIG_VALUE_0 = '*'

# ---------------------------------------------------------------------------------------------------------------------
# 2) Quellcode spiegeln
# ---------------------------------------------------------------------------------------------------------------------
Write-Step "Quellcode -> $SdkRoot\Mods\LivingWorld"
$ModTarget = Join-Path $SdkRoot 'Mods\LivingWorld'
& robocopy $ModSource $ModTarget /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy fehlgeschlagen ($LASTEXITCODE)" }

# ---------------------------------------------------------------------------------------------------------------------
# 3) Mod in die SDK-CMakeLists eintragen
# ---------------------------------------------------------------------------------------------------------------------
$RootCMake = Join-Path $SdkRoot 'CMakeLists.txt'
$cmakeText = [IO.File]::ReadAllText($RootCMake)
if ($cmakeText -notmatch '(?m)^\s+LivingWorld\s*$') {
    Write-Step 'Trage LivingWorld in die MODS-Liste ein'
    $cmakeText = [regex]::Replace($cmakeText, '(?m)^(\s+)CheatMenu\s*$', "`$1CheatMenu`r`n`$1LivingWorld", 1)
    if ($cmakeText -notmatch '(?m)^\s+LivingWorld\s*$') { throw 'Konnte LivingWorld nicht in die MODS-Liste eintragen.' }
    [IO.File]::WriteAllText($RootCMake, $cmakeText)
}

# (nlohmann-json und whisper.cpp holt Mod/CMakeLists.txt per CPM direkt aus den Quellarchiven.)

# ---------------------------------------------------------------------------------------------------------------------
# 4) MSVC-Umgebung laden und bauen
# ---------------------------------------------------------------------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio Build Tools mit C++ nicht gefunden.' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'

Write-Step 'Lade MSVC-Umgebung'
$envDump = & cmd.exe /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $envDump) {
    $idx = $line.IndexOf('=')
    if ($idx -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0, $idx), $line.Substring($idx + 1), 'Process') }
}

$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

# Das SDK enthaelt einen kleinen Rust-Teil (corrosion) – cargo/rustup muessen im PATH sein.
$cargoBin = Join-Path $env:USERPROFILE '.cargo\bin'
if ((Test-Path $cargoBin) -and ($env:PATH -notlike "*$cargoBin*")) { $env:PATH = "$cargoBin;$env:PATH" }

if (-not (Test-Path (Join-Path $BuildDir 'build.ninja'))) {
    Write-Step 'Erstkonfiguration des SDK-Baums (Preset x64-Release, vcpkg stellt Pakete aus dem Cache wieder her)'
    Push-Location $SdkRoot
    try {
        & $cmake --preset x64-Release
        if ($LASTEXITCODE -ne 0) { throw "CMake-Konfiguration fehlgeschlagen (Exit $LASTEXITCODE)" }
    } finally { Pop-Location }
}

Write-Step 'Baue LivingWorld + LivingWorldTests (beim ersten Mal inkl. SDK und whisper.cpp, das dauert)'
& $cmake --build $BuildDir --target LivingWorld LivingWorldTests
if ($LASTEXITCODE -ne 0) { throw "Build fehlgeschlagen (Exit $LASTEXITCODE)" }

$Dll = Join-Path $BuildDir 'Mods\LivingWorld\LivingWorld.dll'
$Tests = Join-Path $BuildDir 'Mods\LivingWorld\LivingWorldTests.exe'
Write-Host "DLL:   $Dll"
Write-Host "Tests: $Tests"

# Fertige DLL ins Projekt kopieren (dist\), damit install.ps1 ohne Build-Werkzeuge auskommt.
$Dist = Join-Path $ProjectRoot 'dist'
New-Item -ItemType Directory -Force $Dist | Out-Null
Copy-Item $Dll (Join-Path $Dist 'LivingWorld.dll') -Force
Write-Host "Kopie: $Dist\LivingWorld.dll"

if ($RunTests) {
    Write-Step 'Starte Kern-Tests'
    & $Tests
    if ($LASTEXITCODE -ne 0) { throw "Tests fehlgeschlagen (Exit $LASTEXITCODE)" }
}

if ($Install) {
    Write-Step 'Installiere ins Spiel'
    if ($GamePath) { & (Join-Path $PSScriptRoot 'install.ps1') -GamePath $GamePath } else { & (Join-Path $PSScriptRoot 'install.ps1') }
}