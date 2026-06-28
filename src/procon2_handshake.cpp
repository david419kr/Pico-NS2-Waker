#include "ds5onswitch/procon2_handshake.h"

#include <cstring>

namespace ds5onswitch {

namespace {

// ProCon2 control reports observed in procon2-usb-handshake.pcapng share a
// header layout:
//
//   [0] report id   (0x03 / 0x15 / 0x16)
//   [1] direction   (0x91 host->dev request, 0x01 dev->host reply)
//   [2] transport   (0x00 in the USB capture, 0x01 in public BLE docs)
//   [3] subcommand  (0x01 / 0x02 / 0x03 / 0x04 ...)
//   [4] status/len0 (requests: observed length low byte, replies: 0x00)
//   [5] status/len1 (requests: observed length high byte, replies: 0xf8)
//   [6..7] zeros
//   [8..] payload   (payload[0] is 0x00 in requests, 0x01 in 0x15 replies)
constexpr std::size_t kHeaderLen = 8;
constexpr uint8_t kReplyStatus = 0x00;
constexpr uint8_t kReplyAck = 0xf8;
constexpr uint8_t kPayloadRequestMarker = 0x00;
constexpr uint8_t kPayloadReplyMarker = 0x01;

constexpr uint8_t kProcon2FixedB1Wire[16] = {
    0x5c, 0xf6, 0xee, 0x79, 0x2c, 0xdf, 0x05, 0xe1,
    0xba, 0x2b, 0x63, 0x25, 0xc4, 0x1a, 0x5f, 0x10,
};

uint8_t g_ltk_wire[16]{};
bool g_ltk_ready = false;

void write_header(Procon2Reply &reply, const uint8_t *request)
{
    reply.data[0] = request[0];
    reply.data[1] = kProcon2DirDeviceReply;
    reply.data[2] = request[2];
    reply.data[3] = request[3];
    reply.data[4] = kReplyStatus;
    reply.data[5] = kReplyAck;
    reply.data[6] = 0x00u;
    reply.data[7] = 0x00u;
}

void reverse16(const uint8_t in[16], uint8_t out[16])
{
    for (std::size_t i = 0; i < 16u; ++i) {
        out[i] = in[15u - i];
    }
}

static const uint8_t kAesSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};

uint8_t aes_xtime(uint8_t value)
{
    return static_cast<uint8_t>((value << 1u) ^
                                ((value & 0x80u) != 0u ? 0x1bu : 0x00u));
}

void aes_add_round_key(uint8_t state[16], const uint8_t *round_key)
{
    for (std::size_t i = 0; i < 16u; ++i) {
        state[i] ^= round_key[i];
    }
}

void aes_sub_bytes(uint8_t state[16])
{
    for (std::size_t i = 0; i < 16u; ++i) {
        state[i] = kAesSbox[state[i]];
    }
}

void aes_shift_rows(uint8_t state[16])
{
    uint8_t tmp = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = tmp;

    tmp = state[2];
    state[2] = state[10];
    state[10] = tmp;
    tmp = state[6];
    state[6] = state[14];
    state[14] = tmp;

    tmp = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = tmp;
}

void aes_mix_columns(uint8_t state[16])
{
    for (std::size_t i = 0u; i < 16u; i += 4u) {
        const uint8_t a0 = state[i];
        const uint8_t a1 = state[i + 1u];
        const uint8_t a2 = state[i + 2u];
        const uint8_t a3 = state[i + 3u];
        const uint8_t t = static_cast<uint8_t>(a0 ^ a1 ^ a2 ^ a3);
        const uint8_t u = a0;
        state[i] = static_cast<uint8_t>(
            state[i] ^ t ^ aes_xtime(static_cast<uint8_t>(a0 ^ a1)));
        state[i + 1u] = static_cast<uint8_t>(
            state[i + 1u] ^ t ^ aes_xtime(static_cast<uint8_t>(a1 ^ a2)));
        state[i + 2u] = static_cast<uint8_t>(
            state[i + 2u] ^ t ^ aes_xtime(static_cast<uint8_t>(a2 ^ a3)));
        state[i + 3u] = static_cast<uint8_t>(
            state[i + 3u] ^ t ^ aes_xtime(static_cast<uint8_t>(a3 ^ u)));
    }
}

void aes128_expand_key(const uint8_t key[16], uint8_t round_key[176])
{
    static const uint8_t rcon[10] = {
        0x01u, 0x02u, 0x04u, 0x08u, 0x10u,
        0x20u, 0x40u, 0x80u, 0x1bu, 0x36u,
    };
    uint8_t temp[4]{};
    std::size_t bytes = 16u;
    std::size_t rcon_index = 0u;

    std::memcpy(round_key, key, 16u);
    while (bytes < 176u) {
        std::memcpy(temp, &round_key[bytes - 4u], sizeof(temp));
        if ((bytes % 16u) == 0u) {
            const uint8_t first = temp[0];
            temp[0] = static_cast<uint8_t>(kAesSbox[temp[1]] ^ rcon[rcon_index++]);
            temp[1] = kAesSbox[temp[2]];
            temp[2] = kAesSbox[temp[3]];
            temp[3] = kAesSbox[first];
        }
        for (uint8_t b : temp) {
            round_key[bytes] = static_cast<uint8_t>(round_key[bytes - 16u] ^ b);
            ++bytes;
        }
    }
}

void aes128_encrypt_block(const uint8_t key[16], const uint8_t in[16],
                          uint8_t out[16])
{
    uint8_t round_key[176]{};
    std::memcpy(out, in, 16u);
    aes128_expand_key(key, round_key);
    aes_add_round_key(out, &round_key[0]);
    for (std::size_t round = 1u; round < 10u; ++round) {
        aes_sub_bytes(out);
        aes_shift_rows(out);
        aes_mix_columns(out);
        aes_add_round_key(out, &round_key[round * 16u]);
    }
    aes_sub_bytes(out);
    aes_shift_rows(out);
    aes_add_round_key(out, &round_key[160]);
}

void compute_ltk_from_a1_wire(const uint8_t a1_wire[16])
{
    for (std::size_t i = 0; i < 16u; ++i) {
        g_ltk_wire[i] = static_cast<uint8_t>(a1_wire[i] ^ kProcon2FixedB1Wire[i]);
    }
    g_ltk_ready = true;
}

void compute_b2_wire(const uint8_t a2_wire[16], uint8_t b2_wire[16])
{
    uint8_t ltk_aes_order[16]{};
    uint8_t a2_aes_order[16]{};
    reverse16(g_ltk_wire, ltk_aes_order);
    reverse16(a2_wire, a2_aes_order);
    aes128_encrypt_block(ltk_aes_order, a2_aes_order, b2_wire);
}

bool has_payload(const uint8_t *request, std::size_t size, std::size_t payload_len)
{
    return request != nullptr && size >= kHeaderLen + payload_len;
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
    const uint8_t subcommand = request[3];

    write_header(reply, request);

    switch (report_id) {
    case kProcon2ReportMac:
        // frame 309/333: console announces its MAC, controller acks with zeros.
        reply.data[8] = kPayloadReplyMarker;
        reply.data[9] = 0x00u;
        reply.data[10] = 0x00u;
        reply.data[11] = 0x00u;
        reply.len = 12;
        reply.valid = true;
        return reply;

    case kProcon2ReportInit:
        // frame 439/444: init exchange. Controller reply carried no extra body
        // beyond the header in the capture.
        reply.len = 32u;
        reply.valid = true;
        return reply;

    case kProcon2ReportPairing:
        reply.data[8] = kPayloadReplyMarker;
        if (subcommand == 0x01u) {
            // frame 603/614: business-card exchange. The console sent both its
            // own and the controller's MAC. The controller reply observed was
            // "0401 c53382bfefe0" (6 bytes after a 2-byte tag). We echo the
            // controller's own wire-order address as the business card; the
            // exact tag semantics are unverified.
            reply.data[9] = 0x04u;
            reply.data[10] = 0x01u;
            if (controller_addr_wire != nullptr) {
                std::memcpy(&reply.data[11], controller_addr_wire, 6);
            }
            reply.len = 17;
            reply.valid = true;
            return reply;
        }
        if (subcommand == 0x04u) {
            // Exchange LTK components. Host request payload[1..16] is A1 in
            // wire order; controller response payload[1..16] is fixed B1.
            if (has_payload(request, size, 17u) &&
                request[8] == kPayloadRequestMarker) {
                compute_ltk_from_a1_wire(&request[9]);
            }
            std::memcpy(&reply.data[9], kProcon2FixedB1Wire,
                        sizeof(kProcon2FixedB1Wire));
            reply.len = 25;
            reply.valid = true;
            return reply;
        }
        if (subcommand == 0x02u) {
            // Confirm LTK. Host request payload[1..16] is A2 in wire order.
            // Public RE formula:
            //   B2 = AES-128-ECB(reverse(A2), key=reverse(A1 XOR B1))
            // The AES output is written directly as the wire-order B2.
            if (g_ltk_ready && has_payload(request, size, 17u) &&
                request[8] == kPayloadRequestMarker) {
                compute_b2_wire(&request[9], &reply.data[9]);
            } else {
                std::memset(&reply.data[9], 0x00u, 16u);
                reply.is_crypto_placeholder = true;
            }
            reply.len = 25;
            reply.valid = true;
            return reply;
        }
        // subcommand 0x03 (and others): terminal/ack marker with empty body.
        reply.len = 9u;
        reply.valid = true;
        return reply;

    default:
        return reply;
    }
}

} // namespace ds5onswitch
