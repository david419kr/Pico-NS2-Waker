#pragma once

#include <array>
#include <cstdint>

namespace ds5onswitch {

// Bluetooth controller identity reported by the Switch Pro USB persona.
// Captured from BLE-captures/procon1-handshake.pcapng together with that
// capture's USB pairing material.
constexpr std::array<uint8_t, 6> kSwitchProControllerBdAddr{
    0x98, 0xb6, 0xe9, 0xe6, 0xdc, 0x73,
};

// Experimental Switch 2 BLE wake identity.
//
// The Switch 2 ignored byte-perfect wake advertisements when the AdvA was the
// Pico factory address or the captured Switch Pro USB identity above. The
// working anomalous Joy-Con capture received CONNECT_IND from the console when
// advertising from this public address, so keep USB persona identity and wake
// AdvA deliberately separated for this test branch.
constexpr std::array<uint8_t, 6> kSwitch2WakeBleBdAddr{
    0xa0, 0x5a, 0x5f, 0xa5, 0x15, 0xc6,
};

} // namespace ds5onswitch
