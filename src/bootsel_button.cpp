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

constexpr uint32_t kPollIntervalMs = 100;
constexpr int kHoldSamples = 15;
constexpr int kClickWindowSamples = 5;
constexpr int kClearFlashToggles = 12;
constexpr uint32_t kClearFlashIntervalMs = 100;

enum class ButtonState {
    Idle,
    Pressing,
    Held,
    WaitForNextClick,
};

ButtonState g_state = ButtonState::Idle;
int g_press_samples = 0;
int g_wait_samples = 0;
int g_click_count = 0;
uint32_t g_last_check_ms = 0;
bool g_solid_led_enabled = true;
int g_clear_flash_toggles_remaining = 0;
uint32_t g_clear_flash_last_toggle_ms = 0;
bool g_clear_flash_led = false;

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
    std::printf("[BTN] BOOTSEL double click - reboot\n");
    *reinterpret_cast<volatile uint32_t *>(0xe000ed0c) = 0x05fa0004;
    __dsb();
    while (true) {
        tight_loop_contents();
    }
}

void reboot_to_bootsel()
{
    std::printf("[BTN] BOOTSEL triple click - reboot to BOOTSEL\n");
    reset_usb_boot(0, 0);
}

void start_clear_flash()
{
    g_clear_flash_toggles_remaining = kClearFlashToggles;
    g_clear_flash_last_toggle_ms = to_ms_since_boot(get_absolute_time());
    g_clear_flash_led = false;
}

void dispatch_clicks(int clicks)
{
    if (clicks <= 1) {
        g_solid_led_enabled = !g_solid_led_enabled;
        std::printf("[BTN] BOOTSEL click - solid LED %s\n",
                    g_solid_led_enabled ? "on" : "off");
    } else if (clicks == 2) {
        reboot_to_app();
    } else {
        reboot_to_bootsel();
    }
}

} // namespace

void bootsel_button_task()
{
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - g_last_check_ms < kPollIntervalMs) {
        return;
    }
    g_last_check_ms = now;

    dualsense_bt_persist_blacklist_if_dirty();

    const bool pressed = read_bootsel();
    switch (g_state) {
    case ButtonState::Idle:
        if (pressed) {
            g_state = ButtonState::Pressing;
            g_press_samples = 1;
        }
        break;
    case ButtonState::Pressing:
        if (pressed) {
            if (++g_press_samples >= kHoldSamples) {
                g_click_count = 0;
                g_state = ButtonState::Held;
                dualsense_bt_forget_pairings();
                start_clear_flash();
            }
        } else {
            ++g_click_count;
            g_state = ButtonState::WaitForNextClick;
            g_wait_samples = 0;
        }
        break;
    case ButtonState::Held:
        if (!pressed) {
            g_state = ButtonState::Idle;
            g_press_samples = 0;
        }
        break;
    case ButtonState::WaitForNextClick:
        if (pressed) {
            g_state = ButtonState::Pressing;
            g_press_samples = 1;
        } else if (++g_wait_samples >= kClickWindowSamples) {
            const int clicks = g_click_count;
            g_click_count = 0;
            g_state = ButtonState::Idle;
            g_press_samples = 0;
            dispatch_clicks(clicks);
        }
        break;
    }
}

bool bootsel_button_solid_led_enabled()
{
    return g_solid_led_enabled;
}

bool bootsel_button_led_override(bool &enabled)
{
    if (g_clear_flash_toggles_remaining <= 0) {
        return false;
    }

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - g_clear_flash_last_toggle_ms >= kClearFlashIntervalMs) {
        g_clear_flash_last_toggle_ms = now;
        g_clear_flash_led = !g_clear_flash_led;
        --g_clear_flash_toggles_remaining;
    }

    enabled = g_clear_flash_led;
    return true;
}

} // namespace ds5onswitch
