#pragma once

#include "ds5onswitch/types.h"

namespace ds5onswitch {

struct MapperConfig {
    uint8_t trigger_button_threshold = 32;

    // The nRF5340 Switch Pro implementation maps source motion as:
    // accel=(Y,-X,Z), gyro=(Y,-X,Z). Keep this default until hardware
    // traces prove a DualSense-specific correction is needed.
    bool use_nrf5340_switch_motion_axes = true;
};

uint16_t axis8_to_switch12(uint8_t value);
int16_t dualsense_accel_to_switch(int16_t value);
int16_t dualsense_gyro_to_switch(int16_t value);
SwitchProState map_dualsense_to_switch(const DualSenseInput &input,
                                        MapperConfig config = {});

} // namespace ds5onswitch
