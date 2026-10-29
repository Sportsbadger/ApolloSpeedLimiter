# Apollo Phantom V4X BLE project history

Updated: 27 October 2026. This is a handover for a new local development session. Inspect the current project files before modifying them; this document records results and intentions, not a substitute for the source code.

## Goal and approach

Build an independently powered controller that can request a low global maximum speed on the user's Apollo Phantom V4X using BLE. Develop in stages: prove the speed command, test the existing ESP32-WROOM hardware, then develop a standalone Android app, potentially with a widget or Quick Settings tile. A TENSTAR ESP32-C3 SuperMini was ordered but is delayed; the present work uses the existing WROOM.

## Confirmed BLE findings

- Scooter advertising name observed previously: `HW_Phantom`.
- Service: `F1F0`; write characteristic: `F1F1`; notify characteristic: `F1F2`. Discover by UUID, not captured ATT handles.
- Mode control works independently using nRF Connect and write without response. Eco to Comfort: `AB000AF10323304092C1`; Comfort to Eco: `AB000AF003233040AF01`. The scooter beeped.
- The official Apollo app sent the following sequence to `F1F1` before setting the global maximum to approximately 10 mph:
  1. `A500FF000000005A`
  2. Wait approximately 30 ms.
  3. `011700200001002000010200A052DC`
- An nRF Connect macro using that sequence and a nominal 30 ms delay changed the Apollo app's Advanced Settings maximum from 28 mph to 10 mph on subsequent app readback. The necessity of the first frame and the exact timing tolerance have not been isolated.
- Observed 10 mph response on `F1F2`: `011700200200A0A29F`. A successful write without response is not a GATT acknowledgement; even a matching notification or settings readback does not prove physical speed enforcement.
- Persistence after scooter power-off and actual enforced road speed remain unverified. Per-mode speed settings did not visibly change. Keep tests stationary and do not treat this as a safety-critical limiter.

## WROOM development and troubleshooting

MacBook Air, VS Code, PlatformIO, Arduino framework and NimBLE-Arduino. A previous watchdog reset loop (`TG1WDT_SYS_RESET`) occurred, but it was unclear whether the intended minimal sketch had actually been uploaded. The flash was erased, then the following tests succeeded:

1. A minimal Serial sketch printed `ESP32 BOOT OK` and repeated `ESP32 ALIVE`.
2. A NimBLE initialisation sketch printed `Bluetooth initialised successfully!` and continued running.
3. WROOM automatic prototype v0.4 subsequently booted and completed three scan attempts without watchdog resets. It reported `HW_Phantom not found` each time. The available conversation does not establish whether the scooter was powered on during that test.

The working `platformio.ini` used `esp32dev`, Arduino, monitor speed 115200, upload speed 115200, DIO flash mode, 40 MHz flash frequency, and `h2zero/NimBLE-Arduino@^2.5.1`. Preserve the actual current file rather than replacing it based on this summary.

The v0.4 startup log describes a three-second startup delay, up to three attempts to request the 10 mph limit, and a GPIO18-to-GND manual retry. The prior design includes disconnecting after an attempt and matching the F1F2 response; inspect the actual source to confirm details. Do not conflate this automatic WROOM version with the separate C3 prototype, which was designed for button-triggered operation and remains untested.

## Immediate task

Inspect `src/main.cpp` and add BLE scan diagnostics showing each discovered device's name, address and RSSI, including unnamed devices. Adapt to the existing NimBLE API and scan flow; don't assume the scan call or result type without checking. Preserve `platformio.ini`, the existing payloads, 30 ms delay and automatic startup behavior. Build the firmware and report errors. Do not claim scooter discovery or a successful speed change until physically tested.

For the next physical test, power on the stationary scooter, disconnect Apollo and nRF Connect from it, place the WROOM nearby, and capture Serial Monitor output at 115200. Scanning from nRF Connect without connecting can help distinguish advertising issues from ESP32 scan logic. Restore the user's normal limit after tests as appropriate.

## Maintenance

Record new experiments with date, exact conditions, observed serial output, readback, and confirmed versus unverified conclusions. A more detailed earlier log exists as `Apollo_Phantom_V4X_BLE_Project.md`; its WROOM status predates the successful tests above.
