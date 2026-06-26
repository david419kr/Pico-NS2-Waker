#pragma once

#include <array>
#include <cstdint>

namespace ds5onswitch {

using SwitchRumblePacket = std::array<uint8_t, 4>;

struct DualSenseHapticCommand {
    uint8_t left_motor = 0;
    uint8_t right_motor = 0;
    uint8_t left_pulse = 0;
    uint8_t right_pulse = 0;
    bool active = false;
};

class SwitchRumbleTranslator {
public:
    DualSenseHapticCommand translate(const SwitchRumblePacket &left,
                                     const SwitchRumblePacket &right);
    void reset();

private:
    SwitchRumblePacket previous_left_{};
    SwitchRumblePacket previous_right_{};
    bool has_previous_ = false;
};

bool switch_rumble_packet_is_silent(const SwitchRumblePacket &packet);
uint32_t switch_rumble_amplitude_q15(const SwitchRumblePacket &packet);
uint32_t switch_rumble_texture_q15(const SwitchRumblePacket &packet);

} // namespace ds5onswitch
