# Phase 0 — Current Branch Repository Audit Addendum

**Audit date:** 2026-11-07  
**Repository:** Quan285001/esp32_very_simple_smart_watch.  
**Audited branch:** rebuild/watch-face-studio-v2  
**Audited HEAD:** 50130f7f7531266a9d70d09206c4b20efaa57daa  
**Scope:** GitHub source inspection. No local PlatformIO build, browser interaction test, firmware flash, BLE session, or physical-device test was executed during this audit.

This addendum supersedes statements in the original Phase 0 report that describe the older baseline tree. The original report remains historical context, but it is not an accurate inventory of the current branch's Web application or shared contract files.

## 1. Executive findings

The current branch contains three distinct maturity levels:

1. **Existing firmware foundation:** fixed clock/weather screens, button navigation, BLE text commands, Wi-Fi/weather operations, time/weather persistence, and Light Sleep lifecycle code.
2. **Implemented Web editor foundation:** a vanilla JavaScript Watch Face Studio UI with templates, object layers, inspector, local draft/design storage, IndexedDB image storage, JSON import/export, legacy BLE controls, and an Upload action that currently displays an informational placeholder.
3. **Draft integration architecture:** shared JSON schemas, a device profile, a .wface package draft, and a BLE protocol draft. These documents are not equivalent to working firmware modules or an implemented transport.

The main integration gap is the absence of a common, validated end-to-end design/package pipeline and a firmware renderer that consumes it.

## 2. Repository and branch safety

- The audited branch exists and currently points to 50130f7f7531266a9d70d09206c4b20efaa57daa.
- No changes in this work are to be pushed to main.
- The branch is not protected according to the branch metadata returned during inspection.
- The current PlatformIO workflow includes rebuild/watch-face-studio-v2 in its push trigger. The workflow file alone does not prove that the latest commit built successfully.
- The available commit workflow-run lookup returned no PR-triggered runs for this commit, and the combined status lookup returned no status checks. The latest branch state is therefore not build-verified by evidence gathered in this audit.

## 3. Current source tree

The recursive Git tree for the audited branch contains:

- .github/workflows/platformio-build.yml
- docs/architecture/phase-1-foundation.md
- docs/architecture/repository-audit.md
- docs/frontend/phase-2-editor-mvp.md
- docs/package/wface-v1.md
- docs/protocol/protocol-v1-draft.md
- index.html, platformio.ini
- scripts/build_all.py, export_firmware.py, flash.py, patch_tft_c3.py, validate_schemas.py, validate_web.py
- shared/capabilities/esp32-c3-gc9a01-240.json
- shared/schemas/device-profile.schema.json
- shared/schemas/watch-face.schema.json
- shared/schemas/examples/minimal-watch-face-v1.json
- src/app/app_controller.cpp, app_controller.h, app_state.h
- src/boot_terminal.cpp, config.h
- src/face/face_clock.cpp, face_info.cpp, face_menu.cpp, face_weather.cpp
- src/icons.h
- src/input/button_manager.cpp, button_manager.h
- src/main.cpp
- src/services/ble_command_handler.cpp, ble_service.cpp, ble_service.h, time_service.cpp, time_service.h, weather_service.cpp, weather_service.h
- tests/light_sleep_isolation/
- web/app.js, web/styles.css

There is no implemented firmware-side watchface renderer, general display manager, protocol parser, transfer manager, face storage manager, or package installer yet. The suggested architecture is not present merely because its design is documented.

## 4. Hardware and build configuration verified from source

The current platformio.ini confirms:

| Item | Current value |
|---|---|
| Environment | c3_clock |
| Platform | espressif32@6.9.0 |
| Board | esp32-c3-devkitm-1 |
| Framework | Arduino |
| Partition | huge_app.csv |
| Display | GC9A01, 240 × 240 |
| SPI pins | SCLK 4, MOSI 6, CS 7, DC 2, RST 3, MISO -1 |
| BOOT button | GPIO 9 |
| Monitor/upload speed | 115200 |
| Libraries | Adafruit GFX ^1.13.0, ArduinoJson ^7.2.2, TFT_eSPI ^2.5.43 |
| TFT SPI frequency | 40 MHz |

The build flags currently set ARDUINO_USB_CDC_ON_BOOT=1; this differs from an older project note that said 0. Preserve the current branch configuration unless an actual build or hardware test justifies a change.

src/config.h includes the local secrets.h, defines GPIO9/button timing, NTP/UTC+7, default HCMC weather coordinates, weather refresh interval, screen enum and a fixed-size WeatherData structure. The local secrets file is not part of the tracked tree.

## 5. Firmware module audit

### 5.1 Application lifecycle — src/main.cpp

The file is approximately 32 KB and currently owns or coordinates:
- global TFT_eSPI tft instance;
- screen/menu/weather state;
- boot UI and display rendering calls;
- Wi-Fi enable/retry and scan state;
- weather task scheduling and completion notification;
- BLE command servicing;
- automatic/manual Light Sleep request and wake path;
- time persistence and loop diagnostics.

This is a substantial centralization point. It should be decomposed incrementally, not replaced in one destructive rewrite. The display driver should remain a single shared instance.

### 5.2 Application controller — src/app/*

app_controller.cpp still draws fixed screens by calling drawAnalogClockFace, drawWeatherFaceData, drawMenuScreen and drawSystemInfoScreen. It also owns menu navigation and invokes Wi-Fi, BLE, time, weather and sleep operations through extern declarations.

This is a useful fixed-screen state controller, but it is not a declarative watch-face application controller. Its broad extern dependencies indicate tight coupling.

### 5.3 Display and fixed face rendering — src/face/*

- face_clock.cpp draws a hard-coded analog face with fixed labels, weather/connection indicators and clock hands.
- It creates a 160 × 160 RGB565 sprite, requiring approximately 51,200 bytes of pixel memory before allocator overhead.
- face_weather.cpp contains a large custom forecast/detail renderer and animation state tied directly to the fixed WeatherData structure and TFT calls.
- face_menu.cpp and face_info.cpp are fixed display screens.

There is no shared canvas abstraction, generic object renderer, schema-driven layout loader, glyph atlas renderer, or asset-backed image renderer in the current firmware source tree.

### 5.4 BLE service — src/services/ble_service.*

Verified identifiers:
- Device advertised name: ESP32_Watch_C3.
- Service UUID: 4fafc201-1fb5-459e-8fcc-c5c9c331914b.
- Characteristic UUID: beb5483e-36e1-4688-b7f5-ea07361b26a8.
- Characteristic properties: READ, WRITE, WRITE_NR and NOTIFY.
- BLE2902 notification descriptor is installed.

The write callback copies a bounded value into a FreeRTOS queue. This is a sound isolation pattern to preserve: do not run storage, network, parsing or renderer work inside BLE callbacks.

Current limits and gaps:
- one text command/notification characteristic;
- command length limited to 127 bytes;
- queue length is 5;
- no version negotiation, session ID, request correlation, capability response, framed binary data, chunk acknowledgement, transfer retry, package validation or apply transaction;
- notifications are not a general framed binary transfer channel;
- BLE is shut down for Light Sleep and initialized again later.

The advertised name differs from historical ESP32_Watch documentation, although the current Web name-prefix filter ESP32_Watch matches the actual name.

### 5.5 Legacy command handler

ble_command_handler.cpp handles:
- GET_AUTO_SLEEP and AUTO_SLEEP:<seconds>;
- SYNC_TIME:<epoch>;
- SYNC_WEATHER;
- GPS:<lat>,<lon>;
- WIFI:<ssid>,<password>;
- SCAN_WIFI;
- SLEEP_ON and SLEEP_OFF.

It persists settings through Preferences and delegates some work to the main application. The parser is delimiter-based. SSID/password values containing commas are not safely representable. SLEEP_OFF only sends an AWAKE notification; it does not itself wake a sleeping MCU. Preserve supported behaviors through explicit migration and define a safe structured representation for credentials.

### 5.6 Time service

time_service.cpp contains NTP setup, restoration of a saved Unix epoch and UTC offset, periodic NVS checkpointing, phone GPS preference, IP-based timezone fallback, and timezone updates from weather results. This is useful behavior, but network geolocation, timezone policy, persistence and clock access should be separated behind a small service interface.

### 5.7 Weather service

weather_service.cpp contains Open-Meteo requests, HCMC default coordinates and mutable GPS coordinates, fixed-size current/hourly weather snapshots, mutex-protected publication, NVS weather-cache serialization/restoration, and optional sunrise/sunset/lunar data.

Keep the fixed-size snapshot and cache semantics as a baseline. Measure peak heap during TLS/JSON processing. Audit insecure TLS configuration before treating remote weather as authenticated.

### 5.8 Button manager

button_manager.cpp is comparatively isolated. It debounces GPIO9 and dispatches short, double and long press events. Keep this module and formalize its events/state transitions rather than rebuilding it without need.

### 5.9 Sleep/wake

Light Sleep is requested by application code, waits for background network work to finish, disables Wi-Fi, calls BLE shutdown, paints the display black, configures GPIO9 low-level wake, enters esp_light_sleep_start(), then restores button handling and redraws the screen. Radio services remain off after wake until explicitly re-enabled.

This is a prototype lifecycle with prior user-reported BLE/wake failures. It is not proven by source inspection. Test repeated sleep/wake, BLE reinitialization, GPIO wake, Wi-Fi coexistence and display redraw on the physical board.

## 6. Current Web application and exact design model

The current Web application is vanilla JavaScript in web/app.js, with index.html and web/styles.css. It is not yet the React/Vite PWA proposed in the Phase 1 architecture document. Do not migrate framework solely for architectural fashion; first complete a reliable vertical slice with the existing stack.

### 6.1 Root design document

The actual blankDesign() implementation creates:
- schemaVersion: 1
- designId: generated string
- name and description
- deviceProfileId: esp32-c3-gc9a01-240
- createdAt and updatedAt ISO timestamps
- canvas: width, height, shape, backgroundColor, clipToShape
- objects: array

Current templates use a 240 × 240 canvas and object types including digital-clock, date, analog-clock, weather, text and sunrise-sunset.

### 6.2 Object model observed in source

Common object properties:
- id, type, name;
- visible, locked;
- transform.x, transform.y, transform.width, transform.height, transform.anchor, transform.zIndex;
- transform.rotation and transform.opacity are read by the Web canvas renderer;
- optional style;
- type-specific text, textStyle, dataBinding, clockMode, timeFormat, handConfig, weatherFields, shape and assetId.

Observed textStyle properties include fontSize, fill, fontFamily, fontWeight, textAlign and letterSpacing. The current renderer applies these as CSS styles. Text-like types include text, digital-clock, date, weather, moon and sunrise-sunset.

The editor currently draws analog clock preview with an inline SVG mock-up. This is not proof of matching firmware analog rendering.

### 6.3 Image assets

The Web stores uploaded image data URLs in IndexedDB store watch-face-studio-assets with records shaped approximately as {id, data, name}. The design object stores only assetId.

The current JSON export serializes the design object only. It does not embed or package the IndexedDB binary/image data. Therefore an exported JSON design containing an image cannot independently reproduce that image on another browser or on the ESP32.

The .wface package pipeline must resolve asset IDs against IndexedDB, convert assets into supported device formats, include them in the package manifest and validate all references.

### 6.4 Current Web-to-device behavior

The Web connects using the legacy service and characteristic UUID, subscribes to notifications, and sends legacy text commands. The Upload action currently invokes an informational placeholder (showTransferInfo) rather than a package transfer. A successful GATT write is treated as command-send completion; the UI has no correlated protocol response or face-install confirmation.

## 7. Shared contract consistency findings

The branch contains:
- shared/schemas/watch-face.schema.json;
- shared/schemas/device-profile.schema.json;
- shared/capabilities/esp32-c3-gc9a01-240.json;
- docs/package/wface-v1.md;
- docs/protocol/protocol-v1-draft.md.

These are drafts and fixtures, not proof of parser/exporter/transport implementation.

The current Web editor uses textStyle.fontFamily and textStyle.textAlign, but the current glyphStyle definition in watch-face.schema.json does not declare those properties while using additionalProperties: false. A strict validator would reject such designs. The schema's glyph properties include fontId, fontWeight, fontSize, width, height, scaleX, scaleY, fill/stroke/background, opacity, rotation, letter spacing and visibility; the editor and schema must be reconciled explicitly.

The schema also permits object types beyond the current physical firmware renderer. The capability profile currently lists fixed analog clock, basic date, current/forecast weather, BLE/Wi-Fi, BOOT input and Light Sleep. It does not advertise dynamic text, image, shape, glyph atlas or package installation. Do not infer support from the schema enum alone.

The .wface draft proposes ZIP-compatible packaging with manifest.json, layout.json and assets. The protocol draft proposes BLE control/data characteristics, a frame envelope, negotiation and transfer state machine, but leaves final UUIDs and several implementation constants unresolved. Both documents explicitly state that parser/exporter and protocol are not implemented.

## 8. Memory/performance constraints visible from source

- One 160 × 160 RGB565 sprite consumes about 51,200 bytes before overhead.
- Weather and network worker tasks use 8,192-byte stack allocations in the current implementation.
- ArduinoJson documents, Strings, TLS clients and weather cache serialization create dynamic allocation peaks.
- The Web design schema allows up to 128 objects, but the firmware has no measured capacity for 128 dynamic objects.
- A full 240 × 240 RGB565 bitmap is 115,200 bytes. Do not allocate multiple full-screen frame buffers on ESP32-C3.
- No measured maximum package, JSON, asset, object or glyph-atlas limits are currently available.
- No physical measurements for free heap, minimum free heap, largest free block or task stack high-water marks were captured in this audit.

Set limits only after measuring actual flash filesystem capacity and worst-case heap during BLE, rendering, image streaming and weather TLS workloads.

## 9. Security and validation findings

- Treat every BLE payload and every imported design as untrusted.
- Do not log Wi-Fi passwords or transmit them in delimiter-ambiguous strings in the new protocol.
- The new protocol must authenticate or explicitly authorize privileged configuration and package-apply operations; decide the pairing/session model before exposing them.
- Audit the weather service's TLS certificate verification configuration before production use.
- Validate archive paths, package lengths, asset digests, image dimensions, object counts, numeric ranges, string lengths, nesting depth and supported capabilities.
- Never execute or compile C++/JavaScript received from the browser. Transfer declarative data and preprocessed assets only.

## 10. Phase 0 conclusions and migration decisions

| Area | Keep | Refactor/replace |
|---|---|---|
| Hardware profile | Current pin and display config as verified baseline | Centralize constants and add build-time assertions |
| Button | Existing debounce/event logic | Route through application controller events |
| Time | NTP, epoch checkpoint, timezone and GPS preference | Typed snapshots and bounded service API |
| Weather | Open-Meteo mapping, fixed snapshot, cache, HCMC default | Non-blocking service state and measured memory behavior |
| Fixed faces | Preserve as fallback/reference until dynamic renderer works | Replace active fixed face path only after renderer parity tests |
| BLE | Callback-to-bounded-queue isolation and UUID compatibility | Versioned control/data protocol and transfer manager |
| Storage | Preferences for small settings/cache | Filesystem/staging/atomic active pointer for face packages |
| Web editor | Existing editor UI and local design workflow | Shared schema validation, package builder, capability checker, transfer state machine |
| CI | Existing PlatformIO build workflow | Run schema, web syntax and firmware build checks on every feature-branch push |

### Phase 0 exit checklist

- [x] Confirm current repository, branch and HEAD.
- [x] Inspect PlatformIO configuration and hardware pins.
- [x] Inspect main application lifecycle and current display renderer.
- [x] Inspect BLE service and legacy command handler.
- [x] Inspect time, weather, button and sleep/wake modules.
- [x] Inspect current Web design object model and image storage/export behavior.
- [x] Inspect current shared schemas, Device Profile, package draft and protocol draft.
- [x] Identify major Web/schema mismatches and missing firmware integration modules.
- [ ] Run a fresh PlatformIO build on the audited HEAD.
- [ ] Run schema and Web validation scripts on the audited HEAD.
- [ ] Run physical ESP32-C3 regression tests.

The first eight items are source-inspection findings. The final three remain unverified and must not be marked passed until executed.
