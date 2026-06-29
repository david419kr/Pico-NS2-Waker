#pragma once

#include <cstdint>

namespace switchbridge {

enum class Switch2AutoRegistrationAction : uint8_t {
    None,
    EnterSwitch2RegistrationPersona,
    RevertToSwitchProPersona,
};

struct Switch2AutoRegistrationInputs {
    uint32_t now_ms = 0;
    bool usb_mounted = false;
    bool controller_connected = false;
    bool debug_usb_mode = false;
    bool usb_reconnect_pending = false;
    bool switch2_persona_active = false;
    bool registration_complete = false;
};

class Switch2AutoRegistration {
public:
    void reset();
    void note_switch_console_seen(uint32_t now_ms);
    void note_pairing_finalized();

    Switch2AutoRegistrationAction service(
        const Switch2AutoRegistrationInputs &inputs);

    bool active() const;
    bool blocks_audio_reconnect() const;

private:
    enum class State : uint8_t {
        Idle,
        Armed,
        EnteringRegistration,
        RegistrationActive,
        Cooldown,
    };

    State state_ = State::Idle;
    uint32_t state_ms_ = 0;
    uint32_t console_seen_ms_ = 0;
    uint32_t finalized_ms_ = 0;
    bool console_seen_ = false;
    bool pairing_finalized_ = false;

    void enter_idle();
    void enter_cooldown(uint32_t now_ms);
};

} // namespace switchbridge
