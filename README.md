# DS5onSwitch

Raspberry Pi Pico 2 W firmware for using a Sony DualSense as a Nintendo Switch 1 Pro Controller.

Current status: the repository now contains the first buildable Pico 2 W scaffold, testable core mapping logic, and a TinyUSB Nintendo Switch Pro Controller USB persona. The runtime bridge is intentionally split into follow-up work:

1. DualSense Bluetooth HID connection and `0x31` input/output transport, based on `DS5Dongle`.
2. DualSense-to-Switch input mapping, Switch Pro IMU report generation, and HD rumble to DualSense haptic policy.
3. DualSense haptic output over BT `0x31`, driven by Switch `0x01/0x10` rumble output.

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

The current UF2 enumerates as USB VID/PID `057e:2009` with product string `Pro Controller` and sends neutral Switch Pro input reports. It handles the initial USB command/subcommand surface needed for Switch Pro negotiation, including USB response `0x81`, subcommand reply `0x21`, SPI read calibration payloads, IMU/vibration enable state, player-light state, and `0x01/0x10` rumble packet decoding.

## Design Notes

Detailed local-code and public-reference analysis is in [docs/analysis.md](docs/analysis.md).
