#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ds5onswitch {

// ProCon2 (Switch 2 Pro Controller) USB "business-card" handshake helper.
//
// This models the control-report exchange observed in
// BLE-captures/procon2-usb-handshake.pcapng. The hypothesis (see
// docs/2026-06-28_procon2-usb-handshake-analysis.md) is that completing a
// Switch 2 style handshake registers the controller's own Bluetooth address
// into the console's BLE wake whitelist, enabling a *general-purpose* wake
// using the Pico's own address instead of a stolen, hardcoded one.
//
// IMPORTANT / UNCERTAIN: the captured 0x15 exchange contains a 16-byte
// challenge/response that is almost certainly key-based (LTK/session-key
// negotiation). We cannot derive correct responses from the capture alone.
// Those replies are produced here as best-effort placeholders and are flagged
// via Procon2Reply::is_crypto_placeholder so callers/tests know the response is
// not authenticated. This module is therefore handshake *scaffolding*, not a
// verified working ProCon2 authentication.

constexpr uint8_t kProcon2ReportMac = 0x03;        // console MAC announce / ack
constexpr uint8_t kProcon2ReportPairing = 0x15;    // business-card + crypto
constexpr uint8_t kProcon2ReportInit = 0x16;       // init exchange

constexpr uint8_t kProcon2DirHostRequest = 0x91;   // host -> device
constexpr uint8_t kProcon2DirDeviceReply = 0x01;   // device -> host

constexpr std::size_t kProcon2ReplyMaxLen = 64;

struct Procon2Reply {
    bool valid = false;
    bool is_crypto_placeholder = false;
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

} // namespace ds5onswitch
