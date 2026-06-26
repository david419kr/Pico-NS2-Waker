[CmdletBinding()]
param(
    [string]$ToolsHome = "$env:USERPROFILE\.ds5onswitch-build",
    [switch]$SkipDownloads
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$PicoSdkRef = '2.2.0'
$TinyUsbRef = '0.20.0'
$ArmVer = '14.2.rel1'
$ArmZip = "arm-gnu-toolchain-$ArmVer-mingw-w64-x86_64-arm-none-eabi.zip"
$ArmUrl = "https://developer.arm.com/-/media/Files/downloads/gnu/$ArmVer/binrel/$ArmZip"
$MingwUrl = 'https://github.com/brechtsanders/winlibs_mingw/releases/download/14.2.0posix-19.1.1-12.0.0-ucrt-r2/winlibs-x86_64-posix-seh-gcc-14.2.0-mingw-w64ucrt-12.0.0-r2.zip'

$SdkPath = Join-Path $ToolsHome 'pico-sdk'
$ArmRoot = Join-Path $ToolsHome 'arm-gnu-toolchain'
$MingwRoot = Join-Path $ToolsHome 'mingw64'
$EnvScript = Join-Path $ToolsHome 'env.ps1'

function Info($m) { Write-Host "[ds5onswitch] $m" -ForegroundColor Cyan }
function Ok($m) { Write-Host "[ds5onswitch] $m" -ForegroundColor Green }
function Die($m) { Write-Host "[ds5onswitch] ERROR: $m" -ForegroundColor Red; exit 1 }
function Have($cmd) { [bool](Get-Command $cmd -ErrorAction SilentlyContinue) }

function Add-SessionPath($dir) {
    if ($dir -and (Test-Path $dir) -and ($env:Path -notlike "*$dir*")) {
        $env:Path = "$dir;$env:Path"
    }
}

function Add-CommonToolPaths {
    Add-SessionPath "$env:ProgramFiles\CMake\bin"
    Add-SessionPath "$env:ProgramFiles\Git\cmd"
    Add-SessionPath "$env:LOCALAPPDATA\Microsoft\WinGet\Links"
}

function Expand-Download($url, $destination, $zipName) {
    if ($SkipDownloads) { Die "Missing dependency and -SkipDownloads was set: $destination" }
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $zip = Join-Path $env:TEMP $zipName
    Info "Downloading $zipName"
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    Info "Extracting $zipName"
    Expand-Archive -Path $zip -DestinationPath $destination -Force
    Remove-Item $zip -Force
}

function Ensure-HostCompiler {
    if (Have cl) { Ok 'Host compiler: MSVC cl'; return }
    $gpp = Get-Command g++ -ErrorAction SilentlyContinue
    if ($gpp -and $gpp.Source -notlike '*arm-none-eabi*') {
        Ok "Host compiler: $($gpp.Source)"
        return
    }
    $existing = Get-ChildItem -Path $MingwRoot -Recurse -Filter g++.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $existing) {
        Expand-Download $MingwUrl $MingwRoot 'winlibs-mingw.zip'
        $existing = Get-ChildItem -Path $MingwRoot -Recurse -Filter g++.exe |
            Select-Object -First 1
    }
    if (-not $existing) { Die 'Could not install a native host compiler.' }
    Add-SessionPath (Split-Path $existing.FullName)
    Ok "Host compiler: $($existing.DirectoryName)"
}

function Ensure-ArmToolchain {
    $existing = Get-ChildItem -Path $ArmRoot -Recurse -Filter arm-none-eabi-gcc.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $existing) {
        Expand-Download $ArmUrl $ArmRoot $ArmZip
        $existing = Get-ChildItem -Path $ArmRoot -Recurse -Filter arm-none-eabi-gcc.exe |
            Select-Object -First 1
    }
    if (-not $existing) { Die 'Could not install ARM GNU toolchain.' }
    Add-SessionPath (Split-Path $existing.FullName)
    Ok "ARM toolchain: $($existing.DirectoryName)"
}

function Ensure-PicoSdk {
    if (-not (Test-Path (Join-Path $SdkPath 'pico_sdk_init.cmake'))) {
        if ($SkipDownloads) { Die "Pico SDK missing and -SkipDownloads was set: $SdkPath" }
        Info "Cloning pico-sdk $PicoSdkRef"
        git clone --depth 1 --branch $PicoSdkRef https://github.com/raspberrypi/pico-sdk.git $SdkPath
        git -C $SdkPath submodule update --init --recursive
    } else {
        Ok "Pico SDK already present: $SdkPath"
    }

    $tinyusb = Join-Path $SdkPath 'lib\tinyusb'
    $tag = ''
    if (Test-Path $tinyusb) {
        $tag = (git -C $tinyusb describe --tags --exact-match 2>$null)
    }
    if ($tag -ne $TinyUsbRef) {
        Info "Checking out TinyUSB $TinyUsbRef"
        git -C $tinyusb fetch --depth 1 origin "refs/tags/${TinyUsbRef}:refs/tags/$TinyUsbRef"
        git -C $tinyusb checkout --detach $TinyUsbRef
    } else {
        Ok "TinyUSB already at $TinyUsbRef"
    }
}

New-Item -ItemType Directory -Force -Path $ToolsHome | Out-Null
Add-CommonToolPaths

foreach ($cmd in @('git', 'cmake', 'ninja')) {
    if (-not (Have $cmd)) { Die "$cmd is missing. Install it or add it to PATH first." }
    Ok "${cmd}: $((Get-Command $cmd).Source)"
}

Ensure-HostCompiler
Ensure-ArmToolchain
Ensure-PicoSdk

$armBin = Split-Path (Get-ChildItem -Path $ArmRoot -Recurse -Filter arm-none-eabi-gcc.exe | Select-Object -First 1).FullName
$hostGpp = Get-Command g++ -ErrorAction SilentlyContinue
$hostBin = if ($hostGpp) { Split-Path $hostGpp.Source } else { '' }

@"
`$env:PICO_SDK_PATH = '$SdkPath'
`$env:Path = '$armBin;$hostBin;$env:ProgramFiles\CMake\bin;$env:LOCALAPPDATA\Microsoft\WinGet\Links;' + `$env:Path
"@ | Set-Content -Encoding ASCII $EnvScript

Ok "Environment ready. To reuse in a new PowerShell session:"
Ok "  . '$EnvScript'"
