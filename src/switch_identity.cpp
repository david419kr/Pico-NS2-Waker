#include "switch_bridge/switch_identity.h"

namespace switchbridge {

namespace {

SwitchControllerBdAddr g_bd_addr = kSwitchProControllerBdAddrFallback;

bool addr_usable(const SwitchControllerBdAddr &addr)
{
    bool any_nonzero = false;
    bool any_not_ff = false;
    for (uint8_t b : addr) {
        any_nonzero = any_nonzero || b != 0x00u;
        any_not_ff = any_not_ff || b != 0xffu;
    }
    return any_nonzero && any_not_ff;
}

} // namespace

const SwitchControllerBdAddr &switch_identity_bd_addr()
{
    return g_bd_addr;
}

void switch_identity_set_bd_addr(const SwitchControllerBdAddr &addr)
{
    if (!addr_usable(addr)) {
        return;
    }
    g_bd_addr = addr;
}

} // namespace switchbridge
