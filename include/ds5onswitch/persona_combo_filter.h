#pragma once

#include "ds5onswitch/types.h"

#include <cstdint>

namespace ds5onswitch {

enum class PersonaComboRequest : uint8_t {
    None,
    SwitchPro,
    Switch2Pro,
};

struct PersonaComboFilterResult {
    uint32_t buttons = 0;
    PersonaComboRequest request = PersonaComboRequest::None;
};

class PersonaComboFilter {
public:
    PersonaComboFilterResult process(uint32_t raw_buttons, uint32_t now_ms);
    void reset();

private:
    bool ps_prev_ = false;
    bool tp_prev_ = false;
    bool suppress_tp_until_release_ = false;
    uint8_t tap_count_ = 0;
};

} // namespace ds5onswitch
