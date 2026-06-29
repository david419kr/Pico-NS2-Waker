[CmdletBinding()]
param(
    [switch]$Clear,
    [string]$OutBin = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$source = @"
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class HidNative {
    [DllImport("hid.dll")]
    public static extern void HidD_GetHidGuid(out Guid hidGuid);

    [DllImport("hid.dll", SetLastError=true)]
    public static extern bool HidD_GetFeature(SafeFileHandle hFile, byte[] reportBuffer, int reportBufferLength);

    [DllImport("hid.dll", SetLastError=true)]
    public static extern bool HidD_SetFeature(SafeFileHandle hFile, byte[] reportBuffer, int reportBufferLength);

    [DllImport("setupapi.dll", SetLastError=true)]
    public static extern IntPtr SetupDiGetClassDevs(ref Guid classGuid, IntPtr enumerator, IntPtr hwndParent, uint flags);

    [DllImport("setupapi.dll", SetLastError=true)]
    public static extern bool SetupDiEnumDeviceInterfaces(IntPtr deviceInfoSet, IntPtr deviceInfoData, ref Guid interfaceClassGuid, uint memberIndex, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData);

    [DllImport("setupapi.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr deviceInfoSet, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData, IntPtr deviceInterfaceDetailData, uint deviceInterfaceDetailDataSize, out uint requiredSize, IntPtr deviceInfoData);

    [DllImport("setupapi.dll", SetLastError=true)]
    public static extern bool SetupDiDestroyDeviceInfoList(IntPtr deviceInfoSet);

    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern SafeFileHandle CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);

    [StructLayout(LayoutKind.Sequential)]
    public struct SP_DEVICE_INTERFACE_DATA {
        public uint cbSize;
        public Guid InterfaceClassGuid;
        public uint Flags;
        public UIntPtr Reserved;
    }
}
"@

Add-Type -TypeDefinition $source

$DIGCF_PRESENT = [uint32]0x00000002
$DIGCF_DEVICEINTERFACE = [uint32]0x00000010
$GENERIC_READ = [Convert]::ToUInt32('80000000', 16)
$GENERIC_WRITE = [Convert]::ToUInt32('40000000', 16)
$FILE_SHARE_READ = [uint32]0x00000001
$FILE_SHARE_WRITE = [uint32]0x00000002
$OPEN_EXISTING = [uint32]3
$INVALID_HANDLE_VALUE = [IntPtr]::new(-1)

function Get-HidDevicePaths {
    [Guid]$guid = [Guid]::Empty
    [HidNative]::HidD_GetHidGuid([ref]$guid)
    $set = [HidNative]::SetupDiGetClassDevs([ref]$guid, [IntPtr]::Zero, [IntPtr]::Zero, $DIGCF_PRESENT -bor $DIGCF_DEVICEINTERFACE)
    if ($set -eq $INVALID_HANDLE_VALUE) { throw "SetupDiGetClassDevs failed" }
    try {
        $index = 0
        while ($true) {
            $ifData = New-Object HidNative+SP_DEVICE_INTERFACE_DATA
            $ifData.cbSize = if ([IntPtr]::Size -eq 8) { 32 } else { 28 }
            $ok = [HidNative]::SetupDiEnumDeviceInterfaces($set, [IntPtr]::Zero, [ref]$guid, $index, [ref]$ifData)
            if (-not $ok) { break }
            $needed = 0
            [void][HidNative]::SetupDiGetDeviceInterfaceDetail($set, [ref]$ifData, [IntPtr]::Zero, 0, [ref]$needed, [IntPtr]::Zero)
            $detail = [Runtime.InteropServices.Marshal]::AllocHGlobal([int]$needed)
            try {
                $cbSize = if ([IntPtr]::Size -eq 8) { 8 } else { 6 }
                [Runtime.InteropServices.Marshal]::WriteInt32($detail, $cbSize)
                $ok = [HidNative]::SetupDiGetDeviceInterfaceDetail($set, [ref]$ifData, $detail, $needed, [ref]$needed, [IntPtr]::Zero)
                if ($ok) {
                    $pathPtr = [IntPtr]::Add($detail, 4)
                    [Runtime.InteropServices.Marshal]::PtrToStringUni($pathPtr)
                }
            } finally {
                [Runtime.InteropServices.Marshal]::FreeHGlobal($detail)
            }
            ++$index
        }
    } finally {
        [void][HidNative]::SetupDiDestroyDeviceInfoList($set)
    }
}

function Open-SwitchBridgeHid {
    $paths = Get-HidDevicePaths | Where-Object {
        $_ -match 'vid_057e' -and ($_ -match 'pid_2009' -or $_ -match 'pid_2069')
    }
    foreach ($path in $paths) {
        $handle = [HidNative]::CreateFile($path, $GENERIC_READ -bor $GENERIC_WRITE, $FILE_SHARE_READ -bor $FILE_SHARE_WRITE, [IntPtr]::Zero, $OPEN_EXISTING, 0, [IntPtr]::Zero)
        if (-not $handle.IsInvalid) {
            return $handle
        }
    }
    throw "SwitchBridge HID device not found. Connect the Pico while the firmware is running."
}

function Set-LogOffset([Microsoft.Win32.SafeHandles.SafeFileHandle]$Handle, [uint32]$Offset, [bool]$DoClear) {
    $buf = New-Object byte[] 64
    $buf[0] = 0x7f
    $buf[1] = [byte]($Offset -band 0xff)
    $buf[2] = [byte](($Offset -shr 8) -band 0xff)
    $buf[3] = [byte](($Offset -shr 16) -band 0xff)
    $buf[4] = [byte](($Offset -shr 24) -band 0xff)
    $buf[5] = if ($DoClear) { 1 } else { 0 }
    if (-not [HidNative]::HidD_SetFeature($Handle, $buf, $buf.Length)) {
        throw "HidD_SetFeature failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
}

function Read-LogRaw([Microsoft.Win32.SafeHandles.SafeFileHandle]$Handle) {
    Set-LogOffset $Handle 0 $false
    $raw = New-Object byte[] 4096
    $pos = 0
    while ($pos -lt $raw.Length) {
        $buf = New-Object byte[] 64
        $buf[0] = 0x7e
        if (-not [HidNative]::HidD_GetFeature($Handle, $buf, $buf.Length)) {
            throw "HidD_GetFeature failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
        }
        $copy = [Math]::Min(63, $raw.Length - $pos)
        [Array]::Copy($buf, 1, $raw, $pos, $copy)
        $pos += $copy
    }
    $raw
}

function U16([byte[]]$b, [int]$o) { [BitConverter]::ToUInt16($b, $o) }
function U32([byte[]]$b, [int]$o) { [BitConverter]::ToUInt32($b, $o) }

$events = @{
    1='Boot'; 2='StackReady'; 3='InquiryResult'; 4='CreateConnection'
    5='ConnectionRequest'; 6='ConnectionComplete'; 7='AuthComplete'
    8='EncryptionChange'; 9='Disconnect'; 10='SdpComplete'
    11='ControllerKind'; 12='L2capOpen'; 13='L2capIncoming'
    14='L2capClosed'; 15='HidData'
}
$kinds = @{
    0='Unknown'
    1='DualSense'
    2='DualShock4'
    3='SwitchPro'
    4='Xbox'
}

$handle = Open-SwitchBridgeHid
try {
    if ($Clear) {
        Set-LogOffset $handle 0 $true
        Write-Host "BT debug log cleared."
        return
    }

    $raw = Read-LogRaw $handle
    if ($OutBin) {
        [IO.File]::WriteAllBytes((Join-Path (Get-Location) $OutBin), $raw)
    }

    for ($i = 0; $i -lt $raw.Length; $i += 16) {
        $magic = U32 $raw $i
        if ($magic -eq [Convert]::ToUInt32('ffffffff', 16)) { break }
        if ($magic -ne [uint32]0x354c4447) { continue }
        $ms = U32 $raw ($i + 4)
        $event = $raw[$i + 8]
        $kind = $raw[$i + 9]
        $v16 = U16 $raw ($i + 10)
        $v32 = U32 $raw ($i + 12)
        $eventName = if ($events.ContainsKey([int]$event)) { $events[[int]$event] } else { "Event$event" }
        $kindName = if ($kinds.ContainsKey([int]$kind)) { $kinds[[int]$kind] } else { "Kind$kind" }
        "{0,6}ms  {1,-18} {2,-9} v16=0x{3:x4} v32=0x{4:x8}" -f $ms, $eventName, $kindName, $v16, $v32
    }
} finally {
    $handle.Dispose()
}
