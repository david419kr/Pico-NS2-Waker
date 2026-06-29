[한국어 README](README.ko.md)

# "Wake Switch 2 with Any Modern Controllers" Dongle

Firmware for using a DualSense, DualShock 4, Nintendo Switch Pro Controller 1, Steam Controller, or Xbox Bluetooth controller on Nintendo Switch 1/2 with a Raspberry Pi Pico 2 W.  
Any supported controller can wake a Nintendo Switch 2 from sleep. Switch 1 wake is not supported.

Credits:  
[ataradov/usb-sniffer](https://github.com/ataradov/usb-sniffer) and [homewsn/blesniff](https://github.com/homewsn/blesniff) were a great help in analyzing controller behavior.  
This project is heavily inspired by [awalol/DS5Dongle](https://github.com/awalol/DS5Dongle) and references its DualSense implementation extensively.

## What It Does

- Makes each controller appear as a `Nintendo Switch Pro Controller`.
- Can wake a Nintendo Switch 2 from sleep.
- Supports motion controls and rumble. Xbox controllers do not support motion controls.
- Lets you use the DualSense 3.5 mm jack and microphone.

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

When you connect the Pico 2 W for the first time, you must connect it with the Switch 2 powered on so the controller gets registered.

1. Turn on the Switch 2. Connect the Pico 2 W to the Switch 2, then connect the controller to the Pico 2 W.
2. Wait until the controller is registered on the Switch 2 and becomes usable.
3. Put the Switch 2 into sleep mode.
4. After that, press PS/Home/Steam/Guide on the controller to wake the Switch 2.

During wake, the Bluetooth controller connection disconnects once. After the Switch 2 wakes up, press PS/Home/Steam/Guide again to turn the controller back on and reconnect it.

Switch 1 wake is not supported.

## Controller-Specific Mappings

| Controller | Behavior |
| --- | --- |
| DualSense | Touchpad click is recognized as the screenshot Capture button. |
| DualShock 4 | Touchpad click is recognized as the screenshot Capture button. |
| Steam Controller | Left touchpad is recognized as D-pad, right touchpad as right stick, and the QAM (...) button as screenshot Capture. |
| Xbox Controller | Pressing Back/View + Menu together is recognized as screenshot Capture. |

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

- If the controller does not appear on the Switch, make sure the controller is connected to the Pico 2 W. The controller device is not exposed while no Bluetooth controller is connected.
- To connect a different controller, click BOOTSEL 2 times to disconnect the current controller, then pair the new controller.
- If automatic reconnection gets stuck, long-press BOOTSEL to delete saved pairings, then pair again.
- If audio output does not appear while using DualSense, make sure earphones or a headset are plugged into the DualSense 3.5 mm jack.

### If Switch 2 wake does not work

In normal use, no manual setup is required. If the Switch 2 still does not wake after pairing and connecting to the Switch 2, or if you have manually deleted the controller pairing, try forcing the wake registration manually as a troubleshooting step.

1. Pair the controller with the Pico 2 W and connect the Pico 2 W to the Switch 2.
2. Enter manual registration mode.

| Controller | Action |
| --- | --- |
| DualSense | Hold PS, click the touchpad twice, then release PS |
| DualShock 4 | Hold PS, click the touchpad twice, then release PS |
| Switch Pro Controller 1 | Hold Home, press Capture twice, then release Home |
| Steam Controller | Hold Steam, press the QAM (...) button twice, then release Steam |
| Xbox Controller | Hold Guide, press Back/View twice, then release Guide |

3. It reconnects as a `Switch 2 Pro Controller`. Wait until the Switch 2 recognizes it once.
4. Return to normal mode.

| Controller | Action |
| --- | --- |
| DualSense | Hold PS, click the touchpad once, then release PS |
| DualShock 4 | Hold PS, click the touchpad once, then release PS |
| Switch Pro Controller 1 | Hold Home, press Capture once, then release Home |
| Steam Controller | Hold Steam, press the QAM (...) button once, then release Steam |
| Xbox Controller | Hold Guide, press Back/View once, then release Guide |

5. Put the Switch 2 into sleep mode.
6. Press PS/Home/Steam/Guide on the controller to wake the Switch 2.
