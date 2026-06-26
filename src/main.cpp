#include "ds5onswitch/haptics.h"
#include "ds5onswitch/mapper.h"
#include "ds5onswitch/switch_pro.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

using namespace ds5onswitch;

namespace {

SwitchProInputReport make_neutral_report(uint8_t timer)
{
    DualSenseInput neutral{};
    neutral.has_imu = true;
    const SwitchProState state = map_dualsense_to_switch(neutral);
    return build_switch_pro_input_report(state, timer, false);
}

} // namespace

int main()
{
    stdio_init_all();
    if (cyw43_arch_init() != 0) {
        return 1;
    }

    uint8_t timer = 0;
    while (true) {
        const SwitchProInputReport report = make_neutral_report(timer++);
        (void)report;

        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
        sleep_ms(80);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
        sleep_ms(920);
    }
}
