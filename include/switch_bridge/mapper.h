#pragma once

#include "switch_bridge/types.h"

namespace switchbridge {

struct MapperConfig {
    uint8_t trigger_button_threshold = 32;

    // nRF5340 Steam-source parity through its PlayStation and Switch Pro paths:
    // accel=(-Z,-X,Y), gyro=(-Y,-X,Z) for parsed PlayStation raw fields.
    bool use_nrf5340_switch_motion_axes = true;
};

uint16_t axis8_to_switch12(uint8_t value);
int16_t playstation_accel_to_switch(int16_t value);
int16_t playstation_gyro_to_switch(int16_t value);
SwitchProState map_playstation_to_switch(const PlayStationInput &input,
                                        MapperConfig config = {});

} // namespace switchbridge
