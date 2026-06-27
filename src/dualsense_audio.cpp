#include "ds5onswitch/dualsense_audio.h"

#include "ds5onswitch/dualsense_bt.h"

#include "bsp/board_api.h"
#include "opus.h"
#include "pico.h"
#include "tusb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace ds5onswitch {
namespace {

constexpr uint8_t kSpeakerInterface = 2;
constexpr uint8_t kMicOnlyInterface = 2;
constexpr uint8_t kHeadsetMicInterface = 3;
constexpr uint8_t kSpeakerFeatureUnit = 2;
constexpr uint8_t kMicFeatureUnit = 5;
constexpr uint8_t kAudioReportId = 0x36;
constexpr uint8_t kAudioStatusReportId = 0x32;
constexpr uint8_t kAudioBufferLength = 64;
constexpr uint8_t kHapticBytesPerReport = 64;
constexpr uint8_t kSpeakerUsbChannels = 2;
constexpr uint8_t kSpeakerOpusChannels = 2;
constexpr uint16_t kSpeakerInputFrameSamples = 512;
constexpr uint16_t kOpusBytesPerReport = 200;
constexpr uint16_t kOpusFrameSamples = 480;
constexpr uint16_t kMicOpusBytes = 71;
constexpr uint8_t kMicQueueDepth = 4;
constexpr uint8_t kSpeakerReadBurstsPerLoop = 4;
constexpr float kSpeakerPreGain = 5.0f;
constexpr uint32_t kSpeakerStreamTimeoutMs = 2000;

struct MicPacket {
    std::array<uint8_t, kMicOpusBytes> data{};
};

bool g_connected = false;
bool g_headphones_connected = false;
bool g_mic_connected = false;
bool g_mic_muted = false;
bool g_external_mic_active = false;
bool g_speaker_streaming = false;
bool g_mic_streaming = false;
uint8_t g_report_sequence = 0;
uint8_t g_packet_counter = 0;
uint32_t g_last_speaker_audio_ms = 0;
std::array<float, kSpeakerInputFrameSamples * kSpeakerOpusChannels>
    g_speaker_input_frame{};
uint16_t g_speaker_input_frames = 0;
std::array<float, kOpusFrameSamples * kSpeakerOpusChannels> g_pcm_frame{};
std::array<MicPacket, kMicQueueDepth> g_mic_queue{};
uint8_t g_mic_head = 0;
uint8_t g_mic_tail = 0;
uint8_t g_mic_count = 0;
OpusEncoder *g_encoder = nullptr;
OpusDecoder *g_decoder = nullptr;
uint8_t g_mute[2] = {0, 0};
int16_t g_volume[2] = {0, 0};
float g_speaker_gain = kSpeakerPreGain;

bool __not_in_flash_func(mic_enabled)()
{
    return g_connected && g_mic_streaming;
}

void recompute_speaker_gain()
{
    if (g_mute[0] != 0) {
        g_speaker_gain = 0.0f;
        return;
    }

    const float db = static_cast<float>(g_volume[0]) / 256.0f;
    g_speaker_gain = kSpeakerPreGain * powf(10.0f, db / 20.0f);
}

void __not_in_flash_func(clear_mic_queue)()
{
    g_mic_head = 0;
    g_mic_tail = 0;
    g_mic_count = 0;
}

bool __not_in_flash_func(pop_mic_packet)(MicPacket &packet)
{
    if (g_mic_count == 0) {
        return false;
    }

    packet = g_mic_queue[g_mic_tail];
    g_mic_tail = static_cast<uint8_t>((g_mic_tail + 1u) % kMicQueueDepth);
    --g_mic_count;
    return true;
}

void __not_in_flash_func(push_mic_packet)(const uint8_t *data)
{
    if (g_mic_count == kMicQueueDepth) {
        g_mic_tail = static_cast<uint8_t>((g_mic_tail + 1u) % kMicQueueDepth);
        --g_mic_count;
    }

    std::copy(data, data + kMicOpusBytes, g_mic_queue[g_mic_head].data.begin());
    g_mic_head = static_cast<uint8_t>((g_mic_head + 1u) % kMicQueueDepth);
    ++g_mic_count;
}

void __not_in_flash_func(drain_speaker_audio)()
{
    uint8_t raw[512];
    while (tud_audio_available() != 0) {
        if (tud_audio_read(raw, sizeof(raw)) == 0) {
            break;
        }
    }
    g_speaker_input_frames = 0;
}

void __not_in_flash_func(resample_speaker_frame_512_to_480)()
{
    constexpr uint32_t kResampleStepQ16 =
        (static_cast<uint32_t>(kSpeakerInputFrameSamples) << 16u) /
        kOpusFrameSamples;
    uint32_t position_q16 = 0;

    for (uint16_t dst = 0; dst < kOpusFrameSamples; ++dst) {
        uint16_t src = static_cast<uint16_t>(position_q16 >> 16u);
        if (src >= kSpeakerInputFrameSamples - 1u) {
            src = kSpeakerInputFrameSamples - 2u;
        }

        const uint32_t frac = position_q16 & 0xffffu;
        for (uint8_t ch = 0; ch < kSpeakerOpusChannels; ++ch) {
            const float a =
                g_speaker_input_frame[src * kSpeakerOpusChannels + ch];
            const float b =
                g_speaker_input_frame[(src + 1u) * kSpeakerOpusChannels + ch];
            g_pcm_frame[dst * kSpeakerOpusChannels + ch] =
                a + (b - a) * (static_cast<float>(frac) / 65536.0f);
        }

        position_q16 += kResampleStepQ16;
    }
}

bool __not_in_flash_func(encode_opus_frame)(uint8_t *out)
{
    if (g_encoder == nullptr || out == nullptr) {
        return false;
    }

    std::fill(out, out + kOpusBytesPerReport, 0);
    const int encoded = opus_encode_float(g_encoder, g_pcm_frame.data(),
                                          kOpusFrameSamples, out,
                                          kOpusBytesPerReport);
    return encoded > 0;
}

bool __not_in_flash_func(send_audio_status_report)()
{
    if (!dualsense_bt_is_connected()) {
        return false;
    }

    std::array<uint8_t, 142> report{};
    report[0] = kAudioStatusReportId;
    report[1] = static_cast<uint8_t>((g_report_sequence & 0x0fu) << 4u);
    g_report_sequence = static_cast<uint8_t>((g_report_sequence + 1u) & 0x0fu);
    report[2] = 0x91;
    report[3] = 7;
    report[4] = mic_enabled() ? 0xff : 0xfe;
    std::fill(report.begin() + 5, report.begin() + 10, kAudioBufferLength);
    report[10] = g_packet_counter++;

    fill_dualsense_output_crc(report.data(), report.size());
    return dualsense_bt_send_raw_output_report(report.data(), report.size());
}

bool __not_in_flash_func(send_speaker_report)(const DualSenseOutputState &state,
                                              uint8_t &report_sequence)
{
    DualSenseBtAudioReport report{};
    report[0] = kAudioReportId;
    report[1] = static_cast<uint8_t>((report_sequence & 0x0fu) << 4u);
    report_sequence = static_cast<uint8_t>((report_sequence + 1u) & 0x0fu);
    report[2] = 0x91;
    report[3] = 7;
    report[4] = mic_enabled() ? 0xff : 0xfe;
    std::fill(report.begin() + 5, report.begin() + 10, kAudioBufferLength);
    report[10] = g_packet_counter++;

    report[11] = 0x90;
    report[12] = kDualSenseOutputStateBlockSize;
    state.copy_state_block(report.data() + 13, kDualSenseOutputStateBlockSize);

    report[76] = 0x92;
    report[77] = kHapticBytesPerReport;

    report[142] = 0x96;
    report[143] = static_cast<uint8_t>(kOpusBytesPerReport);
    if (!encode_opus_frame(report.data() + 144)) {
        return false;
    }

    fill_dualsense_output_crc(report.data(), report.size());
    return dualsense_bt_send_raw_output_report(report.data(), report.size());
}

void __not_in_flash_func(append_speaker_usb_frames)(
    const int16_t *samples, uint16_t frames, const DualSenseOutputState &state,
    uint8_t &report_sequence)
{
    uint16_t src_frame = 0;
    while (src_frame < frames) {
        g_speaker_input_frame[g_speaker_input_frames * kSpeakerOpusChannels] =
            std::clamp(static_cast<float>(
                           samples[src_frame * kSpeakerUsbChannels]) /
                           32768.0f * g_speaker_gain,
                       -1.0f, 1.0f);
        g_speaker_input_frame[g_speaker_input_frames * kSpeakerOpusChannels +
                              1u] =
            std::clamp(static_cast<float>(
                           samples[src_frame * kSpeakerUsbChannels + 1u]) /
                           32768.0f * g_speaker_gain,
                       -1.0f, 1.0f);
        ++g_speaker_input_frames;
        ++src_frame;

        if (g_speaker_input_frames == kSpeakerInputFrameSamples) {
            resample_speaker_frame_512_to_480();
            (void)send_speaker_report(state, report_sequence);
            g_speaker_input_frames = 0;
        }
    }
}

void __not_in_flash_func(service_speaker)(const DualSenseOutputState &state,
                                          uint8_t &report_sequence)
{
    if (!g_speaker_streaming || !g_headphones_connected ||
        usb_descriptors_audio_persona() != UsbAudioPersona::Headset ||
        !dualsense_bt_is_connected()) {
        drain_speaker_audio();
        return;
    }

    if (tud_audio_available() == 0) {
        if (g_last_speaker_audio_ms != 0 &&
            board_millis() - g_last_speaker_audio_ms > kSpeakerStreamTimeoutMs) {
            g_speaker_input_frames = 0;
            g_last_speaker_audio_ms = 0;
        }
        return;
    }

    for (uint8_t burst = 0; burst < kSpeakerReadBurstsPerLoop; ++burst) {
        int16_t raw[256]{};
        const uint16_t bytes_read = tud_audio_read(raw, sizeof(raw));
        const uint16_t frames = static_cast<uint16_t>(
            bytes_read / (kSpeakerUsbChannels * sizeof(int16_t)));
        if (frames == 0) {
            break;
        }

        g_last_speaker_audio_ms = board_millis();
        append_speaker_usb_frames(raw, frames, state, report_sequence);

        if (tud_audio_available() == 0) {
            break;
        }
    }
}

void __not_in_flash_func(service_mic)()
{
    if (!mic_enabled() || g_decoder == nullptr) {
        clear_mic_queue();
        return;
    }

    MicPacket packet{};
    if (!pop_mic_packet(packet)) {
        return;
    }

    std::array<int16_t, kOpusFrameSamples> mono{};
    const int decoded = opus_decode(g_decoder, packet.data.data(), kMicOpusBytes,
                                    mono.data(), kOpusFrameSamples, 0);
    if (decoded <= 0) {
        return;
    }

    std::array<uint32_t, kOpusFrameSamples> stereo{};
    for (int i = 0; i < decoded; ++i) {
        const uint16_t sample = static_cast<uint16_t>(mono[static_cast<std::size_t>(i)]);
        stereo[static_cast<std::size_t>(i)] =
            static_cast<uint32_t>(sample) |
            (static_cast<uint32_t>(sample) << 16u);
    }

    const uint16_t bytes =
        static_cast<uint16_t>(decoded * static_cast<int>(sizeof(uint32_t)));
    (void)tud_audio_write(stereo.data(), bytes);
}

uint8_t entity_index(uint8_t entity)
{
    return entity == kSpeakerFeatureUnit ? 0 : 1;
}

bool handle_get_entity(uint8_t rhport, tusb_control_request_t const *request)
{
    const uint8_t channel = TU_U16_LOW(request->wValue);
    const uint8_t control = TU_U16_HIGH(request->wValue);
    const uint8_t entity = TU_U16_HIGH(request->wIndex);

    if ((entity != kSpeakerFeatureUnit && entity != kMicFeatureUnit) ||
        channel != 0) {
        return false;
    }

    const uint8_t index = entity_index(entity);
    switch (control) {
    case AUDIO10_FU_CTRL_MUTE:
        return tud_audio_buffer_and_schedule_control_xfer(
            rhport, request, &g_mute[index], sizeof(g_mute[index]));
    case AUDIO10_FU_CTRL_VOLUME:
        switch (request->bRequest) {
        case AUDIO10_CS_REQ_GET_CUR:
            return tud_audio_buffer_and_schedule_control_xfer(
                rhport, request, &g_volume[index], sizeof(g_volume[index]));
        case AUDIO10_CS_REQ_GET_MIN: {
            static int16_t min_volume[2] = {-15360, 0};
            return tud_audio_buffer_and_schedule_control_xfer(
                rhport, request, &min_volume[index], sizeof(min_volume[index]));
        }
        case AUDIO10_CS_REQ_GET_MAX: {
            static int16_t max_volume[2] = {0, 12288};
            return tud_audio_buffer_and_schedule_control_xfer(
                rhport, request, &max_volume[index], sizeof(max_volume[index]));
        }
        case AUDIO10_CS_REQ_GET_RES: {
            static int16_t volume_step[2] = {256, 122};
            return tud_audio_buffer_and_schedule_control_xfer(
                rhport, request, &volume_step[index],
                sizeof(volume_step[index]));
        }
        default:
            return false;
        }
    default:
        return false;
    }
}

bool handle_set_entity(tusb_control_request_t const *request, uint8_t *buffer)
{
    const uint8_t channel = TU_U16_LOW(request->wValue);
    const uint8_t control = TU_U16_HIGH(request->wValue);
    const uint8_t entity = TU_U16_HIGH(request->wIndex);

    if ((entity != kSpeakerFeatureUnit && entity != kMicFeatureUnit) ||
        channel != 0 || buffer == nullptr ||
        request->bRequest != AUDIO10_CS_REQ_SET_CUR) {
        return false;
    }

    const uint8_t index = entity_index(entity);
    switch (control) {
    case AUDIO10_FU_CTRL_MUTE:
        if (request->wLength != 1) {
            return false;
        }
        g_mute[index] = buffer[0];
        if (index == 0) {
            recompute_speaker_gain();
        }
        return true;
    case AUDIO10_FU_CTRL_VOLUME:
        if (request->wLength != 2) {
            return false;
        }
        std::memcpy(&g_volume[index], buffer, sizeof(g_volume[index]));
        if (index == 0) {
            recompute_speaker_gain();
        }
        return true;
    default:
        return false;
    }
}

bool set_audio_interface(uint8_t interface, uint8_t alternate)
{
    const UsbAudioPersona persona = usb_descriptors_audio_persona();
    if (persona == UsbAudioPersona::Headset &&
        interface == kSpeakerInterface) {
        g_speaker_streaming = alternate != 0;
        if (!g_speaker_streaming) {
            drain_speaker_audio();
        }
        return true;
    }

    const bool mic_interface =
        (persona == UsbAudioPersona::Headset &&
         interface == kHeadsetMicInterface) ||
        (persona == UsbAudioPersona::MicOnly && interface == kMicOnlyInterface);
    if (mic_interface) {
        const bool new_streaming = alternate != 0;
        if (g_mic_streaming != new_streaming) {
            g_mic_streaming = new_streaming;
            if (!g_mic_streaming) {
                clear_mic_queue();
            }
            (void)send_audio_status_report();
        }
        return true;
    }

    return true;
}

} // namespace

void dualsense_audio_init()
{
    int error = 0;
    g_encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_RESTRICTED_LOWDELAY,
                                    &error);
    if (g_encoder != nullptr && error == OPUS_OK) {
        opus_encoder_ctl(g_encoder,
                         OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_10_MS));
        opus_encoder_ctl(g_encoder, OPUS_SET_BITRATE(160000));
        opus_encoder_ctl(g_encoder, OPUS_SET_VBR(0));
        opus_encoder_ctl(g_encoder, OPUS_SET_COMPLEXITY(0));
    } else {
        g_encoder = nullptr;
    }

    error = 0;
    g_decoder = opus_decoder_create(48000, 1, &error);
    if (g_decoder == nullptr || error != OPUS_OK) {
        g_decoder = nullptr;
    }
}

void __not_in_flash_func(dualsense_audio_set_connected)(bool connected)
{
    g_connected = connected;
    if (!connected) {
        g_headphones_connected = false;
        g_mic_connected = false;
        g_mic_muted = false;
        g_external_mic_active = false;
        g_speaker_streaming = false;
        g_mic_streaming = false;
        g_speaker_input_frames = 0;
        clear_mic_queue();
    }
}

void __not_in_flash_func(dualsense_audio_update_status)(
    const DualSenseInput &input)
{
    g_connected = true;
    g_headphones_connected = input.headphones_connected;
    g_mic_connected = input.mic_connected;
    g_mic_muted = input.mic_muted;
    g_external_mic_active = input.external_mic_active;
}

void __not_in_flash_func(dualsense_audio_add_mic_frame)(const uint8_t *data,
                                                        std::size_t size)
{
    if (!mic_enabled() || data == nullptr || size < kMicOpusBytes) {
        return;
    }

    push_mic_packet(data);
}

UsbAudioPersona __not_in_flash_func(dualsense_audio_desired_persona)()
{
    if (!g_connected) {
        return UsbAudioPersona::HidOnly;
    }
    if (g_headphones_connected) {
        return UsbAudioPersona::Headset;
    }
    return UsbAudioPersona::MicOnly;
}

bool __not_in_flash_func(dualsense_audio_speaker_streaming)()
{
    return g_speaker_streaming && g_headphones_connected;
}

bool __not_in_flash_func(dualsense_audio_speaker_active)()
{
    return dualsense_audio_speaker_streaming() &&
           g_last_speaker_audio_ms != 0 &&
           board_millis() - g_last_speaker_audio_ms < 40u;
}

void __not_in_flash_func(dualsense_audio_loop)(const DualSenseOutputState &state,
                                               uint8_t &sequence)
{
    service_speaker(state, sequence);
    service_mic();
}

} // namespace ds5onswitch

extern "C" bool tud_audio_set_itf_cb(uint8_t rhport,
                                      tusb_control_request_t const *request)
{
    (void)rhport;
    return ds5onswitch::set_audio_interface(TU_U16_LOW(request->wIndex),
                                            TU_U16_LOW(request->wValue));
}

extern "C" bool tud_audio_get_req_entity_cb(
    uint8_t rhport, tusb_control_request_t const *request)
{
    return ds5onswitch::handle_get_entity(rhport, request);
}

extern "C" bool tud_audio_set_req_entity_cb(
    uint8_t rhport, tusb_control_request_t const *request, uint8_t *buffer)
{
    (void)rhport;
    return ds5onswitch::handle_set_entity(request, buffer);
}
