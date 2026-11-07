# Phase 2 — Core Firmware Foundation Progress

> **Historical snapshot:** this document predates the current WFS v1 GATT, transfer manager, package validator and Web uploader source. Its older statements that these modules do not exist are superseded by `phase-0-audit-6afd283.md` and `phase-1-architecture-contract.md`. Do not use this snapshot as the current source inventory.

**Status:** firmware/Web integration is implemented to a substantial source level; CI is passing, while hardware and end-to-end verification remain open.  
**Branch:** rebuild/watch-face-studio-v2  
**Hardware profile:** ESP32-C3 + GC9A01 240×240  
**Important:** this document records implemented source changes and outstanding verification separately.

## 1. Implemented foundation modules

### Display ownership

- src/display/display_manager.h/.cpp now own the single global TFT_eSPI instance.
- beginDisplay() performs the one-time GC9A01 initialization and rotation setup.
- Existing face modules continue using the shared tft symbol as a transitional compatibility layer.
- No second display object should be created in future modules.
- Existing direct drawing calls are not yet fully routed through a Canvas abstraction.

### Memory monitoring and diagnostics

- src/system/memory_monitor.h/.cpp captures free heap, minimum free heap and largest contiguous 8-bit heap block.
- src/system/diagnostics.h/.cpp reports memory snapshots at boot, after BLE initialization and every 15 seconds.
- The previous one-second loop heartbeat was replaced with bounded telemetry to reduce serial noise.
- This is telemetry, not a complete stack-watermark or allocation profiler.

### Storage abstraction

- src/storage/storage_manager.h/.cpp mounts the existing SPIFFS partition with format-on-failure disabled.
- Mount failure is non-fatal to the clock; storage-dependent features remain unavailable.
- The current Arduino-ESP32 huge_app partition layout defines a 0xE0000-byte SPIFFS partition (917,504 bytes nominal). Actual usable capacity must be queried on the device because filesystem overhead reduces it.
- Preferences/NVS remains the store for small settings and time/weather metadata; it is not the face-package filesystem.

### Application state

- Shared screen/menu/weather state definitions moved from main.cpp into src/app/app_state.cpp.
- app_state.h remains the declaration boundary.
- app_controller.cpp still has legacy extern dependencies and is not yet a fully isolated controller.

### Face model, parser and renderer foundation

- src/watchface/face_model.* defines a fixed-capacity model with a 16-object initial parser limit.
- src/watchface/face_parser.* parses schema v1 JSON with a nesting limit, bounded strings, numeric/color checks and explicit parser errors.
- The parser recognizes text, digital-clock, analog-clock, date, weather, shape and image object kinds. Recognition does not mean every kind is currently renderable.
- src/watchface/face_renderer.* renders basic text, digital clock, date, weather temperature, analog clock and simple shapes.
- The first renderer uses the TFT_eSPI built-in bitmap font. Nominal font size is quantized to integer bitmap-font scale steps; independent glyph scaleX/scaleY is serialized, parsed and applied to glyph pixels. Arbitrary Web fonts and glyph atlases remain unsupported.
- Rotation, partial opacity, letter spacing, non-system font families, advanced glyph effects and custom analog hand styles are rejected before activation.
- Images are streamed as RGB565 rows from the active package. Compressed PNG/JPEG decoding and transparent overlay composition are not supported on-device.
- Dynamic elements use dirty-region redraw with a bounded TFT viewport; intersecting layers are clipped to the dirty rectangle. A newly activated face is fully rendered once before dynamic-only redraws.
- FaceManager loads and validates the committed WFS1 package first, falls back to the other valid slot, then retains /face.json as a raw integration/test fallback before using the built-in clock.

## 2. Conservative limits and why they exist

The initial parser caps a layout at 16 objects and 16 KiB of JSON. These are conservative implementation guardrails, not measured maximum capacities and not a claim that 16 objects is the final device limit.

Before increasing or freezing limits, measure:
- parser peak heap and largest contiguous block;
- BLE connected idle heap;
- renderer memory and stack;
- weather TLS peak heap;
- SPIFFS total/used/free bytes;
- simultaneous staging and active package capacity.

The schema may allow more objects, but the runtime capability response must advertise the firmware's tested limit rather than copying the schema maximum.

## 3. Storage decision for face replacement

The existing huge_app.csv layout already provides SPIFFS. Keep this partition initially; do not change the app/FS allocation until measured package needs justify it.

Implemented safe replacement strategy:
1. Keep active face in one of two SPIFFS package slots.
2. Receive only into the inactive slot after checking available capacity while preserving the active package.
3. Verify WFS1 header, manifest, layout, asset ranges/digests, object references and renderer compatibility from the staged file.
4. Persist and read back the active-slot pointer in NVS only after verification.
5. Retain the previous slot as rollback; FaceStorage::removeSlot refuses to remove the active slot.
6. Failed transfers can be explicitly cancelled or cleaned after BLE session loss, including the Failed state.

The current package ceiling is constrained by a 16 KiB layout parser limit, manifest limit, 16 objects, 16 assets, negotiated chunk size and actual SPIFFS free capacity. Host tests cover sequencing and cancellation policy; SPIFFS write-fault, power-loss and reboot-recovery integration tests remain open.


## 4. Current implementation limitations

- The WFS v1 control/data GATT service, framed protocol, handshake, capability/status responses, ordered inbound queue and stop-and-wait transfer are implemented in source.
- The Web package builder emits WFS1 packages and the uploader waits for application-level transfer/verify/apply responses. Host tests cover frame bytes, package digests/CRC, ACK waiter cleanup and glyph scale preservation.
- Web Bluetooth ↔ physical-device interoperability and a complete successful upload have not yet been demonstrated on hardware.
- Firmware supports only 16 objects and 16 assets, with a 16 KiB layout limit; schema vocabulary is intentionally broader than device capabilities.
- Images are RGB565 and streamed row-by-row. Transparent overlay composition, alpha masks and compressed PNG/JPEG decoding are not supported.
- The renderer does not support rotation, partial opacity, arbitrary fonts, glyph atlases/overrides, groups, gauges, progress rings or advanced analog hand styling.
- Weather face objects currently support temperature and condition only; other forecast/lunar values remain in the legacy weather screen, not the custom-face object renderer.
- The parser rejects unknown keys at supported root/object/nested levels, but dedicated parser fixture/fuzz tests are still outstanding.
- Weather TLS currently uses setInsecure(); certificate validation remains a security hardening item.
- No physical hardware tests have been run for these modules.

## 6. Phase 2 acceptance checklist

- [x] Single display instance moved into DisplayManager.
- [x] Memory snapshot module and bounded diagnostics cadence added.
- [x] SPIFFS abstraction added with non-destructive mount behavior.
- [x] Shared application state moved out of main.cpp.
- [x] Fixed-capacity face model and bounded JSON parser added.
- [x] Basic schema-driven renderer and dirty-region dynamic update path added.
- [x] PlatformIO CI build passes on branch HEAD 31b6773.
- [x] Schema/Web CI checks pass on branch HEAD 31b6773.
- [x] Flash and RAM linker usage recorded from CI: 103,468 / 327,680 bytes RAM (31.6%) and 1,797,688 / 3,145,728 bytes application flash (57.1%). These are static linker figures, not runtime heap measurements.
- [ ] Physical board boot regression completed.
- [ ] Physical board display/clock/weather/button/sleep regression completed.

## 7. Next mandatory phases

1. Add dedicated parser fixtures and malformed-input/fuzz tests for the ArduinoJson-backed firmware parser.
2. Add TransferManager + SPIFFS integration tests for short writes, length/digest mismatch, cancel, disconnect and reboot recovery.
3. Exercise the BLE handshake, reconnect and upload workflow against the physical ESP32-C3 from supported desktop/mobile browsers.
4. Measure runtime heap minimum/largest block, task stack high-water marks, SPIFFS usable capacity and TLS peak on the target board.
5. Complete repeated boot, clock, weather-cache, image, face-activation, idle, BLE connect/disconnect and Light Sleep wake regressions.
