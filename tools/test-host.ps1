[CmdletBinding()]
param(
    [string]$ToolsHome = "$env:USERPROFILE\.switchbridge-build"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$EnvScript = Join-Path $ToolsHome 'env.ps1'
if (-not (Test-Path $EnvScript)) {
    $LegacyToolsHome = Join-Path $env:USERPROFILE ('.' + 'ds5' + 'on' + 'switch-build')
    $LegacyEnvScript = Join-Path $LegacyToolsHome 'env.ps1'
    if (Test-Path $LegacyEnvScript) {
        $EnvScript = $LegacyEnvScript
    }
}
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
    (Join-Path $RepoRoot 'src\dualshock4_output.cpp') `
    (Join-Path $RepoRoot 'src\dualshock4_report.cpp') `
    (Join-Path $RepoRoot 'src\dualsense_output.cpp') `
    (Join-Path $RepoRoot 'src\dualsense_report.cpp') `
    (Join-Path $RepoRoot 'src\haptics.cpp') `
    (Join-Path $RepoRoot 'src\mapper.cpp') `
    (Join-Path $RepoRoot 'src\persona_combo_filter.cpp') `
    (Join-Path $RepoRoot 'src\procon2_handshake.cpp') `
    (Join-Path $RepoRoot 'src\procon2_report.cpp') `
    (Join-Path $RepoRoot 'src\steam_controller.cpp') `
    (Join-Path $RepoRoot 'src\switch2_wake.cpp') `
    (Join-Path $RepoRoot 'src\switch_identity.cpp') `
    (Join-Path $RepoRoot 'src\switch_pro.cpp') `
    (Join-Path $RepoRoot 'src\switch_usb.cpp') `
    (Join-Path $RepoRoot 'src\xbox_controller.cpp') `
    (Join-Path $RepoRoot 'tests\host_mapping_test.cpp') `
    -o $Exe

& $Exe
Write-Host "[switchbridge] Host mapping tests passed." -ForegroundColor Green
