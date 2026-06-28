#pragma once

#include <cstdint>

namespace ds5onswitch {

enum class UsbAudioPersona : uint8_t {
    HidOnly = 0,
    MicOnly = 1,
    Headset = 2,
};

enum class UsbControllerPersona : uint8_t {
    SwitchPro = 0,
    Switch2ProRegistration = 1,
};

void usb_descriptors_set_controller_persona(UsbControllerPersona persona);
UsbControllerPersona usb_descriptors_controller_persona();

void usb_descriptors_set_audio_persona(UsbAudioPersona persona);
UsbAudioPersona usb_descriptors_audio_persona();

} // namespace ds5onswitch
