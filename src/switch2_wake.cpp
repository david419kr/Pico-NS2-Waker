#include "switch_bridge/switch2_wake.h"

namespace switchbridge {

void Switch2WakeUsbState::update(bool mounted, bool suspended, uint32_t now_ms)
{
    if (mounted && !suspended) {
        inactive_ = false;
    } else if (!inactive_) {
        inactive_ = true;
        inactive_since_ms_ = now_ms;
    }
}

bool Switch2WakeUsbState::active() const
{
    return !inactive_;
}

bool Switch2WakeUsbState::sleep_candidate(uint32_t now_ms,
                                           bool reconnect_pending,
                                           uint32_t debounce_ms) const
{
    return inactive_ && !reconnect_pending &&
           now_ms - inactive_since_ms_ >= debounce_ms;
}

bool switch2_wake_addr_valid(const uint8_t addr_wire[kSwitch2WakeConsoleAddrWireSize])
{
    if (addr_wire == nullptr) {
        return false;
    }

    bool any_nonzero = false;
    bool any_not_ff = false;
    for (std::size_t i = 0; i < kSwitch2WakeConsoleAddrWireSize; ++i) {
        any_nonzero = any_nonzero || addr_wire[i] != 0x00u;
        any_not_ff = any_not_ff || addr_wire[i] != 0xffu;
    }
    return any_nonzero && any_not_ff;
}

Switch2WakeAdvData switch2_wake_build_adv_data(
    const uint8_t console_addr_wire[kSwitch2WakeConsoleAddrWireSize])
{
    Switch2WakeAdvData adv{
        0x02, 0x01, 0x06,
        0x1b, 0xff, 0x53, 0x05,
        0x01, 0x00, 0x03, 0x7e, 0x05, 0x66, 0x20, 0x00, 0x01, 0x81,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    if (console_addr_wire != nullptr) {
        for (std::size_t i = 0; i < kSwitch2WakeConsoleAddrWireSize; ++i) {
            adv[17u + i] = console_addr_wire[i];
        }
    }

    return adv;
}

} // namespace switchbridge
