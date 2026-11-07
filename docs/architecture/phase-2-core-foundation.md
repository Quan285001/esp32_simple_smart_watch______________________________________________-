# Phase 2 — Core Firmware Foundation Progress

> **Historical snapshot:** this document predates the current WFS v1 GATT, transfer manager, package validator and Web uploader source. Its older statements that these modules do not exist are superseded by `phase-0-audit-6afd283.md` and `phase-1-architecture-contract.md`. Do not use this snapshot as the current source inventory.

**Status:** substantial implementation in progress; the latest code is awaiting CI build and physical-board verification.  
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
- The first renderer uses the TFT_eSPI built-in bitmap font. Font size is quantized to integer scale steps; arbitrary Web fonts and exact per-glyph deformation require the planned glyph-atlas pipeline.
- Rotation, partial opacity, non-system font families and image assets are rejected before the display is touched.
- Dynamic elements use dirty-region redraw: clear the dynamic element bounds, then redraw all intersecting visible layers in z-order. This avoids full-screen redraw every second for ordinary clock updates.
- src/watchface/face_manager.* can load a validated raw JSON layout from /face.json and retains the built-in clock as fallback.
- This raw JSON loading path is a temporary bring-up/test path, not the final .wface package installer.

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

Proposed safe replacement strategy:
1. Keep active face in one of two package slots.
2. Receive and validate into the inactive slot.
3. Confirm package and asset digests and schema/capability compatibility.
4. Persist a small active-slot pointer in NVS.
5. Retain the previous slot as rollback until a later successful boot/validation policy allows cleanup.

If the inactive slot cannot fit the complete new package while preserving the active package, reject the transfer before accepting data. Never delete the current active face to make room for an unvalidated replacement.

The final slot layout, package size ceiling and cleanup policy remain unimplemented.

## 4. Current implementation limitations

- No BLE v1 control/data GATT service is implemented.
- No versioned frame parser, handshake, capability response or request correlation is implemented.
- No chunk transfer, ACK/retry, digest verification or cancellation is implemented.
- No .wface archive/container parser or Web package exporter is implemented.
- Image support is limited to opaque RGB565 assets, streamed row-by-row. Transparent overlays, image alpha masks, image scaling on-device and compressed PNG/JPEG decoding are not supported.
- The renderer does not yet support opacity, rotation, arbitrary fonts, glyph overrides, groups, gauges or progress rings.
- Analog hand style configuration is not yet mapped from the full schema.
- Weather renderer currently displays a basic temperature value; field-specific layout and freshness are incomplete.
- FaceManager's raw JSON boot path is for integration bring-up only and must be replaced by the package installer.
- No physical hardware tests have been run for these new modules.

## 6. Phase 2 acceptance checklist

- [x] Single display instance moved into DisplayManager.
- [x] Memory snapshot module and bounded diagnostics cadence added.
- [x] SPIFFS abstraction added with non-destructive mount behavior.
- [x] Shared application state moved out of main.cpp.
- [x] Fixed-capacity face model and bounded JSON parser added.
- [x] Basic schema-driven renderer and dirty-region dynamic update path added.
- [ ] Current PlatformIO CI build passes on latest branch HEAD.
- [ ] Schema/Web CI checks pass on latest branch HEAD.
- [ ] Flash and RAM size report recorded from CI.
- [ ] Physical board boot regression completed.
- [ ] Physical board display/clock/weather/button/sleep regression completed.

## 7. Next mandatory phases

1. Stabilize Phase 2 build and fix any compile/link regressions.
2. Add host-side parser fixtures and malformed-input tests.
3. Complete renderer property mapping and capability negotiation.
4. Implement two-slot face storage with rollback.
5. Freeze the package container and protocol UUIDs/frames.
6. Implement BLE handshake and status before bulk transfer.
7. Implement transfer and then the Web package builder/Upload action.
8. Complete end-to-end and physical reliability tests.
