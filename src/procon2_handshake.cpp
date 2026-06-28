#include "ds5onswitch/procon2_handshake.h"

#include <cstring>

namespace ds5onswitch {

namespace {

// ProCon2 control reports observed in procon2-usb-handshake.pcapng share a
// header layout:
//
//   [0] report id   (0x03 / 0x15 / 0x16)
//   [1] direction   (0x91 host->dev request, 0x01 dev->host reply)
//   [2..3] command  (LE16)
//   [4..5] length   (LE16; replies observed as 0x00f8)
//   [6] flags/seq   (0x00)
//   [7] sub/opcode  (request-specific)
//   [8..] payload
constexpr std::size_t kHeaderLen = 7;
constexpr uint16_t kReplyLengthMarker = 0x00f8;

uint16_t get_u16_le(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8u);
}

void put_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xffu);
    p[1] = static_cast<uint8_t>((v >> 8u) & 0xffu);
}

void write_header(Procon2Reply &reply, uint8_t report_id, uint16_t command)
{
    reply.data[0] = report_id;
    reply.data[1] = kProcon2DirDeviceReply;
    put_u16_le(&reply.data[2], command);
    put_u16_le(&reply.data[4], kReplyLengthMarker);
    reply.data[6] = 0x00u;
}

} // namespace

bool procon2_is_handshake_request(const uint8_t *data, std::size_t size)
{
    if (data == nullptr || size < kHeaderLen) {
        return false;
    }
    if (data[1] != kProcon2DirHostRequest) {
        return false;
    }
    return data[0] == kProcon2ReportMac || data[0] == kProcon2ReportPairing ||
           data[0] == kProcon2ReportInit;
}

Procon2Reply procon2_build_reply(const uint8_t *request, std::size_t size,
                                 const uint8_t controller_addr_wire[6])
{
    Procon2Reply reply{};
    if (!procon2_is_handshake_request(request, size)) {
        return reply;
    }

    const uint8_t report_id = request[0];
    const uint16_t command = get_u16_le(&request[2]);
    const uint8_t opcode = size > 7 ? request[7] : 0x00u;

    write_header(reply, report_id, command);

    switch (report_id) {
    case kProcon2ReportMac:
        // frame 309/333: console announces its MAC, controller acks with zeros.
        reply.data[6] = 0x01u;
        reply.data[7] = 0x00u;
        reply.data[8] = 0x00u;
        reply.data[9] = 0x00u;
        reply.len = 10;
        reply.valid = true;
        return reply;

    case kProcon2ReportInit:
        // frame 439/444: init exchange. Controller reply carried no extra body
        // beyond the header in the capture.
        reply.data[6] = 0x00u;
        reply.len = kHeaderLen + 1u;
        reply.valid = true;
        return reply;

    case kProcon2ReportPairing:
        reply.data[6] = 0x00u;
        reply.data[7] = 0x01u; // reply flag observed before each 0x15 body
        if (command == 0x0001u) {
            // frame 603/614: business-card exchange. The console sent both its
            // own and the controller's MAC. The controller reply observed was
            // "0401 c53382bfefe0" (6 bytes after a 2-byte tag). We echo the
            // controller's own wire-order address as the business card; the
            // exact tag semantics are unverified.
            reply.data[8] = 0x04u;
            reply.data[9] = 0x01u;
            if (controller_addr_wire != nullptr) {
                std::memcpy(&reply.data[10], controller_addr_wire, 6);
            }
            reply.len = 16;
            reply.valid = true;
            return reply;
        }
        if (command == 0x0004u || command == 0x0002u) {
            // frame 692/725, 840/851: 16-byte crypto challenge -> response.
            // UNKNOWN key derivation. Emit a zero placeholder and flag it so
            // callers/tests never treat this as a real authenticated reply.
            std::memset(&reply.data[8], 0x00u, 16);
            reply.len = 24;
            reply.is_crypto_placeholder = true;
            reply.valid = true;
            return reply;
        }
        // command 0x0003 (and others): terminal/ack marker with empty body.
        reply.len = kHeaderLen + 1u;
        reply.valid = true;
        (void)opcode;
        return reply;

    default:
        return reply;
    }
}

} // namespace ds5onswitch
