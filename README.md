# DS5onSwitch

Raspberry Pi Pico 2 W firmware for using a Sony DualSense as a Nintendo Switch 1 Pro Controller.

Current status on `main`: the repository contains a buildable Pico 2 W firmware with testable core mapping logic, a TinyUSB Nintendo Switch Pro Controller USB persona, a first DualSense Bluetooth Classic HID bridge, and an experimental pure USB Audio path for the DualSense headset jack/microphone. The local build and host-side protocol tests pass, but real Nintendo Switch and real DualSense hardware validation is still pending.

Implemented runtime pieces:

1. DualSense Bluetooth HID inquiry/pairing/L2CAP transport, based on `DS5Dongle`.
2. DualSense BT `0x31` input parsing into Switch Pro `0x30` reports.
3. TinyUSB HID device descriptor and report descriptor for Nintendo Switch Pro Controller USB VID/PID `057e:2009`.
4. Switch USB command/subcommand handling for handshake, device info, SPI calibration reads, IMU/vibration enable, player lights, and rumble packets.
5. Switch `0x01/0x10` HD rumble translation into conservative DualSense BT `0x31` rumble-emulation output.
6. Automatic USB Audio persona switching:
   - no DualSense: HID-only Switch Pro Controller.
   - DualSense connected, no headset: HID + USB microphone.
   - DualSense headset jack active: HID + USB stereo output + USB microphone.
7. DualSense headset audio output: USB 48 kHz stereo PCM is encoded to Opus and sent over Bluetooth in DualSense `0x36` speaker/headset reports.
8. DualSense microphone input: Bluetooth `0x31` mic Opus frames are decoded and exposed through the USB Audio IN endpoint.
9. BOOTSEL button gestures:
   - short press disconnects the current controller if connected, or starts a 30-second pairing scan if disconnected.
   - double press reboots the Pico into the firmware.
   - triple press reboots into USB BOOTSEL mass-storage mode.
   - long press forgets all paired controllers, persists a reconnect blacklist, and flashes the LED six times.

Not implemented on `main`:

- Auto Audio Haptics.
- Runtime haptics mode switching.
- User configuration UI.

## Build

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup-pico-windows.ps1
powershell -ExecutionPolicy Bypass -File tools\test-host.ps1
powershell -ExecutionPolicy Bypass -File tools\build.ps1
```

The firmware output is:

```text
build\pico2_w\ds5onswitch.uf2
```

Verified locally on 2026-06-27:

- Host mapping tests pass via `tools\test-host.ps1`.
- Pico 2 W firmware builds via `tools\build.ps1`.
- Produced UF2: `build\pico2_w\ds5onswitch.uf2`.

The current UF2 starts as USB VID/PID `057e:2009` with product string `Pro Controller`. It handles the initial USB command/subcommand surface needed for Switch Pro negotiation, including USB response `0x81`, subcommand reply `0x21`, SPI read calibration payloads, IMU/vibration enable state, player-light state, and `0x01/0x10` rumble packet decoding. When a DualSense is connected over Bluetooth, its BT `0x31` reports replace the neutral input state.

USB Audio interfaces are exposed only after DualSense status makes them useful. Because USB interfaces cannot be added to an already-enumerated configuration, the firmware automatically performs a soft USB disconnect/reconnect when the DualSense connects, or when the headset jack state changes. No BOOTSEL mode switch is required. Switch hardware behavior for the composite HID+Audio personas is still unvalidated.

The BOOTSEL button is polled in firmware. Short press keeps pairings intact: if a controller is connected it is disconnected so another paired controller can take over, and if nothing is connected a 30-second scan starts for a controller in PS + Create/Share pairing mode. The runtime relocates selected BT/CYW43/TinyUSB hot paths into RAM so BOOTSEL QSPI CSn sampling can coexist with active Bluetooth traffic.

## Performance

The hot audio and packet path runs from RAM where it matches DS5Dongle's model: libopus text/rodata, DualSense speaker/mic processing, DualSense output report CRC/state helpers, BTstack HCI/L2CAP handlers and TX pumps, CYW43 Bluetooth/SPI transport, TinyUSB HID/audio transfer paths, Pico queues, and common memory operations are relocated to `.time_critical` RAM sections. This is required for stable 3.5 mm headset output, USB microphone forwarding, and connected-state BOOTSEL polling without flash-fetch stalls.

## Design Notes

Detailed local-code and public-reference analysis is in [docs/analysis.md](docs/analysis.md).
