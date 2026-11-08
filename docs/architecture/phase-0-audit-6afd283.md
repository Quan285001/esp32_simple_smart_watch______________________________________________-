# Phase 0 — Current Branch Audit (HEAD 6afd283)

**Date:** 2026-11-07  
**Branch:** `rebuild/watch-face-studio-v2`  
**Audited HEAD:** `6afd283b14e1be2bc38da6d8a6c2cd76fbdf9e2d`  
**Scope:** GitHub source inspection. No local PlatformIO build, browser interaction test, BLE session, firmware flash, or physical-device test was executed.

## Executive summary

The branch contains a substantial firmware and Web integration implementation: application state/controller, display and button managers, time/weather/Wi-Fi services, legacy BLE, a separate WFS v1 GATT service, a framed protocol codec, a chunk transfer manager, WFS1 package validation, two-slot SPIFFS face storage, a fixed-capacity face model/parser, a basic renderer, and a Web package builder/uploader.

The repository is not yet verified as an end-to-end working system. The highest-priority source-level blocker is a frame-header field-offset disagreement between JavaScript and C++: Web writes payload length at byte offset 12, while firmware reads/writes it at offset 10. The latter overlaps the final two bytes of the four-byte session ID. The current host codec test is self-round-trip only and therefore does not detect this cross-language incompatibility.

## Repository and build configuration

- Repository: `Quan285001/esp32_very_simple_smart_watch.`
- Environment: `c3_clock`
- Platform: `espressif32@6.9.0`
- Board/framework: `esp32-c3-devkitm-1` / Arduino
- Partition: `huge_app.csv`
- GC9A01 240×240; SCLK 4, MOSI 6, CS 7, DC 2, RST 3, MISO -1; BOOT GPIO9
- Monitor/upload baud: 115200
- Libraries: Adafruit GFX ^1.13.0, ArduinoJson ^7.2.2, TFT_eSPI ^2.5.43
- SPI frequency: 40 MHz
- Current build flag is `ARDUINO_USB_CDC_ON_BOOT=1`; preserve it unless build/hardware evidence justifies a change.

The recursive tree contains 88 files. The current branch head is 6afd283. Earlier audit documents in this repository refer to HEAD 50130f7 and describe an earlier source tree; they are historical, not authoritative for this HEAD.

## A. Current firmware behavior

The firmware boots a serial/display boot terminal, restores time and weather cache, starts a background network initialization task, then initializes BLE and the normal UI. The main UI includes built-in analog clock, weather, menu, and system information. It supports NTP/time persistence, weather fetch/cache, Wi-Fi configuration/scan, legacy BLE commands, Light Sleep, and BOOT-button navigation.

Custom watch-face infrastructure now exists: fixed-capacity model, JSON parser, renderer, manager, WFS1 package validator, two-slot SPIFFS storage, and transfer manager. The WFS v1 GATT service is also implemented in source. Existence of these components is not proof that the current branch builds or that browser-to-device upload succeeds.

## B. Existing modules

- App: `src/app/app_controller.*`, `app_state.*`
- Display/input: `src/display/display_manager.*`, `src/input/button_manager.*`
- Communication: `src/communication/{ble_manager,command_handler,protocol,transfer_manager}.*`
- Legacy BLE: `src/services/ble_service.*`, `ble_command_handler.cpp`
- Watch face: `src/watchface/{face_model,face_parser,face_renderer,face_manager,package_validator}.*`
- Elements: `src/watchface/elements/image.*`
- Storage: `src/storage/{storage_manager,face_storage}.*`
- Services: time, weather and Wi-Fi under `src/services`
- System: manager, diagnostics and memory monitor under `src/system`

## C. Coupling and duplication

- `src/main.cpp` remains a large coordinator (953 lines) for startup, network scheduling, weather/Wi-Fi state, BLE servicing, sleep/wake and periodic rendering.
- Legacy and WFS command handlers are separate by design for migration, but lifecycle/command ownership needs a single documented boundary.
- `app_controller.cpp` still calls services/renderers through broad `extern` declarations.
- The display manager owns the single TFT_eSPI instance, but old face code still draws directly through the shared `tft` symbol; a full canvas abstraction does not yet exist.

Do not remove compatibility code until each legacy behavior has a tested replacement.

## D. Rendering

The Web editor uses a 240×240 coordinate space and stores object geometry in `transform`. Firmware parses once into a fixed-capacity `FaceModel`, sorts by zIndex, and draws text, digital clock, analog clock, date, weather, shapes and RGB565 images. Dynamic rendering clears changed bounds and redraws intersecting layers.

Current limits: 16 objects, 16 assets, 16 KiB layout JSON. Typography uses quantized TFT_eSPI bitmap text scaling rather than browser-equivalent font metrics. Renderer preflight rejects nonzero rotation, partial opacity, nonzero letter spacing, unsupported weights and non-system font families. Analog `handConfig` styling is not fully implemented. The Web editor also exposes object types beyond the firmware's supported set; those must remain capability-gated.

## E. BLE and protocol

Legacy advertised name is `ESP32_Watch_C3`; Web filters by prefix `ESP32_Watch`. Legacy service/characteristic UUIDs remain:
- Service: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- Characteristic: `beb5483e-36e1-4688-b7f5-ea07361b26a8`

WFS v1 has a separate service and four RX/TX characteristics. BLE callbacks copy bounded packets to FreeRTOS queues; packet handling and flash work run from the application loop. This callback isolation should be preserved.

**Critical defect:** the documented and Web frame layout places payload length at offsets 12–13. C++ `protocol.cpp` currently reads/writes it at offsets 10–11, overlapping session ID bytes 10–11. The codec test encodes and decodes using the same implementation, so its round-trip assertions cannot detect the Web/C++ mismatch. Add golden cross-language frame fixtures and correct this before relying on handshake or transfer.

Other reliability gaps to verify: ACK retry/recovery, sequence handling across both characteristics, disconnect during transfer, and status after reconnect. A successful GATT write is not proof of application-level success.

## F. Sleep/wake

Light Sleep is entered from the main loop after checking background work and BLE connection state. Wi-Fi and BLE are shut down; GPIO9 is configured for wake; the display is blanked and redrawn after wake. BLE is not expected to remain connected during sleep. Physical repeated sleep/wake and BLE-after-wake tests remain mandatory.

## G. Actual Web design model

The canonical document is schema v1:
- Root: `schemaVersion`, `designId`, `name`, optional `description`, `deviceProfileId`, timestamps, `canvas`, `objects`.
- Canvas: width, height, shape, backgroundColor, clipToShape, safeArea.
- Object transform: x, y, width, height, anchor, zIndex, rotation, opacity.
- Typography: `textStyle` fields including fontSize, fontWeight, fill, fontFamily, textAlign and letterSpacing.
- Other fields include `style`, `dataBinding`, `weatherFields`, `clockMode`, `timeFormat`, `handConfig`, `assetId`, and element-specific properties.

Web object vocabulary is broader than the current device renderer. The editor currently creates image, shape, analog-clock, digital-clock, date, text, weather, moon, sunrise-sunset, gauge and progress-ring objects. Shared schema vocabulary also includes group/background. These are not all firmware-supported.

## H. Web-to-device blockers

1. WFS frame payload-length offset mismatch (blocking).
2. Web upload is sequential and waits for ACK, but does not implement an actual chunk retry/resume strategy.
3. The browser package builder assembles a complete package byte array before transfer; package and browser memory ceilings need explicit validation.
4. Normal JSON export does not include image bytes held separately in IndexedDB; only the WFS package builder resolves and converts assets.
5. Schema permits up to 128 objects while firmware model/capabilities limit 16.
6. Firmware parser handles only a subset of schema properties; unknown/unsupported properties need explicit capability/error behavior.
7. Web's `validateAgainstSchema()` fetches the schema but does not validate the current design against it.
8. Renderer typography and transformations are materially narrower than browser preview semantics.

## I. ESP32-C3 resource constraints

Fixed model capacities and bounded BLE queues are appropriate for this MCU. Image rendering streams one RGB565 row rather than allocating a full-screen image. Package validation hashes file ranges in bounded blocks but separately allocates manifest and layout text for parsing. The legacy clock sprite allocates 160×160×2 = 51,200 bytes.

No measured peak heap, minimum heap, largest free block, TLS peak, parser peak, stack watermark, or actual SPIFFS usable capacity was captured. Current limits are conservative implementation limits, not measured maxima.

## J. Required changes

### Firmware
- Correct WFS frame offsets and add cross-language golden tests.
- Unify lifecycle ownership while retaining legacy BLE service compatibility.
- Define sequence, retry, timeout, cancellation and reconnect behavior.
- Make package validation and model parsing agree with the canonical schema.
- Advertise only renderer features actually implemented and tested.
- Improve typography/glyph rendering and map analog hand configuration.
- Measure memory and storage before changing hard limits.
- Verify transactional two-slot activation and recovery under interruption.
- Test sleep/wake and radio lifecycle on the physical ESP32-C3.

### Web
- Validate documents against the shared JSON schema, not just fetch it.
- Keep editor vocabulary and runtime capabilities distinct.
- Add ACK retry/timeout and safe cancellation/reconnect behavior.
- Validate image and package resource bounds before allocating large buffers.
- Make unsupported transform/font/glyph behavior explicit in UI.
- Use device-confirmed APPLY_RESULT before showing activation success.

## Verification status

| Verification type | Status |
|---|---|
| GitHub source/tree inspection | Completed for HEAD 6afd283 |
| Static cross-file contract review | Completed; blockers identified |
| JSON schema validation script | Not executed in this audit |
| Host protocol tests | Not executed in this audit |
| PlatformIO clean build | Not executed in this audit |
| CI result for HEAD | No successful check evidence confirmed |
| Browser interaction test | Not executed |
| BLE simulation | Not executed |
| Physical board test | Not executed |
| Web-to-device integration test | Not executed |

## Phase 1 acceptance criteria

1. Freeze one byte-exact WFS frame specification with shared C++/JavaScript golden fixtures.
2. Freeze schema-v1-to-device-model mapping and explicit unsupported-property behavior.
3. Freeze WFS1 package and asset limits, including overflow and rollback semantics.
4. Freeze command/error/event/request correlation and transfer retry/reconnect behavior.
5. Publish module ownership/dependency and lifecycle diagrams.
6. Establish measured-versus-provisional memory limits and a measurement plan.
7. Update stale audit/protocol/package documents so they describe current source status accurately.

No firmware or Web source files were modified as part of this Phase 0 audit.
