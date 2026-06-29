#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace switchbridge {

// ProCon2 (Switch 2 Pro Controller) USB "business-card" handshake helper.
//
// This models the control-report exchange observed in
// BLE-captures/procon2-usb-handshake.pcapng. The hypothesis (see
// docs/2026-06-28_procon2-usb-handshake-analysis.md) is that completing a
// Switch 2 style handshake registers the controller's own Bluetooth address
// into the console's BLE wake whitelist, enabling a *general-purpose* wake
// using the Pico's own address instead of a stolen, hardcoded one.
//
// The 0x15/0x04 + 0x15/0x02 challenge/response follows the public RE from
// ndeadly/switch2_controller_research:
//
//   0x15/0x04: host sends A1, controller replies fixed B1
//   LTK = A1 XOR B1
//   0x15/0x02: host sends A2, controller replies
//               AES-128-ECB(reverse(A2), key=reverse(LTK))
//
// The 16-byte A1/A2/B1/B2 values are carried in the packet's reverse byte
// order. The AES output is placed on the wire as produced by the above formula.

constexpr uint8_t kProcon2ReportMac = 0x03;        // console MAC announce / ack
constexpr uint8_t kProcon2ReportPairing = 0x15;    // business-card + crypto
constexpr uint8_t kProcon2ReportInit = 0x16;       // init exchange

constexpr uint8_t kProcon2DirHostRequest = 0x91;   // host -> device
constexpr uint8_t kProcon2DirDeviceReply = 0x01;   // device -> host

// The largest device->host reply is an SPI flash read (cmd 0x02): an 8-byte
// header + 8-byte address block + up to 0x40 bytes of flash = 0x50 bytes.
constexpr std::size_t kProcon2ReplyMaxLen = 0x50;

struct Procon2Reply {
    bool valid = false;
    // Legacy test/diagnostic flag. This should remain false now that 0x15/0x02
    // is computed from the exchanged LTK material instead of zero-filled.
    bool is_crypto_placeholder = false;
    bool business_card_sent = false;
    bool ltk_exchanged = false;
    bool ltk_confirmed = false;
    bool pairing_finalized = false;
    std::size_t len = 0;
    std::array<uint8_t, kProcon2ReplyMaxLen> data{};
};

// Build the device->host reply for a host->device ProCon2 control report.
// `controller_addr_wire` is the controller's own BD_ADDR in HID wire order
// (little-endian), i.e. the same order embedded into the BLE wake advert.
//
// Returns Procon2Reply::valid=false if the input is not a recognized
// host-request control report.
Procon2Reply procon2_build_reply(const uint8_t *request, std::size_t size,
                                 const uint8_t controller_addr_wire[6]);

// True if the report is a host->device ProCon2 control request we model.
bool procon2_is_handshake_request(const uint8_t *data, std::size_t size);

// Reset ProCon2 handshake session state (pairing LTK, negotiated features,
// active report id). Call when a fresh Switch 2 registration persona starts.
void procon2_handshake_reset();

} // namespace switchbridge
