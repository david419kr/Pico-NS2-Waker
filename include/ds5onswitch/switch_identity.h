#pragma once

#include <array>
#include <cstdint>

namespace ds5onswitch {

using SwitchControllerBdAddr = std::array<uint8_t, 6>;

// Fallback Bluetooth controller identity reported by the Switch Pro USB
// persona. Captured from BLE-captures/procon1-handshake.pcapng together with
// that capture's USB pairing material.
//
// This is now only a fallback/default. The real identity should be the Pico's
// own factory-derived public Bluetooth address (CYW43 Wi-Fi MAC + 1), injected
// at boot via switch_identity_set_bd_addr(). Keeping the controller identity
// equal to the radio's real address is what makes a *general-purpose* Switch 2
// wake possible, instead of borrowing a hardcoded anomalous Joy-Con address.
constexpr SwitchControllerBdAddr kSwitchProControllerBdAddrFallback{
    0x98, 0xb6, 0xe9, 0xe6, 0xdc, 0x73,
};

// Current controller Bluetooth identity in display/public order (MSB first),
// i.e. the order used by the device-info subcommand reply. USB connection-status
// and manual-pairing replies use the reversed (HID wire / little-endian) order.
const SwitchControllerBdAddr &switch_identity_bd_addr();

// Override the controller identity. addr must be in display/public order
// (same order as kSwitchProControllerBdAddrFallback). Ignored if addr is all
// zero or all 0xff. Call once at boot after the CYW43 radio MAC is known.
void switch_identity_set_bd_addr(const SwitchControllerBdAddr &addr);

} // namespace ds5onswitch
