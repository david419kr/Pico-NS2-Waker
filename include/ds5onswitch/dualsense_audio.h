#pragma once

#include "ds5onswitch/dualsense_output.h"
#include "ds5onswitch/types.h"
#include "ds5onswitch/usb_descriptors.h"

#include <cstddef>
#include <cstdint>

namespace ds5onswitch {

void dualsense_audio_init();
void dualsense_audio_set_connected(bool connected);
void dualsense_audio_update_status(const DualSenseInput &input);
void dualsense_audio_add_mic_frame(const uint8_t *data, std::size_t size);
void dualsense_audio_set_mic_muted(bool muted);
bool dualsense_audio_toggle_mic_muted();
bool dualsense_audio_mic_muted();
UsbAudioPersona dualsense_audio_desired_persona();
bool dualsense_audio_speaker_streaming();
bool dualsense_audio_speaker_active();
void dualsense_audio_loop(const DualSenseOutputState &state, uint8_t &sequence);

} // namespace ds5onswitch
