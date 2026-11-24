<#
.SYNOPSIS
    Installiert Living World (dist\LivingWorld.dll) in HITMAN World of Assassination.

.DESCRIPTION
    - kopiert LivingWorld.dll nach <Spiel>\Retail\mods
    - legt <Spiel>\Retail\mods\LivingWorld\models an (Sprachmodelle, Protokoll, Einstellungen)
    - aktiviert die Mod in mods.ini
    Andere Mods (z. B. CheatMenu) und deren Einstellungen bleiben unveraendert.
    Mit -Uninstall wird die Mod wieder deaktiviert und die DLL entfernt (Modelle bleiben liegen).
#>
param(
    [string]$GamePath = '',
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Dll = Join-Path $ProjectRoot 'dist\LivingWorld.dll'

function Find-Game {
    $candidates = @()
    try {
        $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath
        $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                $candidates += (Join-Path ($m.Groups[1].Value -replace '\\\\', '\') 'steamapps\common\HITMAN 3')
            }
        }
    } catch {}
    $candidates += 'D:\SteamLibrary\steamapps\common\HITMAN 3'
    $candidates += 'C:\Program Files (x86)\Steam\steamapps\common\HITMAN 3'
    $candidates += 'C:\Program Files\Epic Games\HITMAN3'
    return $candidates | Where-Object { Test-Path (Join-Path $_ 'Retail\HITMAN3.exe') } | Select-Object -First 1
}

if (-not $GamePath) { $GamePath = Find-Game }
if (-not $GamePath) { throw 'HITMAN-Ordner nicht gefunden. Bitte mit -GamePath "...\HITMAN 3" angeben.' }

$Retail = Join-Path $GamePath 'Retail'
if (-not (Test-Path (Join-Path $Retail 'ZHMModSDK.dll'))) {
    throw "ZHMModSDK ist in '$Retail' nicht installiert - Living World braucht das SDK."
}
if (Get-Process HITMAN3 -ErrorAction SilentlyContinue) {
    throw 'HITMAN laeuft gerade. Bitte das Spiel beenden und dann erneut installieren.'
}

$ModsDir = Join-Path $Retail 'mods'
$ModsIni = Join-Path $Retail 'mods.ini'
$ini = if (Test-Path $ModsIni) { [IO.File]::ReadAllText($ModsIni) } else { '' }

if ($Uninstall) {
    Remove-Item (Join-Path $ModsDir 'LivingWorld.dll') -ErrorAction SilentlyContinue
    $ini = [regex]::Replace($ini, '(?mi)^\[livingworld\]\s*(\r?\n)*', '')
    [IO.File]::WriteAllText($ModsIni, $ini)
    Write-Host 'Living World wurde deaktiviert und entfernt (Modelle/Einstellungen in mods\LivingWorld bleiben erhalten).' -ForegroundColor Yellow
    return
}

if (-not (Test-Path $Dll)) { throw "dist\LivingWorld.dll fehlt - erst tools\build.ps1 ausfuehren." }

New-Item -ItemType Directory -Force (Join-Path $ModsDir 'LivingWorld\models') | Out-Null
Copy-Item $Dll (Join-Path $ModsDir 'LivingWorld.dll') -Force

if ($ini -notmatch '(?mi)^\[livingworld\]') {
    $ini = "[livingworld]`r`n`r`n" + $ini
    [IO.File]::WriteAllText($ModsIni, $ini)
}

Write-Host "Living World installiert nach $ModsDir" -ForegroundColor Green
Write-Host 'Im Spiel: T = Chat, B gedrueckt halten = Sprechen, F8 = Einstellungen.'
