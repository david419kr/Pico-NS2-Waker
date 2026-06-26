#include "ds5onswitch/haptics.h"

#include <algorithm>
#include <cmath>

namespace ds5onswitch {

namespace {

constexpr uint32_t kQ15One = 32768u;

uint32_t q15_clamp(uint32_t value)
{
    return std::min(value, kQ15One);
}

uint32_t q15_weight(uint32_t value, uint32_t percent)
{
    return (value * percent + 50u) / 100u;
}

uint8_t q15_to_u8(uint32_t value)
{
    value = q15_clamp(value);
    return static_cast<uint8_t>((value * 255u + kQ15One / 2u) / kQ15One);
}

uint32_t q15_sqrt_mix(uint32_t value)
{
    value = q15_clamp(value);
    const double unit = static_cast<double>(value) / static_cast<double>(kQ15One);
    return static_cast<uint32_t>(std::sqrt(unit) * static_cast<double>(kQ15One) + 0.5);
}

uint32_t packet_delta_q15(const SwitchRumblePacket &current,
                          const SwitchRumblePacket &previous,
                          bool has_previous)
{
    if (!has_previous) {
        return switch_rumble_amplitude_q15(current);
    }

    uint32_t sum = 0;
    uint32_t peak = 0;
    for (std::size_t i = 0; i < current.size(); ++i) {
        const uint32_t diff = current[i] > previous[i] ?
            static_cast<uint32_t>(current[i] - previous[i]) :
            static_cast<uint32_t>(previous[i] - current[i]);
        sum += diff;
        peak = std::max(peak, diff);
    }

    const uint32_t sum_unit = (sum * kQ15One + 510u) / 1020u;
    const uint32_t peak_unit = (peak * kQ15One + 127u) / 255u;
    return q15_clamp(q15_weight(sum_unit, 70u) + q15_weight(peak_unit, 30u));
}

uint32_t body_unit_q15(uint32_t amplitude)
{
    constexpr uint32_t threshold = (kQ15One * 74u + 50u) / 100u;
    if (amplitude <= threshold) {
        return 0;
    }

    const uint32_t range = kQ15One - threshold;
    const uint32_t unit = ((amplitude - threshold) * kQ15One + range / 2u) / range;
    return q15_weight(q15_sqrt_mix(unit), 34u);
}

uint32_t pulse_unit_q15(uint32_t amplitude, uint32_t texture, uint32_t delta)
{
    uint32_t amp_tail = 0;
    constexpr uint32_t amp_floor = (kQ15One * 18u + 50u) / 100u;
    if (amplitude > amp_floor) {
        amp_tail = amplitude - amp_floor;
    }

    return q15_clamp(q15_weight(delta, 160u) +
                     q15_weight(amp_tail, 28u) +
                     q15_weight(texture, 8u));
}

} // namespace

bool switch_rumble_packet_is_silent(const SwitchRumblePacket &packet)
{
    const bool all_zero =
        packet[0] == 0 && packet[1] == 0 && packet[2] == 0 && packet[3] == 0;
    const bool neutral =
        packet[0] == 0x00 && packet[1] == 0x01 &&
        packet[2] == 0x40 && packet[3] == 0x40;
    return all_zero || neutral;
}

uint32_t switch_rumble_amplitude_q15(const SwitchRumblePacket &packet)
{
    if (switch_rumble_packet_is_silent(packet)) {
        return 0;
    }

    const uint32_t high = ((packet[1] & 0xfeu) * kQ15One + 127u) / 254u;
    const uint32_t low = (packet[3] * kQ15One + 127u) / 255u;
    return q15_clamp(q15_weight(high, 78u) + q15_weight(low, 22u));
}

uint32_t switch_rumble_texture_q15(const SwitchRumblePacket &packet)
{
    if (switch_rumble_packet_is_silent(packet)) {
        return 0;
    }

    const uint32_t high_freq = (static_cast<uint32_t>(packet[0]) << 1u) |
                               (packet[1] & 0x01u);
    const uint32_t low_freq = packet[2] & 0x7fu;
    const uint32_t high_unit = (high_freq * kQ15One + 255u) / 511u;
    const uint32_t low_unit = (low_freq * kQ15One + 63u) / 127u;
    return q15_clamp(q15_weight(high_unit, 65u) + q15_weight(low_unit, 35u));
}

DualSenseHapticCommand SwitchRumbleTranslator::translate(const SwitchRumblePacket &left,
                                                         const SwitchRumblePacket &right)
{
    const bool left_silent = switch_rumble_packet_is_silent(left);
    const bool right_silent = switch_rumble_packet_is_silent(right);

    const uint32_t left_amp = switch_rumble_amplitude_q15(left);
    const uint32_t right_amp = switch_rumble_amplitude_q15(right);
    const uint32_t left_texture = switch_rumble_texture_q15(left);
    const uint32_t right_texture = switch_rumble_texture_q15(right);
    const uint32_t left_delta = packet_delta_q15(left, previous_left_, has_previous_);
    const uint32_t right_delta = packet_delta_q15(right, previous_right_, has_previous_);

    previous_left_ = left;
    previous_right_ = right;
    has_previous_ = true;

    DualSenseHapticCommand out{};
    if (!left_silent) {
        out.left_motor = q15_to_u8(body_unit_q15(left_amp));
        out.left_pulse = q15_to_u8(pulse_unit_q15(left_amp, left_texture, left_delta));
    }
    if (!right_silent) {
        out.right_motor = q15_to_u8(body_unit_q15(right_amp));
        out.right_pulse = q15_to_u8(pulse_unit_q15(right_amp, right_texture, right_delta));
    }

    out.active = out.left_motor != 0 || out.right_motor != 0 ||
                 out.left_pulse != 0 || out.right_pulse != 0;
    return out;
}

void SwitchRumbleTranslator::reset()
{
    previous_left_.fill(0);
    previous_right_.fill(0);
    has_previous_ = false;
}

} // namespace ds5onswitch
