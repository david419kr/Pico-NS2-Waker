#pragma once

#include <array>
#include <cstdint>

namespace ds5onswitch {

// Public Bluetooth controller identity reported to the Switch Pro USB persona
// and programmed into the CYW43 controller before BTstack powers HCI on.
//
// The Raspberry Pi CYW43 factory address (2c:cf:67:...) produced a byte-perfect
// Switch 2 wake advertisement but the console never sent CONNECT_IND. Use the
// Nintendo/Pro-Controller-family identity captured in
// BLE-captures/procon1-handshake.pcapng together with that capture's USB
// pairing material, so the USB registration record and BLE AdvA are coherent.
constexpr std::array<uint8_t, 6> kSwitchProControllerBdAddr{
    0x98, 0xb6, 0xe9, 0xe6, 0xdc, 0x73,
};

} // namespace ds5onswitch
