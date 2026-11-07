# Phase 0 — Current-Branch Audit Addendum

**Repository:** `Quan285001/esp32_very_simple_smart_watch.`  
**Branch:** `rebuild/watch-face-studio-v2`  
**Source baseline reviewed:** `126e4d1` (current branch also includes the subsequent audit-document commits)  
**Scope:** GitHub source inspection and review of existing Phase 0/1 documents. No local build, browser interaction, BLE session, firmware flash, or physical-device test was performed as part of this source audit.

This addendum supersedes the source-state claims in the older Phase 0 reports. The 6afd283 audit is retained as historical context. Twenty-eight commits were added after that audit before this addendum; protocol, parser, renderer, and transfer behavior have materially changed.

## Executive summary

The branch now contains an integrated source-level path for a declarative Watch Face Studio design: schema v1 → Web package builder → WFS1 package → WFS v1 BLE service → chunk transfer → package validation → two-slot SPIFFS staging → active-face model. This is substantially beyond the earlier firmware-only baseline.

It is **not yet proven end-to-end**. The latest PlatformIO workflow observed for source commit `be90892` was still running. Browser-to-physical-device upload, interrupted-transfer recovery, reboot persistence, and sleep/wake reliability have not been demonstrated on hardware.

## A. What the current firmware actually does

- Boots the serial/display boot terminal and initializes the GC9A01 through a shared display manager.
- Restores time/weather state and schedules network work.
- Provides built-in analog clock, weather, menu, and system-information screens.
- Supports BOOT-button navigation and Light Sleep with GPIO9 wake.
- Retains legacy BLE commands for time, weather, location, Wi-Fi scan/configuration, and sleep.
- Adds a separate WFS v1 BLE service for capability/status discovery and face-package transfer.
- Parses a received WFS1 package into a bounded face model, validates its declared capabilities and assets, and stages it in the inactive storage slot before activation.

Presence of this code does not prove that each path works on the physical board.

## B. Existing modules

- App/lifecycle: `src/main.cpp`, `src/app/*`
- Display/input: `src/display/display_manager.*`, `src/input/button_manager.*`
- Legacy BLE: `src/services/ble_service.*`, `src/services/ble_command_handler.cpp`
- WFS BLE/protocol/transfer: `src/communication/{ble_manager,protocol,transfer_manager,command_handler}.*`
- Face model/parser/renderer/manager/validator: `src/watchface/*`
- Image element: `src/watchface/elements/image.*`
- Storage: `src/storage/{storage_manager,face_storage}.*`
- Services: time, weather, Wi-Fi under `src/services/*`
- Diagnostics/power: `src/system/*`
- Web Studio: `index.html`, `web/app.js`, `web/styles.css`
- Shared contract: `shared/schemas/*`, `shared/capabilities/*`, `docs/package/*`, `docs/protocol/*`

## C. Duplication and tight coupling

- `src/main.cpp` remains a large 953-line coordinator spanning boot, network scheduling, BLE servicing, sleep/wake, and screen rendering.
- `src/app/app_controller.cpp` still coordinates through broad `extern` declarations and directly knows about several services/render functions.
- Legacy face renderers still draw through the shared global TFT instance; DisplayManager owns the single instance, but a full drawing/canvas boundary is not yet enforced.
- Legacy BLE and WFS BLE coexist intentionally for backward compatibility. Their command ownership and shared radio lifecycle must remain explicit.
- Decompose incrementally; replacing these areas wholesale risks regressions.

## D. How the current watch face is rendered

- Web design canvas is 240×240 logical pixels. Object geometry is serialized in `transform` using x/y/width/height and z-order.
- Firmware parses JSON once into fixed-capacity `FaceModel` structures; current limits are 16 objects and 16 assets.
- Renderer source supports text, digital clock, analog clock, date, temperature weather, basic shapes, and RGB565 image assets.
- Dynamic rendering tracks changed bounds and redraws affected elements rather than reparsing JSON each frame.
- Typography now has nominal font size and independent glyph scaleX/scaleY paths. This is a device bitmap renderer, not browser font rasterization; arbitrary browser fonts, several weights, rotation, opacity, letter spacing and advanced styling remain restricted or rejected.
- The Web editor exposes more object types than the current firmware capability set. The uploader must reject unsupported objects before transfer.

## E. Current BLE implementation

Legacy service:
- Device name currently advertised as `ESP32_Watch_C3`; Web uses the `ESP32_Watch` prefix.
- Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- Characteristic UUID: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- READ/WRITE/WRITE_NR/NOTIFY and BLE2902.

WFS v1 uses a separate service with Control RX/TX and Data RX/TX characteristics. Its 16-byte frame layout is little-endian: magic/version/type/flags, sequence at bytes 6–7, session ID at 8–11, payload length at 12–13, CRC16 at 14–15, payload from byte 16.

The earlier 6afd283 audit identified a C++ offset defect. Current `protocol.cpp` uses offset 12, and the host codec test contains a fixed golden frame. BLE callbacks queue bounded packets; application-loop code handles parsing and filesystem work. Firmware accepts identical retries of the last chunk. The Web uploader now retries the same encoded frame up to three attempts, checks the acknowledged cumulative byte count, and marks the transfer for cancellation as soon as reservation is attempted so a stalled reservation can be cleaned up.

Still unverified: browser encoder against the golden fixture, actual negotiated MTU on target phones, lost/delayed notification behavior, reconnect/cancel races, and physical BLE operation after wake.

## F. Sleep and wake

Current Light Sleep path coordinates background network work, shuts down Wi-Fi and BLE, blanks the display, configures GPIO9 as a wake source, then restores display/button state and redraws after wake. BLE is not expected to remain connected during sleep. The user's earlier BLE-after-idle and wake issues remain hardware acceptance tests; source inspection alone cannot close them.

## G. Web design JSON structure

The canonical document is schema v1 in `shared/schemas/watch-face.schema.json`.

Root fields include `schemaVersion`, `designId`, `name`, `description`, `deviceProfileId`, timestamps, `canvas`, and `objects`.

Canvas contains width, height, shape, backgroundColor, clipToShape and optional safeArea.

Objects contain identity/type/name/visibility/lock state, `transform` (x, y, width, height, anchor, zIndex, rotation, opacity), and optional type-specific fields such as `style`, `textStyle`, `dataBinding`, `weatherFields`, `clockMode`, `timeFormat`, `handConfig`, `assetId`, and glyph properties.

The editor can create image, shape, analog-clock, digital-clock, date, text, weather, moon, sunrise-sunset, gauge, and progress-ring objects. Schema vocabulary also includes background/group. Firmware capability is deliberately narrower and must be checked separately.

Images are held in browser IndexedDB; the package builder resolves the asset, converts it to RGB565 little-endian, adds asset metadata/digests, and includes the binary asset bytes in WFS1.

## H. What currently blocks complete Web-to-device transfer

The source path now exists, and earlier blocking defects have been addressed in code:
- C++ frame payload length now agrees with the Web offset 12.
- A fixed C++ golden frame test now covers the wire layout.
- Firmware has idempotent duplicate-chunk handling.
- Web uploader now retries the same encoded chunk frame up to three attempts, verifies cumulative acknowledged bytes, and attempts cancellation if transfer reservation stalls.
- Upload flow includes handshake, capabilities, status, begin, chunk ACK, verify, apply, and status refresh.

Remaining blockers/risks:
1. No physical browser-to-device transfer has been executed and observed.
2. The C++ golden frame is not yet exercised against the actual browser encoder in an automated cross-language test.
3. The Web package builder materializes the package into browser memory; enforce practical package caps and test large assets.
4. `validateAgainstSchema()` and capability preflight need to be verified against malformed imports and every editor object type.
5. Transfer timeout, delayed ACK, disconnect, cancellation, and power-loss paths require fault-injection tests.
6. The UI must never display activation success without a matching successful device APPLY_RESULT.

## I. ESP32-C3 hardware and memory constraints

- Target remains ESP32-C3 DevKitM-1, Arduino, GC9A01 240×240, 40 MHz SPI, BOOT GPIO9.
- A full 240×240 RGB565 framebuffer would require 115,200 bytes; avoid a full-screen framebuffer unless measured and justified.
- Existing 160×160 RGB565 legacy clock sprite consumes about 51,200 bytes.
- Face model and asset metadata use fixed-capacity arrays; layout JSON limit is 16 KiB, manifest limit 8 KiB, data chunk buffer 216 bytes.
- RGB565 image rendering streams rows rather than loading a full image into RAM.
- No measured minimum heap, largest free block, parser peak, TLS peak, stack watermark, or real SPIFFS usable capacity is available in this audit. Current ceilings are implementation limits, not proven hardware maxima.

## J. Required changes: firmware vs Web

Firmware:
- Finish lifecycle ownership cleanup without breaking legacy screens/services.
- Keep BLE callbacks bounded and move all work into the loop/transfer state machine.
- Add cross-language protocol fixture coverage and transfer fault-injection tests.
- Verify parser/schema semantics and ensure every unsupported appearance field is rejected explicitly.
- Validate two-slot storage rollback across reset/power interruption.
- Measure runtime heap, stack and SPIFFS on the actual board.
- Verify sleep/wake, BLE reinitialization, Wi-Fi coexistence and long-idle stability physically.

Web:
- Run the real design object through schema validation rather than relying only on schema download/asset checks.
- Test the browser frame encoder against the C++ golden frame.
- Add/verify ACK retry, duplicate, delayed ACK, cancellation and disconnect behavior.
- Bound package and image memory before creating large intermediate arrays.
- Keep editor capabilities and device capabilities distinct and explain unsupported design properties before upload.
- Show success only after device-confirmed activation and status refresh.

## Current verification state

| Verification | State |
|---|---|
| GitHub source-tree inspection | Completed |
| Phase 0 source-level A–J review | Completed for this addendum baseline |
| PlatformIO build for latest source commit | In progress at time of inspection |
| Shared schema validation in CI | Passed for prior source commit; latest source commit run pending |
| Web asset validation and JS syntax in CI | Passed for prior source commit; latest source commit run pending |
| Host protocol codec test in CI | Passed for prior source commit; latest source commit run pending |
| Browser interaction test | Not performed |
| BLE simulation with browser | Not performed |
| Physical firmware flash | Not performed |
| Physical sleep/wake and long-idle test | Not performed |
| End-to-end Web-to-device upload | Not performed |

## Migration priority

1. Close automated cross-language protocol coverage and retry/fault-injection tests.
2. Verify current build and keep branch buildable.
3. Test the actual upload path on the ESP32-C3 before expanding renderer features.
4. Reconcile schema validation and capability preflight.
5. Measure memory/storage on hardware.
6. Refactor main/app lifecycle only after the integrated upload path is stable.

No changes to `main` are part of this work.
