# DS5onSwitch

Raspberry Pi Pico 2 W firmware for using a Sony DualSense as a Nintendo Switch 1 Pro Controller.

Current status: the repository now contains a buildable Pico 2 W firmware with testable core mapping logic, a TinyUSB Nintendo Switch Pro Controller USB persona, and a first DualSense Bluetooth Classic HID bridge.

Implemented runtime pieces:

1. DualSense Bluetooth HID inquiry/pairing/L2CAP transport, based on `DS5Dongle`.
2. DualSense BT `0x31` input parsing into Switch Pro `0x30` reports.
3. Switch `0x01/0x10` HD rumble translation into conservative DualSense BT `0x31` rumble-emulation output.

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

The current UF2 enumerates as USB VID/PID `057e:2009` with product string `Pro Controller`. It handles the initial USB command/subcommand surface needed for Switch Pro negotiation, including USB response `0x81`, subcommand reply `0x21`, SPI read calibration payloads, IMU/vibration enable state, player-light state, and `0x01/0x10` rumble packet decoding. When a DualSense is connected over Bluetooth, its BT `0x31` reports replace the neutral input state.

## Design Notes

Detailed local-code and public-reference analysis is in [docs/analysis.md](docs/analysis.md).
