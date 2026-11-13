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

class Switch2WakeUsbGuard {
public:
    void start(bool forced);
    void reset();
    bool should_cancel(bool usb_active);

private:
    bool forced_ = false;
    bool saw_inactive_ = false;
};

class Switch2WakeUsbState {
public:
    void mounted();
    void disconnected(uint32_t now_ms);
    void update(bool mounted, bool suspended, uint32_t now_ms);
    bool active() const;
    bool sleep_candidate(uint32_t now_ms, bool reconnect_pending,
                         uint32_t debounce_ms) const;

private:
    bool session_mounted_ = false;
    bool inactive_ = false;
    uint32_t inactive_since_ms_ = 0;
};

bool switch2_wake_addr_valid(const uint8_t addr_wire[kSwitch2WakeConsoleAddrWireSize]);
Switch2WakeAdvData switch2_wake_build_adv_data(
    const uint8_t console_addr_wire[kSwitch2WakeConsoleAddrWireSize]);

} // namespace switchbridge
