[한국어 README](README.ko.md)

# DualSense on Switch

Firmware for using a DualSense as a Nintendo Switch Pro Controller with a Raspberry Pi Pico 2 W.

Credit: This project is not a fork of [awalol/DS5Dongle](https://github.com/awalol/DS5Dongle), but it is heavily inspired by it and references its implementation extensively.

## What It Does

- Connects to a DualSense over Bluetooth and appears to the Switch/PC as a `Pro Controller`.
- Maps the DualSense touchpad click to the Switch Capture button.
- Supports motion controls and rumble.
- Uses the DualSense 3.5 mm jack as a USB audio output when earphones or a headset are plugged in.
- Exposes the DualSense microphone as a USB microphone.

## Tested Behavior

- PC: 3.5 mm jack audio output and microphone both work.
- Nintendo Switch 1: 3.5 mm jack audio output works.
- Nintendo Switch 2: 3.5 mm jack audio output and microphone both work.

## Requirements

- Raspberry Pi Pico 2 W
- Sony DualSense controller
- USB cable for connecting the Pico 2 W to the Switch/PC
- Firmware file from the [latest GitHub Release](https://github.com/david419kr/DS5onSwitch/releases/latest)

## Installing Firmware

1. Hold the BOOTSEL button on the Pico 2 W while connecting it to your PC over USB.
2. Download `ds5onswitch.uf2` from the [latest GitHub Release](https://github.com/david419kr/DS5onSwitch/releases/latest).
3. Copy `ds5onswitch.uf2` to the USB drive that appears on your PC.
4. After the copy finishes, the Pico 2 W will reboot automatically.

If this firmware is already installed, you can click BOOTSEL 4 times to enter USB drive mode.

## Pairing

1. Connect the Pico 2 W to the Switch or PC.
2. When the Pico 2 W LED is blinking, it is waiting for a controller.
3. Hold PS + Create on the DualSense to enter pairing mode.
4. Once connected, the Pico 2 W LED stays on and the Switch/PC shows a `Pro Controller`.

After the first pairing, the same DualSense will reconnect when powered on.

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
| 1 click | Toggle normal LED display on/off. This setting is remembered after power-off. |
| 2 clicks | Disconnect the current controller, or start a 30-second pairing scan if no controller is connected |
| 3 clicks | Reboot the firmware |
| 4 clicks | Reboot into USB drive mode |
| Long press | Delete saved controller pairings |

Special LED indications, such as the 6 flashes after a long press, still work even when normal LED display is off.

## LED Status

- Blinking: waiting for a controller
- On: DualSense connected
- 6 flashes: saved pairings deleted

## Building

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup-pico-windows.ps1
powershell -ExecutionPolicy Bypass -File tools\test-host.ps1
powershell -ExecutionPolicy Bypass -File tools\build.ps1
```

After building, the firmware file is created here:

```text
build\pico2_w\ds5onswitch.uf2
```

## Troubleshooting

- If the controller does not appear on the Switch/PC, make sure the DualSense is connected to the Pico 2 W. The controller device is not exposed until a DualSense is connected.
- To connect a different DualSense, click BOOTSEL 2 times to disconnect the current controller, then pair the new controller.
- If automatic reconnection gets stuck, long-press BOOTSEL to delete saved pairings, then pair again.
- If audio output does not appear, make sure earphones or a headset are plugged into the DualSense 3.5 mm jack.
