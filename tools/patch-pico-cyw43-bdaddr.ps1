[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PicoSdkPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# SwitchBridge wake approach (current):
#
# We no longer override the controller's real public BT/BLE address in the Pico
# SDK CYW43 BTstack transport. Earlier branches hardcoded a stolen anomalous
# Joy-Con address (a0:5a:5f:a5:15:c6) here to borrow that console's existing BLE
# wake whitelist entry. That worked but is not general-purpose.
#
# The new approach keeps the Pico's own factory-derived address and instead aims
# to get the controller registered into the Switch 2 BLE wake whitelist via a
# Switch 2 (Pro Controller 2) style USB handshake / business-card exchange. So
# this script now RESTORES the stock SDK behavior (BT address = Wi-Fi MAC + 1)
# if a previous build patched it.

$TransportPath = Join-Path $PicoSdkPath 'src\rp2_common\pico_cyw43_driver\btstack_hci_transport_cyw43.c'
if (-not (Test-Path -LiteralPath $TransportPath)) {
    throw "CYW43 BTstack transport not found: $TransportPath"
}

$Source = Get-Content -LiteralPath $TransportPath -Raw

$StockBlock = "    bd_addr_t addr;`r`n" +
    "    cyw43_hal_get_mac(0, (uint8_t*)&addr);`r`n" +
    "    addr[BD_ADDR_LEN - 1]++;`r`n" +
    "    hci_set_chipset(btstack_chipset_cyw43_instance());`r`n" +
    "    hci_set_bd_addr(addr);"

if ($Source -notlike '*SwitchBridge:*' -and
    $Source -like '*cyw43_hal_get_mac(0, (uint8_t*)&addr);*') {
    Write-Host "[switchbridge] CYW43 BT address already stock (factory MAC + 1): $TransportPath" -ForegroundColor DarkGray
    return
}

# Match any project override block we may have written previously and
# replace it with the stock SDK code so the Pico keeps its own factory address.
$PatchedPattern = '(?s)    bd_addr_t addr = \{0x[0-9a-fA-F]{2}, 0x[0-9a-fA-F]{2}, 0x[0-9a-fA-F]{2}, 0x[0-9a-fA-F]{2}, 0x[0-9a-fA-F]{2}, 0x[0-9a-fA-F]{2}\};\s+    hci_set_chipset\(btstack_chipset_cyw43_instance\(\)\);\s+    // [^\r\n]*?:.*?hci_set_bd_addr\(addr\);'

if ([regex]::IsMatch($Source, $PatchedPattern)) {
    $Restored = [regex]::Replace($Source, $PatchedPattern, [System.Text.RegularExpressions.MatchEvaluator] { param($m) $StockBlock }, 1)
    if ($Restored -ne $Source) {
        Set-Content -LiteralPath $TransportPath -Value $Restored -NoNewline
        Write-Host "[switchbridge] Restored stock CYW43 BT address (factory MAC + 1): $TransportPath" -ForegroundColor Yellow
    }
    return
}

throw "Could not find a known CYW43 BD_ADDR block (stock or project-patched) in $TransportPath"
