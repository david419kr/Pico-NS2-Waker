[CmdletBinding()]
param(
    [switch]$Clean,
    [string]$BuildDir = 'build\pico2_w',
    [string]$ToolsHome = "$env:USERPROFILE\.ds5onswitch-build"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$EnvScript = Join-Path $ToolsHome 'env.ps1'
if (Test-Path $EnvScript) {
    . $EnvScript
}

if (-not $env:PICO_SDK_PATH) {
    throw "PICO_SDK_PATH is not set. Run tools\setup-pico-windows.ps1 first."
}
if (-not (Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue)) {
    throw "arm-none-eabi-gcc is not on PATH. Run tools\setup-pico-windows.ps1 first."
}

& (Join-Path $PSScriptRoot 'patch-pico-cyw43-bdaddr.ps1') -PicoSdkPath $env:PICO_SDK_PATH
if (-not $?) {
    throw "CYW43 BD_ADDR patch failed."
}

$FullBuildDir = Join-Path $RepoRoot $BuildDir
if ($Clean -and (Test-Path $FullBuildDir)) {
    Remove-Item -Recurse -Force $FullBuildDir
}

cmake -S $RepoRoot -B $FullBuildDir -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DPICO_BOARD=pico2_w `
    "-DPICO_SDK_PATH=$env:PICO_SDK_PATH"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

cmake --build $FullBuildDir --target ds5onswitch
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE."
}

$uf2 = Join-Path $FullBuildDir 'ds5onswitch.uf2'
if (-not (Test-Path $uf2)) {
    throw "Build finished but $uf2 was not produced."
}

Write-Host "[ds5onswitch] Firmware: $uf2" -ForegroundColor Green
