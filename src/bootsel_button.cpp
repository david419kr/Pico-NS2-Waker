#include "ds5onswitch/bootsel_button.h"

#include "ds5onswitch/dualsense_bt.h"

#include "hardware/gpio.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
#include "pico/platform.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#if PICO_RP2350
#include "hardware/regs/sio.h"
#endif

#include <cstdio>

namespace ds5onswitch {
namespace {

// Gesture thresholds, sampled frequently enough that a held BOOTSEL button can
// gate off BT/USB work before QSPI CSn and active Bluetooth traffic collide.
constexpr uint32_t POLL_INTERVAL_MS = 20;
constexpr int HOLD_SAMPLES = 1500 / POLL_INTERVAL_MS;
constexpr int CLICK_WINDOW_SAMPLES = 500 / POLL_INTERVAL_MS;

// FSM: 0 idle, 1 pressing (counting for hold), 2 held (fired), 3 wait-for-next-click.
int button_fsm = 0;
int button_press_samples = 0;
int button_wait_samples = 0;
int button_click_count = 0;
uint32_t button_last_check_ms = 0;
bool button_hold_pending = false;
bool button_normal_led_enabled = true;

int button_clear_flash_toggles_remaining = 0;
uint32_t button_clear_flash_last_toggle_ms = 0;
bool button_clear_flash_led = false;

void __no_inline_not_in_flash_func(read_bootsel_cb)(void *param)
{
    bool *pressed = static_cast<bool *>(param);
    constexpr uint kCsPinIndex = 1;

    hw_write_masked(&ioqspi_hw->io[kCsPinIndex].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    for (int i = 0; i < 1000; ++i) {
        __asm volatile("nop");
    }

#if PICO_RP2350
    *pressed = (sio_hw->gpio_hi_in & SIO_GPIO_HI_IN_QSPI_CSN_BITS) == 0;
#else
    *pressed = (sio_hw->gpio_hi_in & (1u << kCsPinIndex)) == 0;
#endif

    hw_write_masked(&ioqspi_hw->io[kCsPinIndex].ctrl,
                    GPIO_OVERRIDE_NORMAL
                        << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
}

bool read_bootsel()
{
    bool pressed = false;
    const int rc = flash_safe_execute(read_bootsel_cb, &pressed, 100);
    return rc == PICO_OK && pressed;
}

void reboot_to_app()
{
    std::printf("[BTN] BOOTSEL triple click - reboot\n");
    *reinterpret_cast<volatile uint32_t *>(0xe000ed0c) = 0x05fa0004;
    __dsb();
    while (true) {
        tight_loop_contents();
    }
}

void reboot_to_bootsel()
{
    std::printf("[BTN] BOOTSEL quadruple click - reboot to BOOTSEL\n");
    reset_usb_boot(0, 0);
}

void button_toggle_normal_led()
{
    button_normal_led_enabled = !button_normal_led_enabled;
    std::printf("[BTN] BOOTSEL single click - normal LED %s\n",
                button_normal_led_enabled ? "enabled" : "disabled");
}

void button_start_clear_flash()
{
    button_clear_flash_toggles_remaining = 12;
    button_clear_flash_last_toggle_ms = to_ms_since_boot(get_absolute_time());
    button_clear_flash_led = false;
}

// Act on a completed click sequence once the inter-click window closes.
void button_dispatch(int clicks)
{
    if (clicks <= 1) {
        button_toggle_normal_led();
    } else if (clicks == 2) {
        dualsense_bt_bootsel_click_action(); // double click -> pair / switch controller
    } else if (clicks == 3) {
        reboot_to_app();
    } else if (clicks == 4) {
        reboot_to_bootsel();
    } else {
        std::printf("[BTN] BOOTSEL %d clicks - no action\n", clicks);
    }
}

} // namespace

void __not_in_flash_func(bootsel_button_task)()
{
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - button_last_check_ms < POLL_INTERVAL_MS) {
        return;
    }
    button_last_check_ms = now;

    const bool pressed = read_bootsel();
    switch (button_fsm) {
    case 0: // IDLE - wait for the first press
        if (pressed) {
            button_fsm = 1;
            button_press_samples = 1;
            button_hold_pending = false;
        }
        break;
    case 1: // PRESSING - counting samples for hold
        if (pressed) {
            if (++button_press_samples >= HOLD_SAMPLES) {
                button_click_count = 0;
                button_fsm = 2;
                button_hold_pending = true;
            }
        } else {
            // released before the hold threshold -> count this click and
            // wait to see whether more clicks follow
            ++button_click_count;
            button_fsm = 3;
            button_wait_samples = 0;
        }
        break;
    case 2: // HELD - hold already fired, wait for release
        if (!pressed) {
            button_fsm = 0;
            button_press_samples = 0;
            if (button_hold_pending) {
                button_hold_pending = false;
                dualsense_bt_forget_pairings();
                button_start_clear_flash();
            }
        }
        break;
    case 3: // WAIT - released; watch for the next press inside the window
        if (pressed) {
            button_fsm = 1;
            button_press_samples = 1;
        } else if (++button_wait_samples >= CLICK_WINDOW_SAMPLES) {
            const int clicks = button_click_count;
            button_click_count = 0;
            button_fsm = 0;
            button_press_samples = 0;
            button_dispatch(clicks);
        }
        break;
    }
}

bool __not_in_flash_func(bootsel_button_interaction_active)()
{
    return button_fsm != 0;
}

bool __not_in_flash_func(bootsel_button_normal_led_enabled)()
{
    return button_normal_led_enabled;
}

bool bootsel_button_led_override(bool &enabled)
{
    if (button_clear_flash_toggles_remaining <= 0) {
        return false;
    }

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - button_clear_flash_last_toggle_ms >= 100) {
        button_clear_flash_last_toggle_ms = now;
        button_clear_flash_led = !button_clear_flash_led;
        --button_clear_flash_toggles_remaining;
    }

    enabled = button_clear_flash_led;
    return true;
}

} // namespace ds5onswitch
