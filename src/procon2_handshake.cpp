#include "switch_bridge/procon2_handshake.h"

#include <cstring>

namespace switchbridge {

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

// Negotiable controller feature flags (mirrors nRF5340 SC_SWITCH2_FEATURE_*).
constexpr uint8_t kFeatureButtons = 0x01u;
constexpr uint8_t kFeatureSticks = 0x02u;
constexpr uint8_t kFeatureImu = 0x04u;
constexpr uint8_t kFeatureMouse = 0x10u;
constexpr uint8_t kFeatureRumble = 0x20u;
// We register as a button/stick/trigger only controller: no IMU/rumble lanes.
constexpr uint8_t kFeaturesDefault =
    static_cast<uint8_t>(kFeatureButtons | kFeatureSticks);

constexpr uint8_t kCommandAck1 = 0x78u;
constexpr uint16_t kBatteryVolts = 3800u;
constexpr uint8_t kReportCommon = 0x05u;
constexpr uint8_t kReportPro = 0x09u;

constexpr uint8_t kProcon2FixedB1Wire[16] = {
    0x5c, 0xf6, 0xee, 0x79, 0x2c, 0xdf, 0x05, 0xe1,
    0xba, 0x2b, 0x63, 0x25, 0xc4, 0x1a, 0x5f, 0x10,
};

uint8_t g_ltk_wire[16]{};
bool g_ltk_ready = false;
bool g_ltk_confirmed = false;
uint8_t g_feature_flags = kFeaturesDefault;
uint8_t g_feature_mask = 0u;
uint8_t g_active_report_id = kReportCommon;

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

void put_u16le(uint8_t *p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value & 0xffu);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xffu);
}

void put_u32le(uint8_t *p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value & 0xffu);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    p[2] = static_cast<uint8_t>((value >> 16) & 0xffu);
    p[3] = static_cast<uint8_t>((value >> 24) & 0xffu);
}

uint32_t get_u32le(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint8_t masked_features(uint8_t flags)
{
    return g_feature_mask == 0u ? flags
                                : static_cast<uint8_t>(flags & g_feature_mask);
}

void feature_info(uint8_t flags, uint8_t out[8])
{
    std::memset(out, 0, 8u);
    if ((flags & kFeatureButtons) != 0u) {
        out[0] = 0x07u;
    }
    if ((flags & kFeatureSticks) != 0u) {
        out[1] = 0x07u;
    }
    if ((flags & kFeatureImu) != 0u) {
        out[2] = 0x01u;
    }
    if ((flags & kFeatureMouse) != 0u) {
        out[4] = 0x01u;
    }
    if ((flags & kFeatureRumble) != 0u) {
        out[5] = 0x03u;
    }
}

// Minimal SPI-flash image the Switch 2 reads during ProCon2 registration.
// Only the blocks the console actually requests need real content; everything
// else returns 0xff (erased flash), as captured from a genuine ProCon2.
void minimal_flash_block(uint32_t address, uint8_t out[0x40])
{
    std::memset(out, 0, 0x40u);
    switch (address) {
    case 0x13000u: {
        std::memset(out, 0xff, 0x40u);
        out[0] = 0x01u;
        out[1] = 0x00u;
        static const char serial[] = "HEK10000220896";
        std::memcpy(&out[2], serial, 14u);
        static const uint8_t info[] = {
            0x7e, 0x05, 0x69, 0x20, 0x01, 0x06, 0x01, 0x23, 0x23, 0x23,
            0xa0, 0xa0, 0xa0, 0xe6, 0xe6, 0xe6, 0x32, 0x32, 0x32,
        };
        std::memcpy(&out[0x12], info, sizeof(info));
        break;
    }
    case 0x13040u: {
        static const uint8_t key_like[16] = {
            0x8c, 0xce, 0xda, 0x41, 0x5e, 0xff, 0x4a, 0x3b,
            0xa9, 0x91, 0xde, 0x3b, 0x62, 0x57, 0x0a, 0xbb,
        };
        std::memset(out, 0xff, 0x40u);
        std::memcpy(out, key_like, sizeof(key_like));
        break;
    }
    case 0x13080u:
    case 0x130c0u: {
        static const uint8_t prefix[0x28] = {
            0x01, 0xad, 0xd9, 0x9a, 0x55, 0x56, 0x65, 0xa0,
            0x00, 0x0a, 0xa0, 0x00, 0x0a, 0xe2, 0x20, 0x0e,
            0xe2, 0x20, 0x0e, 0x9a, 0xad, 0xd9, 0x9a, 0xad,
            0xd9, 0x0a, 0xa5, 0x50, 0x0a, 0xa5, 0x50, 0x2f,
            0xf6, 0x62, 0x2f, 0xf6, 0x62, 0x0a, 0xff, 0xff,
        };
        static const uint8_t primary_cal[9] = {
            0xaa, 0x27, 0x82, 0x6a, 0x96, 0x5f, 0x33, 0x66, 0x62,
        };
        static const uint8_t secondary_cal[9] = {
            0x4e, 0x98, 0x84, 0xee, 0xa5, 0x65, 0x4c, 0x06, 0x64,
        };
        std::memset(out, 0xff, 0x40u);
        std::memcpy(out, prefix, sizeof(prefix));
        std::memcpy(&out[0x28],
                    address == 0x13080u ? primary_cal : secondary_cal, 9u);
        break;
    }
    case 0x13100u: {
        static const uint8_t data[0x18] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xd5, 0x30, 0xa3, 0xb9,
            0x96, 0x95, 0xdc, 0x3d, 0x59, 0x3b, 0x1d, 0x41,
        };
        std::memcpy(out, data, sizeof(data));
        break;
    }
    case 0x13e00u: {
        static const char serial[] = "17152528LD1743101";
        std::memcpy(out, serial, 17u);
        static const uint8_t tail20[] = {0x01, 0x02, 0x02, 0x02};
        std::memcpy(&out[0x20], tail20, sizeof(tail20));
        static const uint8_t tail30[] = {
            0x03, 0x02, 0x04, 0x01, 0x05, 0x02, 0x06, 0x02, 0x07, 0x02,
        };
        std::memcpy(&out[0x30], tail30, sizeof(tail30));
        break;
    }
    case 0x1fb000u: {
        static const uint8_t data[] = {
            0x02, 0x00, 0x02, 0x00, 0x01, 0x00, 0x11, 0x03,
        };
        std::memset(out, 0xff, 0x40u);
        std::memcpy(out, data, sizeof(data));
        break;
    }
    case 0x13060u:
    case 0x1fc000u:
    case 0x1fc040u:
    case 0x1fc080u:
        std::memset(out, 0xff, 0x40u);
        break;
    default:
        break;
    }
}

} // namespace

bool procon2_is_handshake_request(const uint8_t *data, std::size_t size)
{
    if (data == nullptr || size < kHeaderLen) {
        return false;
    }
    // Any host->device control request (direction 0x91) is part of the ProCon2
    // bulk handshake. The real Switch 2 issues ~21 distinct command/subcommand
    // pairs during registration (cmd 0x01,0x02,0x03,0x07,0x09,0x0a,0x0b,0x0c,
    // 0x10,0x11,0x15,0x16,0x18); answering only a subset makes the console give
    // up before pairing completes.
    return data[1] == kProcon2DirHostRequest;
}

void procon2_handshake_reset()
{
    std::memset(g_ltk_wire, 0, sizeof(g_ltk_wire));
    g_ltk_ready = false;
    g_ltk_confirmed = false;
    g_feature_flags = kFeaturesDefault;
    g_feature_mask = 0u;
    g_active_report_id = kReportCommon;
}

Procon2Reply procon2_build_reply(const uint8_t *request, std::size_t size,
                                 const uint8_t controller_addr_wire[6])
{
    Procon2Reply reply{};
    if (!procon2_is_handshake_request(request, size)) {
        return reply;
    }

    const uint8_t cmd = request[0];
    const uint8_t sub = request[3];
    uint8_t *out = reply.data.data();
    std::size_t response_len = kHeaderLen;

    // Default device->host header: cmd, dir=0x01, transport, sub, 0x00, 0xf8.
    write_header(reply, request);
    reply.valid = true;

    switch (cmd) {
    case 0x01u:
        if (sub == 0x0cu) {
            static const uint8_t payload[] = {0x61u, 0x12u, 0x50u, 0x10u};
            std::memcpy(&out[8], payload, sizeof(payload));
            response_len = 8u + sizeof(payload);
        }
        break;

    case 0x02u:
        // SPI flash access (calibration / device info reads).
        if (sub == 0x01u && size >= 16u) {
            const uint32_t address = get_u32le(&request[12]);
            uint8_t flash[0x40];
            out[8] = 0x40u;
            put_u32le(&out[12], address);
            minimal_flash_block(address, flash);
            std::memcpy(&out[16], flash, sizeof(flash));
            response_len = 0x50u;
        } else if (sub == 0x02u && size >= 16u) {
            std::memcpy(&out[8], &request[8], 8u);
            response_len = 16u;
        } else if (sub == 0x03u && size >= 16u) {
            out[4] = 0x00u;
            out[5] = kCommandAck1;
            std::memcpy(&out[8], &request[8], 4u);
            response_len = 12u;
        } else if (sub == 0x04u && size >= 16u) {
            uint32_t read_size = request[8];
            const uint32_t address = get_u32le(&request[12]);
            const uint32_t offset = address & 0x3fu;
            uint8_t flash[0x40];
            if (read_size > 0x40u) {
                read_size = 0x40u;
            }
            const uint32_t copy_len =
                read_size < (0x40u - offset) ? read_size : (0x40u - offset);
            put_u32le(&out[8], read_size);
            put_u32le(&out[12], address);
            minimal_flash_block(address & ~0x3fu, flash);
            std::memcpy(&out[16], &flash[offset], copy_len);
            response_len = 16u + read_size;
        } else if (sub == 0x05u && size >= 16u) {
            std::memcpy(&out[8], &request[8], 8u);
            response_len = 16u;
        }
        break;

    case 0x03u:
        switch (sub) {
        case 0x01u:
        case 0x02u:
        case 0x08u:
        case 0x09u:
        case 0x0cu:
            break; // header-only ack
        case 0x03u:
            out[8] = size >= 9u ? request[8] : 0x01u;
            response_len = 12u;
            break;
        case 0x0au:
            // Select active input report id (0x05 common / 0x09 pro).
            if (size >= 9u &&
                (request[8] == kReportCommon || request[8] == kReportPro)) {
                g_active_report_id = request[8];
            }
            break;
        case 0x0du:
            out[8] = 0x01u;
            response_len = 12u;
            break;
        case 0x0fu:
            out[8] = 0x05u;
            response_len = 12u;
            break;
        default:
            break;
        }
        break;

    case 0x07u:
        // 0x07/0x01: header + single zero payload byte (9-byte reply), matching
        // the genuine ProCon2 capture.
        if (sub == 0x01u) {
            out[8] = 0x00u;
            response_len = 9u;
        }
        break;

    case 0x09u:
    case 0x0au:
        break; // header-only ack

    case 0x0bu:
        switch (sub) {
        case 0x03u:
            put_u16le(&out[8], kBatteryVolts);
            response_len = 12u;
            break;
        case 0x04u:
            out[8] = 0x34u;
            out[10] = 0x83u;
            response_len = 12u;
            break;
        case 0x06u:
            out[8] = 0x11u;
            response_len = 12u;
            break;
        default:
            break;
        }
        break;

    case 0x0cu: {
        // Feature negotiation. We only ever advertise button/stick support.
        uint8_t flags = size >= 9u ? request[8] : 0u;
        switch (sub) {
        case 0x01u: {
            uint8_t info[8];
            feature_info(flags, info);
            std::memcpy(&out[12], info, sizeof(info));
            response_len = 20u;
            break;
        }
        case 0x02u:
            g_feature_mask = flags;
            response_len = 12u;
            break;
        case 0x03u:
            g_feature_mask = 0u;
            g_feature_flags = 0u;
            response_len = 12u;
            break;
        case 0x04u:
            flags = masked_features(flags);
            g_feature_flags = static_cast<uint8_t>(g_feature_flags | flags);
            response_len = 12u;
            break;
        case 0x05u:
            flags = masked_features(flags);
            g_feature_flags =
                static_cast<uint8_t>(g_feature_flags & ~flags);
            response_len = 12u;
            break;
        case 0x06u:
            put_u32le(&out[12], 0x02u);
            out[16] = 0x00u;
            out[17] = 0x50u;
            response_len = 48u;
            break;
        default:
            response_len = 12u;
            break;
        }
        break;
    }

    case 0x10u:
        if (sub == 0x01u) {
            static const uint8_t firmware_info[] = {
                0x02u, 0x01u, 0x04u, 0x02u, 0x0cu, 0x00u,
                0x00u, 0x00u, 0x00u, 0x02u, 0x03u, 0x00u,
            };
            std::memcpy(&out[8], firmware_info, sizeof(firmware_info));
            response_len = 8u + sizeof(firmware_info);
        }
        break;

    case 0x11u:
        if (sub == 0x01u) {
            static const uint8_t payload[] = {0x03u, 0x00u, 0x00u, 0x00u};
            std::memcpy(&out[8], payload, sizeof(payload));
            response_len = 8u + sizeof(payload);
        } else if (sub == 0x03u) {
            static const uint8_t payload[] = {
                0x01u, 0xc0u, 0x03u, 0x00u, 0x00u, 0xe7u, 0xd0u, 0x1cu,
                0x3bu, 0x79u, 0x22u, 0xa0u, 0x3au, 0x0au, 0xe8u, 0x9cu,
                0x42u, 0x58u, 0xa0u, 0x0bu, 0x42u, 0x0au, 0xe8u, 0x9cu,
                0x41u, 0x58u, 0xa0u, 0x0bu, 0x41u,
            };
            std::memcpy(&out[8], payload, sizeof(payload));
            response_len = 8u + sizeof(payload);
        }
        break;

    case kProcon2ReportPairing: // 0x15: business-card + RE-based crypto.
        out[8] = kPayloadReplyMarker;
        switch (sub) {
        case 0x01u:
            // Business-card exchange: embed the controller's own wire-order
            // BD_ADDR so the console whitelists this controller for BLE wake.
            out[9] = 0x04u;
            out[10] = 0x01u;
            if (controller_addr_wire != nullptr) {
                std::memcpy(&out[11], controller_addr_wire, 6);
            }
            reply.business_card_sent = true;
            response_len = 17u;
            break;
        case 0x04u:
            // Exchange LTK components: request payload is A1, reply is fixed B1.
            //   LTK = A1 XOR B1   (ndeadly/switch2_controller_research)
            if (has_payload(request, size, 17u) &&
                request[8] == kPayloadRequestMarker) {
                compute_ltk_from_a1_wire(&request[9]);
                reply.ltk_exchanged = true;
            }
            std::memcpy(&out[9], kProcon2FixedB1Wire,
                        sizeof(kProcon2FixedB1Wire));
            response_len = 25u;
            break;
        case 0x02u:
            // Confirm LTK: B2 = AES-128-ECB(reverse(A2), key=reverse(LTK)).
            if (g_ltk_ready && has_payload(request, size, 17u) &&
                request[8] == kPayloadRequestMarker) {
                compute_b2_wire(&request[9], &out[9]);
                g_ltk_confirmed = true;
                reply.ltk_confirmed = true;
            } else {
                std::memset(&out[9], 0x00u, 16u);
                reply.is_crypto_placeholder = true;
            }
            response_len = 25u;
            break;
        case 0x03u:
            // Finalise pairing: header + reply marker only.
            reply.pairing_finalized = g_ltk_confirmed;
            response_len = 9u;
            break;
        default:
            response_len = 9u;
            break;
        }
        break;

    case kProcon2ReportInit: // 0x16: init exchange, zero-padded 32-byte reply.
        if (sub == 0x01u) {
            response_len = 32u;
        }
        break;

    case 0x18u:
        if (sub == 0x01u) {
            static const uint8_t payload[] = {
                0x00u, 0x00u, 0x40u, 0xf0u, 0x00u, 0x00u, 0x60u, 0x00u,
            };
            std::memcpy(&out[8], payload, sizeof(payload));
            response_len = 8u + sizeof(payload);
        }
        break;

    default:
        // Unknown command: acknowledge with a header-only reply.
        break;
    }

    reply.len = response_len;
    return reply;
}

} // namespace switchbridge
