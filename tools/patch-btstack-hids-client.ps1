[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PicoSdkPath,

    [int]$ReportCount = 32
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$HeaderPath = Join-Path $PicoSdkPath 'lib\btstack\src\ble\gatt-service\hids_client.h'
if (-not (Test-Path -LiteralPath $HeaderPath)) {
    throw "BTstack HIDS client header not found: $HeaderPath"
}

$Source = Get-Content -LiteralPath $HeaderPath -Raw
$Pattern = '#define HIDS_CLIENT_NUM_REPORTS\s+\d+'
$Replacement = "#define HIDS_CLIENT_NUM_REPORTS $ReportCount"

if (-not [regex]::IsMatch($Source, $Pattern)) {
    throw "Could not find HIDS_CLIENT_NUM_REPORTS definition in $HeaderPath"
}

$Patched = [regex]::Replace($Source, $Pattern, $Replacement, 1)
if ($Patched -eq $Source) {
    Write-Host "[switchbridge] BTstack HIDS report table already patched: $HeaderPath" -ForegroundColor DarkGray
    return
}

Set-Content -LiteralPath $HeaderPath -Value $Patched -NoNewline
Write-Host "[switchbridge] Patched BTstack HIDS report table to $ReportCount reports: $HeaderPath" -ForegroundColor Yellow
