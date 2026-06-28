[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PicoSdkPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$TransportPath = Join-Path $PicoSdkPath 'src\rp2_common\pico_cyw43_driver\btstack_hci_transport_cyw43.c'
if (-not (Test-Path -LiteralPath $TransportPath)) {
    throw "CYW43 BTstack transport not found: $TransportPath"
}

$Source = Get-Content -LiteralPath $TransportPath -Raw

$Replacement = @'
    bd_addr_t addr = {0xa0, 0x5a, 0x5f, 0xa5, 0x15, 0xc6};
    hci_set_chipset(btstack_chipset_cyw43_instance());
    // DS5onSwitch: make the controller's real public BT/BLE address match the
    // anomalous Joy-Con wake identity captured in
    // BLE-captures/anomalous-joycon-wake-filtered_a0-5a-5f-a5-15-c6_*.pcap.
    // The Switch 2 ignored byte-perfect wake advertisements from both the Pico
    // factory address and the captured Switch Pro USB identity before sending
    // CONNECT_IND.
    hci_set_bd_addr(addr);
'@

if ($Source -like '*DS5onSwitch: make the controller''s real public BT/BLE address*') {
    $Pattern = '(?s)    bd_addr_t addr = \{0x(?:98|a0), 0x(?:b6|5a), 0x(?:e9|5f), 0x(?:e6|a5), 0x(?:dc|15), 0x(?:73|c6)\};\s+    hci_set_chipset\(btstack_chipset_cyw43_instance\(\)\);\s+    // DS5onSwitch: make the controller''s real public BT/BLE address match the\s+    // (?:Switch Pro USB identity\. The default Pico SDK path derives this from the\s+    // CYW43 factory/Wi-Fi MAC, which makes Switch 2 ignore byte-perfect wake\s+    // advertisements before sending CONNECT_IND\.|anomalous Joy-Con wake identity captured in\s+    // BLE-captures/anomalous-joycon-wake-filtered_a0-5a-5f-a5-15-c6_\*\.pcap\.\s+    // The Switch 2 ignored byte-perfect wake advertisements from both the Pico\s+    // factory address and the captured Switch Pro USB identity before sending\s+    // CONNECT_IND\.)\s+    hci_set_bd_addr\(addr\);'
    $Patched = [regex]::Replace($Source, $Pattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $Replacement }, 1)
    if ($Patched -ne $Source) {
        Set-Content -LiteralPath $TransportPath -Value $Patched -NoNewline
        Write-Host "[ds5onswitch] Refreshed CYW43 BT address patch: $TransportPath" -ForegroundColor Yellow
    } else {
        Write-Host "[ds5onswitch] CYW43 BT address patch already present: $TransportPath" -ForegroundColor DarkGray
    }
    return
}

$OriginalPattern = '(?s)    bd_addr_t addr;\s+    cyw43_hal_get_mac\(0, \(uint8_t\*\)&addr\);\s+    addr\[BD_ADDR_LEN - 1\]\+\+;\s+    hci_set_chipset\(btstack_chipset_cyw43_instance\(\)\);\s+    hci_set_bd_addr\(addr\);'

if (-not [regex]::IsMatch($Source, $OriginalPattern)) {
    throw "Could not find the expected CYW43 default BD_ADDR block in $TransportPath"
}

$Source = [regex]::Replace($Source, $OriginalPattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $Replacement }, 1)
Set-Content -LiteralPath $TransportPath -Value $Source -NoNewline
Write-Host "[ds5onswitch] Patched CYW43 BT public address in Pico SDK: $TransportPath" -ForegroundColor Yellow
