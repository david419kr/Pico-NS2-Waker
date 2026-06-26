#pragma once

#include <cstdint>

namespace ds5onswitch {

enum class UsbAudioPersona : uint8_t {
    HidOnly = 0,
    MicOnly = 1,
    Headset = 2,
};

void usb_descriptors_set_audio_persona(UsbAudioPersona persona);
UsbAudioPersona usb_descriptors_audio_persona();

} // namespace ds5onswitch
