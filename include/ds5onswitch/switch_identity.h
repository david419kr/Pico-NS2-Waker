#pragma once

#include <array>
#include <cstdint>

namespace ds5onswitch {

// Public Bluetooth controller identity reported to the Switch Pro USB persona.
// Keep USB replies and BLE wake advertising source address in sync.
constexpr std::array<uint8_t, 6> kSwitchProControllerBdAddr{
    0x00, 0x1b, 0x7a, 0x01, 0x02, 0x03,
};

} // namespace ds5onswitch
