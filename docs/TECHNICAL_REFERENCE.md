# Technical Reference — ESP32-C3 Watch Face Studio

**Document status:** implementation reference for the working branch `rebuild/watch-face-studio-v2`.  
**Hardware target:** ESP32-C3 DevKitM-1 + GC9A01 240×240 circular TFT.  
**Firmware environment:** PlatformIO `c3_clock`, Arduino framework, espressif32 6.9.0.  
**Audience:** firmware, Web Editor, protocol and hardware-test contributors.

> This document describes the current implementation and its verified boundaries. A successful host test or CI build is not evidence of physical-board behavior. Items marked hardware-pending must be verified on the actual ESP32-C3.

## 1. System overview

The product consists of four cooperating parts:

1. **ESP32-C3 firmware** — owns the display, button input, clock/weather UI, BLE services, transfer state machine and persistent storage.
2. **Watch Face Studio Web Editor** — edits declarative watch-face documents, previews them, stores source images in browser IndexedDB, validates against the connected device, builds a portable project or device package, and communicates through Web Bluetooth.
3. **Shared contracts** — JSON schemas, device profile/capability definitions, package format and BLE protocol definitions.
4. **Verification** — host-side C++/JavaScript/Python tests plus GitHub Actions PlatformIO build and Pages deployment. Physical integration remains a separate test stage.

The firmware target is intentionally narrow: one 240×240 RGB565 circular display, one BOOT button, clock/weather/menu functions and optional Watch Face customization. It is not a generic multi-watch operating system.

## 2. Hardware and build contract

### 2.1 Hardware mapping

| Signal | ESP32-C3 GPIO |
|---|---:|
| GC9A01 SCLK | 4 |
| GC9A01 MOSI | 6 |
| GC9A01 CS | 7 |
| GC9A01 DC | 2 |
| GC9A01 RST | 3 |
| BOOT button | 9 |
| TFT MISO | Not connected |

Display: 240×240, GC9A01, SPI, RGB565. The active shared profile ID is `esp32-c3-gc9a01-240`.

### 2.2 Toolchain

The canonical build configuration is `platformio.ini`:

- Environment: `c3_clock`
- Platform: `espressif32@6.9.0`
- Board: `esp32-c3-devkitm-1`
- Framework: Arduino
- Partition table: `huge_app.csv`
- Monitor/upload baud: 115200
- TFT_eSPI: `^2.5.43`
- Adafruit GFX: `^1.13.0`
- ArduinoJson: `^7.2.2`
- TFT compatibility patch: `scripts/patch_tft_c3.py`

Wi-Fi credentials are local-only in `src/secrets.h`; that file is gitignored. Create it from the example in the root README. Never commit real credentials.

### 2.3 Common commands

```sh
pio run -e c3_clock
pio run -e c3_clock -t upload
pio device monitor -b 115200
python scripts/run_host_tests.py
python scripts/validate_schemas.py
python scripts/validate_device_compatibility.py
python scripts/validate_display_profile.py
python scripts/validate_canvas_boundary.py
python scripts/validate_web.py
```

Use the VS Code PlatformIO toolbar or the commands above. CI executes the repository's configured checks and firmware build. The hardware owner must perform upload and board regression separately.

## 3. Firmware architecture and ownership

The application follows a single-loop orchestration model. BLE callbacks are deliberately short and must not perform storage, package parsing, rendering, network operations or blocking waits.

| Area | Primary files | Responsibility |
|---|---|---|
| Startup and loop | `src/main.cpp`, `src/app/app_runtime.*` | Hardware setup, app-loop scheduling and service dispatch |
| App state/UI | `src/app/app_controller.*`, `app_state.*` | Screen transitions and app state |
| Boot | `src/app/boot_coordinator.*` | Restore persistent state and coordinate bounded startup network work |
| Display | `src/display/display_manager.*`, `display_canvas.h`, `display_profile.h` | Own the TFT instance, display lifecycle, drawing boundary and active profile |
| Input | `src/input/button_manager.*` | Debounce and short/double/long press events |
| Built-in faces | `src/face/face_*.cpp` | Clock, weather, menu and information screens |
| Watch Face model | `src/watchface/face_model.h` | Fixed-capacity in-memory model |
| Watch Face parser | `src/watchface/face_parser.*` | Bounded JSON decode and field validation |
| Watch Face renderer | `src/watchface/face_renderer.*`, `elements/image.*` | Render validated models through DisplayCanvas |
| Package validation | `src/watchface/package_validator.*` | Validate WFS1 header, hashes, manifest, layout, assets and capabilities |
| Face activation | `src/watchface/face_manager.*` | Load and render the active custom face |
| BLE | `src/communication/ble_manager.*`, `protocol.*`, `ble_service.*` | Legacy and WFS GATT lifecycle, frame codec and event servicing |
| Commands | `src/communication/command_handler.*`, `src/services/ble_command_handler.cpp` | Legacy and structured command dispatch |
| Transfer | `src/communication/transfer_manager.*`, `transfer_sequence.h` | Receive sequencing, staging, verification, apply and cancellation |
| Storage | `src/storage/storage_manager.*`, `face_storage.*` | SPIFFS access and committed two-slot selector |
| Connectivity | `src/services/wifi_service.*`, `ble_service.*` | Wi-Fi and BLE service operations |
| Weather/time | `src/services/weather_service.*`, `weather_coordinator.*`, `time_service.*` | Forecast, cache coordination and time synchronization |
| Power/diagnostics | `src/system/system_manager.*`, `diagnostics.*`, `memory_monitor.*` | Light Sleep lifecycle, Auto Sleep and bounded telemetry |

### 3.1 Runtime invariants

- One application loop owns service scheduling and UI rendering.
- DisplayManager owns the physical TFT instance. Watch Face rendering uses DisplayCanvas; built-in legacy face code still has a transitional global TFT dependency.
- BLE callbacks copy bounded bytes into queues. They do not parse JSON, write flash or call TransferManager.
- WFS connect/disconnect changes are represented by a session-generation reset request. The application loop applies the reset and rejects packets queued under an old generation.
- Wi-Fi scan, weather fetch, credential changes, location updates and WFS transfer use explicit resource arbitration to avoid overlapping radio/storage work.
- Background tasks have bounded lifetimes and report memory snapshots before exit.
- No full-screen secondary framebuffer is introduced by the Watch Face renderer.

## 4. Display and Watch Face model

### 4.1 Active device profile

The authoritative profile is `shared/capabilities/esp32-c3-gc9a01-240.json`, checked against firmware constants in `src/display/display_profile.h`.

| Property | Current limit |
|---|---:|
| Canvas | 240 × 240 |
| Shape | Circle |
| Color format | RGB565 |
| Maximum objects | 16 |
| Maximum assets | 16 |
| Maximum bitmap width/height | 240 × 240 |
| Maximum source image asset (profile nominal) | 115,200 bytes |
| Firmware layout JSON limit | 16 KiB |
| Firmware manifest JSON limit | 8 KiB |

The profile is compile-time selected. It is not a runtime multi-display abstraction.

### 4.2 Supported object types

The current firmware model includes:

- Text
- Digital clock
- Analog clock
- Date
- Weather
- Shape
- Image (RGB565 little-endian)

WFS capability negotiation advertises the supported element set using compact codes: `t,dc,ac,d,w,s,i`. Weather condition text is separately advertised using `wc=1`.

A schema may describe a wider editor vocabulary than the target device can render. The Web compatibility layer must use the connected device's negotiated capabilities and reject unsupported designs before upload. Schema-valid does not automatically mean device-compatible.

### 4.3 Rendering constraints

- Object count and asset count are fixed-capacity.
- Current device path is RGB565, with image rows streamed from package storage.
- Alpha blending is not implemented; opacity must be exactly 1.0.
- Corner radius is integral, 0–255, and meaningful only for rounded-rectangle shapes.
- Advanced transforms, arbitrary fonts, glyph atlas overrides, transparent compositing, compressed PNG/JPEG decode and several advanced widget types are not device features.
- Web fonts/preview are not guaranteed pixel-identical to the device font renderer. Physical comparison is still required.
- Dynamic redraw uses dirty-region invalidation and redraws intersecting objects in z-order to avoid stale pixels.

## 5. Web Editor and project files

### 5.1 Source design vs portable project vs device package

These are three different representations:

1. **Watch Face JSON** — declarative design document validated by `shared/schemas/watch-face.schema.json`.
2. **Portable project** — `.wfsproject.json`, containing the design and referenced original source images. It is a browser/editor backup and interchange format.
3. **WFS1 device package** — binary `.wface` container with manifest, layout and device-ready RGB565 assets. This is what firmware accepts.

Never send the portable JSON project envelope directly to the firmware package validator.

### 5.2 Browser storage

Source images are stored in IndexedDB and referenced by asset ID. Portable project import validates the envelope, validates the design, verifies that each image reference resolves exactly once, then persists assets transactionally before loading the design. Legacy raw JSON import remains supported, but its referenced images must already exist in the current browser's IndexedDB.

### 5.3 Device preflight

Before upload, Web must:
1. Validate the document against the shared schema.
2. Read device capabilities from the live WFS connection.
3. Validate profile ID, canvas/resource limits, element types, weather features and renderer constraints.
4. Resolve source image references from IndexedDB.
5. Convert source images to RGB565 little-endian and enforce dimensions/size limits.
6. Build the WFS1 container and calculate integrity digests.
7. Transfer only after the device accepts TransferBegin.

The Web compatibility module is `web/device-compatibility.js`; Web protocol and package building are in `web/app.js`.

## 6. WFS1 package format

The package is a deterministic binary container, not ZIP and not executable code.

### 6.1 Fixed header

All integers are little-endian. Header size is 64 bytes.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `WFS1` |
| 4 | 2 | Package version (1) |
| 6 | 2 | Header size (64) |
| 8 | 4 | Manifest length |
| 12 | 4 | Layout length |
| 16 | 2 | Asset count |
| 18 | 2 | Flags (zero in v1) |
| 20 | 4 | Payload length |
| 24 | 32 | SHA-256 of entire payload |
| 56 | 4 | Header CRC32/ISO-HDLC over bytes 0–55 |
| 60 | 4 | Reserved (zero) |

Payload order: UTF-8 manifest JSON, UTF-8 layout JSON, then raw asset bytes in manifest order. Total file length must equal 64 + payload length. Trailing bytes are invalid.

### 6.2 Asset encoding

Current image encoding is `image/rgb565` / `rgb565le`: two bytes per pixel, little-endian RGB565, no compression. PNG/JPEG are browser import formats only; firmware does not decode them. Asset offsets are relative to the start of the asset region after manifest and layout. Length, bounds, ordering, non-overlap, uniqueness and digests are validated.

See `docs/package/wface-v1.md` for the complete normative package specification.

## 7. BLE communication

### 7.1 Services

Legacy BLE service remains available during migration:

- Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- Characteristic UUID: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- Device name currently used by the hardware checklist: `ESP32_Watch_C3`

WFS v1 service:

- Service: `f2c6b100-2f3a-4c1b-9d70-8e4a5c6b7100`
- Control RX: `f2c6b101-2f3a-4c1b-9d70-8e4a5c6b7100`
- Control TX: `f2c6b102-2f3a-4c1b-9d70-8e4a5c6b7100`
- Data RX: `f2c6b103-2f3a-4c1b-9d70-8e4a5c6b7100`
- Data TX: `f2c6b104-2f3a-4c1b-9d70-8e4a5c6b7100`

The Web client requests both services during the compatibility period.

### 7.2 Frame format

WFS v1 frame header is 16 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Magic `WF` |
| 2 | 1 | Major version |
| 3 | 1 | Minor version |
| 4 | 1 | Message type |
| 5 | 1 | Flags (zero in v1) |
| 6 | 2 | Sequence |
| 8 | 4 | Session ID |
| 12 | 2 | Payload length |
| 14 | 2 | CRC16/CCITT-FALSE over bytes 0–13 |

Control JSON payload is limited to 220 bytes. Complete DATA frame limit is 240 bytes; DATA payload reserves 8 bytes for transfer ID and chunk index, leaving at most 216 package bytes per chunk before connection-negotiated constraints. Web must use the chunk limit reported by the device, not assume a fixed ATT size.

Message types include HELLO/HELLO_ACK, GET_CAPABILITIES/CAPABILITIES, GET_STATUS/STATUS, COMMAND/RESPONSE/EVENT, TRANSFER_BEGIN/READY, DATA/DATA_ACK, TRANSFER_END/VERIFY_RESULT, APPLY/APPLY_RESULT, CANCEL, TRANSFER_PROGRESS, PING/PONG and ERROR.

### 7.3 Transfer behavior

- One active transfer at a time; current Web path uses stop-and-wait chunk ACKs.
- Each DATA chunk has transfer ID and monotonically increasing chunk index.
- Duplicate last-accepted chunks are acknowledged without writing twice; out-of-order chunks are rejected.
- Transfer completion requires exact length and digest verification, package/schema/capability validation, then explicit Apply.
- The inactive slot is staged; active package is not modified during reception.
- Disconnect/session loss invalidates the staging session; reconnect starts a fresh transfer rather than resuming stale chunks.
- A successful GATT write is not proof that the application accepted the command.
- Retry/reconnect resume is not promised by the current v1 uploader.

Full message schemas and field definitions are in `docs/protocol/protocol-v1-draft.md`.

## 8. Storage and recovery model

SPIFFS is mounted through StorageManager. Two fixed paths are used:

- Slot A: `/face_a.wfs`
- Slot B: `/face_b.wfs`

NVS namespace `face`, key `active`, is the commit pointer. The pointer, not file existence or file timestamp, determines the active package.

### 8.1 Installation transaction

1. Determine committed active slot and inactive staging slot.
2. Check storage capacity while preserving active package.
3. Stream incoming bytes into inactive slot.
4. Flush and close the staging writer.
5. Reopen and validate exact package length, SHA-256, manifest/layout/assets, capabilities and renderer compatibility.
6. Mark transfer Verified.
7. On explicit Apply, persist the new active-slot pointer and read it back.
8. Only report success after pointer read-back succeeds.

On activation failure, the transfer manager attempts to restore the previous pointer. If the previous state was no active custom face, rollback removes the pointer and verifies absence. If rollback cannot be verified, FaceStorage marks itself unavailable until reinitialization to prevent further writes under ambiguous metadata.

### 8.2 Boot recovery policy

Boot selects only the slot named by the NVS pointer, and only if that file exists. It does **not** promote the other slot when the pointer is missing, invalid or points to a missing file. The other slot may contain a partial package from an interrupted transfer; file existence is not proof of commitment.

If the committed package cannot be loaded/validated, the custom face is rejected and built-in face remains the safe path. The old slot is retained, but automatic fallback to it is not implemented because current metadata does not prove its transaction state.

### 8.3 What is and is not verified

Host policy/guard tests cover selector behavior, staging/active-slot separation, transfer ordering, flush/close-before-verify, apply gating and rollback source invariants. They do not emulate actual NVS/SPIFFS torn writes, power removal, flash wear or filesystem corruption. Those require a fault-injection harness or physical-board testing and must not be reported as already tested.

## 9. Connectivity, weather and power

- Default forecast location: Ho Chi Minh City, latitude 10.8231, longitude 106.6297.
- Weather provider: Open-Meteo.
- Time zone: `Asia/Ho_Chi_Minh`; NTP server `pool.ntp.org`; UTC offset +7.
- Weather refresh interval: 30 minutes when Wi-Fi is enabled and connected.
- WeatherCoordinator serializes fetch work, manages boot/manual/periodic/location refresh and notifies BLE clients.
- WifiService owns saved credential configuration, asynchronous scan lifecycle and connection transition monitoring.
- BootCoordinator restores persisted location, clock checkpoint, auto-sleep setting and weather cache before coordinating bounded startup network work.
- SystemManager owns Auto Sleep and Light Sleep state transitions.
- Radio/resource arbitration prevents conflicting network operations and WFS flash staging.

Security limitation: weather HTTPS currently uses `setInsecure()`; certificate validation is a known hardening item. BLE pairing/authentication policy is prototype-grade and must not be considered security hardened.

## 10. Diagnostics and resource budgeting

Serial diagnostics include:
- `[MEM]`: free heap, minimum free heap, largest free block, fragmentation estimate and current task stack high-water mark.
- `[PERF] WFS receive`: receive/verify duration, package bytes and heap snapshots/delta.
- `[PERF] WFS apply`: apply and total duration.
- Slow renderer diagnostics: frames taking at least 8 ms are logged with rate limiting.

ESP-IDF stack high-water values are reported in bytes in this build; do not multiply them as if they were vanilla FreeRTOS stack words.

Previous recorded link-time baseline was approximately 31.6% static RAM and 57.3% application flash. This is historical and must be refreshed from the current artifact; it is not a runtime free-heap measurement. Do not reduce worker stack sizes without real worst-case hardware measurements and a safety margin.

## 11. Test and CI strategy

### Host-side checks

The `tests/host` suite includes:
- C++ protocol codec and golden frame tests.
- C++ Watch Face parser tests with Arduino stubs.
- C++ display canvas contract tests.
- C++ storage and transfer policy tests.
- JavaScript Web protocol codec tests.
- Web device compatibility tests.
- Python source guards for app runtime, boot coordinator, BLE lifecycle/queue teardown, WFS session epochs, Wi-Fi/radio arbitration, worker diagnostics and storage recovery invariants.

Run the complete suite with `python scripts/run_host_tests.py`.

### GitHub Actions

The PlatformIO workflow validates shared schemas, display profile/canvas boundaries, Web scripts and host tests, then compiles the ESP32-C3 firmware and publishes a build artifact. Pages deployment validates/publishes the Web app separately.

A green CI run proves the configured checks and compilation passed for that commit. It does not prove BLE interoperability, TFT visual quality, runtime heap headroom, SPIFFS fault tolerance or sleep/wake behavior on a physical board.

### Hardware-only verification

Use `docs/architecture/hardware-regression-checklist.md` after flashing. It covers:
- cold boot and serial baseline;
- clock/weather/menu/button and redraw;
- Wi-Fi/NTP/weather cache;
- BLE discovery, handshake, reconnect and legacy command compatibility;
- WFS transfer, cancellation, disconnect, digest failure and reboot;
- resource arbitration;
- Light Sleep wake and repeated cycles;
- runtime heap/stack snapshots.

## 12. Known limitations and release gates

The following are explicit limitations, not silently assumed features:

1. Only ESP32-C3 + GC9A01/TFT_eSPI is implemented; no alternate display backend is validated.
2. Built-in clock/weather/menu/boot-terminal code retains transitional direct TFT access.
3. Web font rendering and firmware font rendering are not pixel-identical.
4. Alpha blending, rotation, arbitrary fonts, glyph atlases, compressed image decoding and advanced widgets are unsupported on-device.
5. Weather objects in custom faces expose only currently implemented temperature/condition bindings; the richer forecast remains in the built-in weather UI.
6. Runtime profile switching and generic multi-device support are not implemented.
7. Actual flash/NVS fault injection and power-loss recovery have not been demonstrated.
8. Physical ESP32-C3 browser-to-device upload, reconnect, sleep/wake and long-run stability remain pending.
9. Weather TLS certificate validation and stronger BLE security are future hardening work.

Do not mark the hardware integration phase complete until the board checklist has been run and logs/results recorded. Do not mark release complete until documentation, clean build, host regression, hardware regression and known-issue review are complete.

## 13. Document map

- `README.md` — quick start, wiring, build/upload and user operation.
- `docs/architecture/rebuild-implementation-status.md` — implementation status and remaining tasks.
- `docs/architecture/phase-1-architecture-contract.md` — module ownership and lifecycle contract.
- `docs/architecture/phase-2-display-canvas.md` — renderer/display boundary.
- `docs/architecture/hardware-regression-checklist.md` — physical-board test plan.
- `docs/protocol/protocol-v1-draft.md` — BLE protocol normative details.
- `docs/package/wface-v1.md` — WFS1 binary package specification.
- `docs/architecture/portable-project-format.md` — portable editor project format.
- `shared/schemas/` — shared JSON schemas.
- `shared/capabilities/esp32-c3-gc9a01-240.json` — active device capability profile.
