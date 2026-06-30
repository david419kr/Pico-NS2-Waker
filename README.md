[한국어 README](README.ko.md)

# "Wake Switch 2 with Any Modern Controllers" Dongle

Firmware for using a DualSense, DualShock 4, Nintendo Switch Pro Controller 1/2, Steam Controller, or Xbox Bluetooth controller on Nintendo Switch 1/2 with a Raspberry Pi Pico 2 W.  
Any supported controller can wake a Nintendo Switch 2 from sleep. (※Switch 1 wake is not supported.)

Credits:  
[ataradov/usb-sniffer](https://github.com/ataradov/usb-sniffer) and [homewsn/blesniff](https://github.com/homewsn/blesniff) were a great help in analyzing controller behavior.  
This project is heavily inspired by [awalol/DS5Dongle](https://github.com/awalol/DS5Dongle) and references its DualSense implementation extensively.

## What It Does

- Makes each controller appear as a `Nintendo Switch Pro Controller`.
- Can wake a Nintendo Switch 2 from sleep.
- Supports motion controls and rumble. Xbox controllers do not support motion controls.
- Lets you use the DualSense 3.5 mm jack on Switch 1/2. You can also use the DualSense microphone on Switch 2.
- Lets you use a Switch 2 Pro Controller wirelessly on Switch 1 or PC. It appears as a Pro Controller 1.

## Requirements

- Raspberry Pi Pico 2 W
- Sony DualSense controller, DualShock 4, Nintendo Switch Pro Controller 1, Steam Controller, or Xbox Bluetooth controller
- Micro USB cable for connecting the Pico 2 W to the Switch/PC
- Firmware file from the [latest GitHub Release](https://github.com/david419kr/wake-NS2-dongle/releases/latest)

## Installing Firmware

1. Hold the BOOTSEL button on the Pico 2 W while connecting it to your PC over USB.
2. Download `switchbridge.uf2` from the [latest GitHub Release](https://github.com/david419kr/wake-NS2-dongle/releases/latest).
3. Copy `switchbridge.uf2` to the USB drive that appears on your PC.
4. After the copy finishes, the Pico 2 W will reboot automatically.

If this firmware is already installed, you can click BOOTSEL 4 times to enter USB drive mode.

## Pairing

1. Connect the Pico 2 W to the Switch.
2. When the Pico 2 W LED is blinking, it is waiting for a controller connection.
3. Put each controller into Bluetooth pairing mode.
4. Once connected, the Pico 2 W LED stays on and the Switch shows a `Pro Controller`.

After pairing once, the same controller will reconnect when powered on.

## Nintendo Switch 2 Wake

The first time, connect the Pico 2 W to USB while the Switch 2 is powered on. The wake information needed for Switch 2 wake will be saved.

1. Turn on the Switch 2. While it is powered on, connect the Pico 2 W to the Switch 2.
2. After that, press PS/Home/Steam/Xbox on the controller to wake the Switch 2.

The Bluetooth connection disconnects once when the Switch 2 wakes, so you may need to press PS/Home/Steam/Xbox once again. Depending on the controller model, it may reconnect automatically without any extra button input.

Switch 1 wake is not supported.

## Controller-Specific Mappings

| Controller | Behavior |
| --- | --- |
| DualSense / DS4 | Touchpad click is recognized as the screenshot Capture button. |
| Xbox Controller | Pressing Back/View + Menu together is recognized as screenshot Capture. |
| Steam Controller | Left touchpad is recognized as an 8-way D-pad, right touchpad as right stick, and the QAM (...) button as screenshot Capture. |
| Steam Controller | To remap L4/R4/L5/R5, hold the exact center click of the left touchpad, press the back button to remap, then press the button to assign. |
| Switch 2 Pro Controller | To remap GL/GR, hold the C button, press the back button to remap, then press the button to assign. |

## Audio

- Plug earphones or a headset into the DualSense to use it as a Switch/PC audio output device.
- Unplugging the earphones/headset removes the audio output device, while controller and microphone functions keep working.
- The DualSense microphone is exposed as a USB microphone while the controller is connected.
- Pressing the DualSense microphone mute button mutes the microphone and turns on the DualSense mute LED.

The USB connection may briefly reconnect when the audio device changes. This is normal.

## BOOTSEL Button

The Pico 2 W BOOTSEL button can be used for these actions.

| Action | Behavior |
| --- | --- |
| 1 click | Toggle LED display on/off. This setting is remembered after power-off. |
| 2 clicks | Disconnect the current controller, or start a 30-second pairing scan if no controller is connected |
| 3 clicks | Reboot the firmware |
| 4 clicks | Reboot into USB drive mode |
| Long press | Delete saved controller pairings |

## LED Status

- Blinking: waiting for a controller
- On: controller connected
- 6 flashes: saved pairings deleted

## Building

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup-pico-windows.ps1
powershell -ExecutionPolicy Bypass -File tools\test-host.ps1
powershell -ExecutionPolicy Bypass -File tools\build.ps1
```

After building, the firmware file is created here:

```text
build\pico2_w\switchbridge.uf2
```

## Troubleshooting

- If the controller does not appear on the Switch, make sure the controller is connected to the Pico 2 W. While no Bluetooth controller is connected, the controller device is not exposed except for the short Switch 2 Pro registration handshake.
- To connect a different controller, click BOOTSEL 2 times to disconnect the current controller, then pair the new controller.
- If automatic reconnection gets stuck, long-press BOOTSEL to delete saved pairings, then pair again.
- If audio output does not appear while using DualSense, make sure earphones or a headset are plugged into the DualSense 3.5 mm jack.

### If Switch 2 wake does not work

In normal use, no manual setup is required. If the Switch 2 still does not wake after pairing the controller and connecting to the Switch 2, try forcing the wake registration manually as a troubleshooting step.

1. Pair the controller with the Pico 2 W and connect the Pico 2 W to the Switch 2.
2. Enter manual registration mode.

| Controller | Action |
| --- | --- |
| DualSense / DS4 | Hold PS and click the touchpad twice. |
| Switch Pro Controller 1/2 | Hold Home and press Capture twice. |
| Steam Controller | Hold Steam and press the QAM (...) button twice. |
| Xbox Controller | Hold Xbox and press Back/View twice. |

3. It briefly reconnects as a `Switch 2 Pro Controller`, then automatically returns to normal mode after the Switch 2 recognizes this controller.

4. Put the Switch 2 into sleep mode.
5. Press PS/Home/Steam/Xbox on the controller to wake the Switch 2.
