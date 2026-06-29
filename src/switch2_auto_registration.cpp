#include "switch_bridge/switch2_auto_registration.h"

namespace switchbridge {

namespace {

constexpr uint32_t kConsoleSeenWindowMs = 30000;
constexpr uint32_t kArmDelayMs = 250;
constexpr uint32_t kEnumerationGraceMs = 3000;
constexpr uint32_t kFinalizeGraceMs = 250;
constexpr uint32_t kRegistrationTimeoutMs = 12000;
constexpr uint32_t kCooldownMs = 10u * 60u * 1000u;

bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

bool elapsed_at_least(uint32_t now_ms, uint32_t start_ms, uint32_t delta_ms)
{
    return time_reached(now_ms, start_ms + delta_ms);
}

bool base_conditions_ok(const Switch2AutoRegistrationInputs &inputs)
{
    return inputs.controller_connected && !inputs.debug_usb_mode;
}

} // namespace

void Switch2AutoRegistration::reset()
{
    enter_idle();
}

void Switch2AutoRegistration::note_switch_console_seen(uint32_t now_ms)
{
    console_seen_ = true;
    console_seen_ms_ = now_ms;
}

void Switch2AutoRegistration::note_pairing_finalized()
{
    pairing_finalized_ = true;
}

Switch2AutoRegistrationAction Switch2AutoRegistration::service(
    const Switch2AutoRegistrationInputs &inputs)
{
    if (console_seen_ &&
        elapsed_at_least(inputs.now_ms, console_seen_ms_,
                         kConsoleSeenWindowMs)) {
        console_seen_ = false;
    }

    if (inputs.registration_complete &&
        (state_ == State::Idle || state_ == State::Cooldown)) {
        enter_idle();
        return Switch2AutoRegistrationAction::None;
    }

    if (!base_conditions_ok(inputs)) {
        if (inputs.switch2_persona_active &&
            state_ != State::Idle &&
            state_ != State::Cooldown) {
            enter_cooldown(inputs.now_ms);
            return Switch2AutoRegistrationAction::RevertToSwitchProPersona;
        }
        enter_idle();
        return Switch2AutoRegistrationAction::None;
    }

    switch (state_) {
    case State::Idle:
        if (console_seen_ && inputs.usb_mounted &&
            !inputs.usb_reconnect_pending && !inputs.switch2_persona_active) {
            state_ = State::Armed;
            state_ms_ = inputs.now_ms;
        }
        break;

    case State::Armed:
        if (!console_seen_ || !inputs.usb_mounted ||
            inputs.usb_reconnect_pending || inputs.switch2_persona_active) {
            enter_idle();
            break;
        }
        if (elapsed_at_least(inputs.now_ms, state_ms_, kArmDelayMs)) {
            state_ = State::EnteringRegistration;
            state_ms_ = inputs.now_ms;
            pairing_finalized_ = false;
            return Switch2AutoRegistrationAction::
                EnterSwitch2RegistrationPersona;
        }
        break;

    case State::EnteringRegistration:
        if (inputs.switch2_persona_active && inputs.usb_mounted &&
            !inputs.usb_reconnect_pending) {
            state_ = State::RegistrationActive;
            state_ms_ = inputs.now_ms;
            break;
        }
        if (elapsed_at_least(inputs.now_ms, state_ms_,
                             kEnumerationGraceMs)) {
            enter_cooldown(inputs.now_ms);
            return Switch2AutoRegistrationAction::RevertToSwitchProPersona;
        }
        break;

    case State::RegistrationActive:
        if (pairing_finalized_) {
            if (finalized_ms_ == 0) {
                finalized_ms_ = inputs.now_ms;
            }
            if (elapsed_at_least(inputs.now_ms, finalized_ms_,
                                 kFinalizeGraceMs)) {
                enter_cooldown(inputs.now_ms);
                return Switch2AutoRegistrationAction::
                    RevertToSwitchProPersona;
            }
            break;
        }
        if (!inputs.switch2_persona_active ||
            elapsed_at_least(inputs.now_ms, state_ms_,
                             kRegistrationTimeoutMs)) {
            enter_cooldown(inputs.now_ms);
            return Switch2AutoRegistrationAction::RevertToSwitchProPersona;
        }
        if (!inputs.usb_mounted && !inputs.usb_reconnect_pending &&
            elapsed_at_least(inputs.now_ms, state_ms_,
                             kEnumerationGraceMs)) {
            enter_cooldown(inputs.now_ms);
            return Switch2AutoRegistrationAction::RevertToSwitchProPersona;
        }
        break;

    case State::Cooldown:
        if (time_reached(inputs.now_ms, state_ms_ + kCooldownMs)) {
            enter_idle();
        }
        break;
    }

    return Switch2AutoRegistrationAction::None;
}

bool Switch2AutoRegistration::active() const
{
    return state_ == State::Armed ||
           state_ == State::EnteringRegistration ||
           state_ == State::RegistrationActive;
}

bool Switch2AutoRegistration::blocks_audio_reconnect() const
{
    return active();
}

void Switch2AutoRegistration::enter_idle()
{
    state_ = State::Idle;
    state_ms_ = 0;
    console_seen_ = false;
    console_seen_ms_ = 0;
    finalized_ms_ = 0;
    pairing_finalized_ = false;
}

void Switch2AutoRegistration::enter_cooldown(uint32_t now_ms)
{
    state_ = State::Cooldown;
    state_ms_ = now_ms;
    console_seen_ = false;
    console_seen_ms_ = 0;
    finalized_ms_ = 0;
    pairing_finalized_ = false;
}

} // namespace switchbridge
