#pragma once

#include <array>
#include <cstdint>

namespace ds5onswitch {

// Public Bluetooth controller identity reported to the Switch Pro USB persona.
// This must match the CYW43 Bluetooth public address seen by the Switch 2 in
// BLE wake advertisements; otherwise the console ignores the wake packet even
// when the Nintendo manufacturer payload is correct.
constexpr std::array<uint8_t, 6> kSwitchProControllerBdAddr{
    0x2c, 0xcf, 0x67, 0xdf, 0x93, 0x93,
};

} // namespace ds5onswitch
