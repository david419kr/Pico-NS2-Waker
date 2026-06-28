[CmdletBinding()]
param(
    [string]$ToolsHome = "$env:USERPROFILE\.ds5onswitch-build"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$EnvScript = Join-Path $ToolsHome 'env.ps1'
if (Test-Path $EnvScript) {
    . $EnvScript
}

$gpp = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $gpp) {
    throw "g++ is not on PATH. Run tools\setup-pico-windows.ps1 first."
}

$OutDir = Join-Path $RepoRoot 'build\host'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Exe = Join-Path $OutDir 'host_mapping_test.exe'

& $gpp.Source -std=c++20 -Wall -Wextra `
    -I (Join-Path $RepoRoot 'include') `
    (Join-Path $RepoRoot 'src\dualsense_output.cpp') `
    (Join-Path $RepoRoot 'src\dualsense_report.cpp') `
    (Join-Path $RepoRoot 'src\haptics.cpp') `
    (Join-Path $RepoRoot 'src\mapper.cpp') `
    (Join-Path $RepoRoot 'src\persona_combo_filter.cpp') `
    (Join-Path $RepoRoot 'src\procon2_handshake.cpp') `
    (Join-Path $RepoRoot 'src\steam_controller.cpp') `
    (Join-Path $RepoRoot 'src\switch2_wake.cpp') `
    (Join-Path $RepoRoot 'src\switch_identity.cpp') `
    (Join-Path $RepoRoot 'src\switch_pro.cpp') `
    (Join-Path $RepoRoot 'src\switch_usb.cpp') `
    (Join-Path $RepoRoot 'tests\host_mapping_test.cpp') `
    -o $Exe

& $Exe
Write-Host "[ds5onswitch] Host mapping tests passed." -ForegroundColor Green
