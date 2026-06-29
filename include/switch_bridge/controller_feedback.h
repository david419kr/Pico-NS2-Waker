#pragma once

#include <cstdint>

namespace switchbridge {

enum class ControllerFeedbackCue : uint8_t {
    Connected,
    WakeDisconnect,
};

void controller_feedback_play(ControllerFeedbackCue cue);
void controller_feedback_service(uint32_t now_ms);

} // namespace switchbridge
