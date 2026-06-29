#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace switchbridge {

constexpr std::size_t kSwitch2WakeConsoleAddrWireSize = 6;
constexpr std::size_t kSwitch2WakeAdvDataSize = 31;

using Switch2WakeConsoleAddrWire =
    std::array<uint8_t, kSwitch2WakeConsoleAddrWireSize>;
using Switch2WakeAdvData = std::array<uint8_t, kSwitch2WakeAdvDataSize>;

bool switch2_wake_addr_valid(const uint8_t addr_wire[kSwitch2WakeConsoleAddrWireSize]);
Switch2WakeAdvData switch2_wake_build_adv_data(
    const uint8_t console_addr_wire[kSwitch2WakeConsoleAddrWireSize]);

} // namespace switchbridge
