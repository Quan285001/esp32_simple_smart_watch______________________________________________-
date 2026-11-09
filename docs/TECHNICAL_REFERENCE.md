# Technical Reference — ESP32-C3 Watch Face Studio

**Working branch:** `rebuild/watch-face-studio-v2`  
**Hardware target:** ESP32-C3 DevKitM-1 + GC9A01 240×240 circular TFT  
**Firmware:** PlatformIO `c3_clock`, Arduino, `espressif32@6.9.0`  
**Verification boundary:** host tests and CI compilation are not physical-board verification.

## 1. System overview

The system has four parts:

1. **Firmware** owns the display, BOOT input, built-in clock/weather/menu UI, BLE services, Watch Face runtime, power policy and persistent storage.
2. **Watch Face Studio** is a browser editor in root `index.html` with implementation in `web/app.js` and `web/device-compatibility.js`. It edits declarative designs, previews them, stores projects/assets in IndexedDB, packages them and communicates over Web Bluetooth.
3. **Shared contracts** are in `shared/schemas/` and `shared/capabilities/`; binary package and BLE wire contracts are specified under `docs/`.
4. **Verification** combines C++/JavaScript/Python host tests, schema/profile checks, PlatformIO firmware build and GitHub Pages deployment. Board tests remain manual.

This is a single-target experimental watch, not a generic multi-vendor smartwatch OS.

## 2. Hardware and build contract

### 2.1 Wiring

| Signal | ESP32-C3 GPIO |
|---|---:|
| GC9A01 SCLK | 4 |
| GC9A01 MOSI | 6 |
| GC9A01 CS | 7 |
| GC9A01 DC | 2 |
| GC9A01 RST | 3 |
| BOOT button | 9 |
| TFT MISO | Not connected |

Display: GC9A01, 240×240, SPI, RGB565. Active profile ID: `esp32-c3-gc9a01-240`.

### 2.2 Toolchain and commands

The canonical configuration is `platformio.ini`:

- Environment `c3_clock`, board `esp32-c3-devkitm-1`
- Arduino framework, `espressif32@6.9.0`
- Partition table `huge_app.csv`
- TFT_eSPI `^2.5.43`, Adafruit GFX `^1.13.0`, ArduinoJson `^7.2.2`
- TFT C3 compatibility patch: `scripts/patch_tft_c3.py`
- Serial monitor: 115200 baud

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

Local Wi-Fi credentials belong in gitignored `src/secrets.h`. Never commit credentials. CI creates a temporary build-only configuration; this does not validate a user's local network.

## 3. Firmware architecture

The Arduino `setup()` remains in `src/main.cpp`; `src/app/app_runtime.cpp` owns `loop()` and main-loop scheduling.

| Subsystem | Primary source | Responsibility |
|---|---|---|
| App/UI | `src/app/` | Screens, state, menu and runtime orchestration |
| Boot | `src/app/boot_coordinator.*` | Persistent restore and ordered startup network work |
| Display | `src/display/` | TFT owner, canvas abstraction and compile-time display profile |
| Input | `src/input/button_manager.*` | BOOT debounce and short/double/long events |
| Built-in faces | `src/face/` | Clock, weather, menu and information screens |
| Watch Face | `src/watchface/` | Fixed-capacity model, parser, renderer, package validator and face manager |
| BLE protocol | `src/communication/` | Legacy/WFS GATT, framing, transfer and structured commands |
| Storage | `src/storage/` | SPIFFS access and two-slot active-face commit pointer |
| Connectivity | `src/services/wifi_service.*`, `ble_service.*` | Wi-Fi and BLE lifecycle |
| Weather/time | `src/services/weather_*.{cpp,h}`, `time_service.*` | Forecast, cache, refresh coordination and time sync |
| Power/diagnostics | `src/system/` | Light Sleep, Auto Sleep and bounded memory/task telemetry |

### 3.1 Runtime invariants

- The application loop owns service scheduling and UI rendering.
- BLE GATT callbacks copy bounded frames into a FreeRTOS queue. They do not parse JSON, access SPIFFS, call TransferManager or render.
- The application loop validates frames and performs transfer/storage work.
- Connect/disconnect callbacks advance a receive-generation counter. Queued packets from an old generation are discarded before processing.
- BLE teardown deinitializes the host/controller before deleting queues that callbacks can reference.
- Weather fetch, Wi-Fi scan/configuration, location changes, boot work and WFS transfer use resource arbitration.
- No full-screen secondary framebuffer is allocated for Watch Face rendering.
- DisplayManager owns the physical TFT. Watch Face elements render through DisplayCanvas; built-in legacy screens and the boot terminal still have transitional direct TFT access.

## 4. Device profile and Watch Face renderer

The authoritative profile is `shared/capabilities/esp32-c3-gc9a01-240.json`, checked against `src/display/display_profile.h`.

| Property | Current target |
|---|---:|
| Canvas | 240 × 240, circular |
| Pixel format | RGB565 |
| Maximum objects | 16 |
| Maximum image assets | 16 |
| Maximum bitmap dimensions | 240 × 240 |
| Layout JSON limit | 16 KiB |
| Manifest JSON limit | 8 KiB |

Supported element types:

- Text
- Digital clock
- Analog clock
- Date
- Weather temperature and condition
- Rectangle, circle, rounded rectangle and line
- Image (RGB565 little-endian)

The renderer uses fixed-capacity model data and dirty-region redraw. Dynamic objects invalidate their previous/new bounds; intersecting objects are redrawn in z-order to avoid stale pixels. Image rows are streamed from package storage to the canvas; the firmware does not decode a full PNG/JPEG bitmap.

### 4.1 Device limitations

- Alpha blending is not implemented; opacity must be exactly 1.0.
- Arbitrary rotation and arbitrary font families are unsupported.
- Current font-weight support is regular 400 and semibold 600.
- Rounded-rectangle corner radius is an integer in the supported byte range; non-rounded shapes cannot carry a nonzero radius.
- Weather custom-face fields are limited to temperature and condition.
- Compressed PNG/JPEG are browser import formats only. The device package contains raw RGB565 little-endian pixels.
- Browser fonts/previews are not guaranteed pixel-identical to firmware fonts.
- The profile is compile-time selected. Runtime display switching and alternate display backends are not implemented.

Schema-valid does not imply device-compatible. The Web preflight must validate against the connected device's negotiated profile and capabilities before building a package.

## 5. Web editor and project persistence

The root `index.html` loads `web/app.js`, `web/device-compatibility.js` and `web/styles.css`.

Three formats must remain distinct:

1. **Watch Face JSON** — declarative design validated by `shared/schemas/watch-face.schema.json`.
2. **Portable project** — `.wfsproject.json`; contains the design plus original source image data URLs. See `docs/architecture/portable-project-format.md`.
3. **WFS1 device package** — binary `.wface` container containing manifest, layout and device-ready RGB565 assets. This is the only representation sent to firmware.

Original images are stored in IndexedDB and referenced by asset ID. Portable import validates the envelope/design/references, persists assets transactionally and loads the design only after persistence succeeds. Legacy raw Watch Face JSON remains importable if its image references already exist locally.

Before upload, Web must:
1. Validate the design schema.
2. Read live device capabilities.
3. Check profile, canvas, object/asset limits, supported elements and renderer constraints.
4. Resolve source images from IndexedDB.
5. Convert images to RGB565 little-endian and enforce limits.
6. Build the WFS1 package and calculate SHA-256/CRC fields.
7. Start a transfer only after the device accepts the reservation.

Web Bluetooth requires a supported browser and secure context (HTTPS or localhost).

## 6. WFS1 package and persistent installation

Normative package details are in `docs/package/wface-v1.md`.

- Fixed 64-byte little-endian header; magic `WFS1`, format version 1.
- Header contains manifest/layout lengths, asset count, payload length, payload SHA-256, header CRC32 and reserved bytes.
- Payload order: UTF-8 manifest, UTF-8 layout, then raw asset bytes.
- Manifest/layout/asset digests and offsets are validated; trailing bytes, duplicate IDs, invalid references, malformed metadata and unsupported capabilities are rejected.
- Payload SHA-256 is the digest sent in TRANSFER_BEGIN and is parsed as exactly 64 hexadecimal characters.

The `huge_app.csv` partition gives SPIFFS a nominal 0xE0000-byte partition; actual usable space is lower and is read at runtime.

### 6.1 Two-slot transaction

`FaceStorage` uses two package slots and an NVS active-slot pointer as the commit record:

1. Protect the current active slot and reserve the inactive slot.
2. Remove only the inactive slot and stream incoming bytes into it.
3. Flush/close the writer, reopen the stored file and validate package/header/digests/schema/assets/capabilities.
4. Apply only from the Verified state.
5. Persist and read back the new active-slot pointer.
6. Install the validated in-memory model.
7. If model installation fails, restore the exact previous pointer; on first install, rollback removes the pointer.
8. If rollback cannot be verified, disable storage until reinitialization and log a critical diagnostic.

Boot trusts only the NVS commit pointer. A missing, invalid or dangling pointer does not promote the other slot; the built-in face is used. Automatic fallback to the previous slot is intentionally not implemented because the other slot may be an interrupted staging package. Runtime free-space checks are mandatory because two maximum-sized packages may not fit.

## 7. BLE services and WFS protocol

The existing legacy text service is retained while the Web client migrates.

### 7.1 GATT identifiers

Legacy:
- Device name: `ESP32_Watch_C3` (Web filter prefix `ESP32_Watch`)
- Service: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- Characteristic: `beb5483e-36e1-4688-b7f5-ea07361b26a8`

WFS v1:
- Service: `f2c6b100-2f3a-4c1b-9d70-8e4a5c6b7100`
- Control RX: `f2c6b101-2f3a-4c1b-9d70-8e4a5c6b7100`
- Control TX: `f2c6b102-2f3a-4c1b-9d70-8e4a5c6b7100`
- Data RX: `f2c6b103-2f3a-4c1b-9d70-8e4a5c6b7100`
- Data TX: `f2c6b104-2f3a-4c1b-9d70-8e4a5c6b7100`

Protected characteristics require encrypted access. BLE bonding is configured with IO capability NONE (Just Works): the link is encrypted, but MITM protection is not provided. This is not strong user/device authentication; production hardening requires a reviewed authorization/pairing UX and physical security testing.

### 7.2 Frame envelope

All fields are little-endian. Header is 16 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Magic `WF` (0x57, 0x46) |
| 2 | 1 | Major version (1) |
| 3 | 1 | Minor version (0) |
| 4 | 1 | Message type |
| 5 | 1 | Flags (must be zero) |
| 6 | 2 | Sequence |
| 8 | 4 | Session ID |
| 12 | 2 | Payload length |
| 14 | 2 | CRC16/CCITT-FALSE over bytes 0–13 |
| 16 | N | Payload |

Control payload limit is 220 bytes. Data frame limit is 240 bytes total, so its payload limit is 224 bytes. The DATA payload reserves 8 bytes for transfer ID and chunk index, leaving at most 216 package bytes.

A single sequence space is shared across each direction's Control/Data characteristics. The device validates the incoming control/data sequence in the application loop. DATA accepts the next sequence or an exact retry of the last accepted DATA frame. A repeated HELLO must use session ID zero; it aborts any cancellable transfer from the previous session before issuing a new session ID.

### 7.3 Handshake and capabilities

1. Web subscribes to Control TX and Data TX notifications.
2. Web sends HELLO with `minMajor`, `maxMajor`, and client name; HELLO uses session ID zero.
3. Device replies HELLO_ACK with `major`, `minor`, `device`, and nonzero `session`.
4. Web requests CAPABILITIES and STATUS; it may request FACE_STATUS.
5. Web performs compatibility preflight before enabling upload.

CAPABILITIES JSON keys:
- `sv`, `pv`: schema/package versions
- `pid`: device profile ID
- `cw`, `ch`, `cs`: canvas width/height/shape
- `bw`, `bh`: maximum bitmap width/height
- `obj`, `assets`: object/asset limits
- `json`: layout JSON limit
- `img`: image support flag
- `el`: compact element list `t,dc,ac,d,w,s,i`
- `wc`: weather-condition support
- `upload`: upload availability
- `chunk`: maximum package bytes per DATA chunk
- `stage`: current inactive-slot staging capacity

STATUS includes Wi-Fi/BLE state, weather validity/temperature, current screen, Auto Sleep seconds, power state and free heap. FACE_STATUS includes active face identity, transfer state/progress and SPIFFS usage/free bytes.

### 7.4 Transfer transaction

Firmware transfer states are `Idle`, `Receiving`, `Verifying`, `Verified`, `Applying`, `Complete`, `Failed`, `Cancelled`.

1. TRANSFER_BEGIN reserves inactive storage and returns TRANSFER_READY with transfer ID/chunk size.
2. DATA carries `transferId:uint32`, `chunkIndex:uint32`, and 1–216 bytes.
3. Web uses stop-and-wait and waits for a DATA_ACK for every chunk. It retries the same frame up to three attempts on timeout. Firmware recognizes an identical retransmission of the last accepted chunk and does not write it twice.
4. TRANSFER_END flushes/closes and validates the package, returning VERIFY_RESULT.
5. APPLY is a separate explicit operation; APPLY_RESULT reports whether the face was activated.
6. CANCEL is accepted only for cancellable states. A disconnect requests deferred cleanup in the app loop. Failed cleanup preserves its concrete error and logs a critical diagnostic.

DATA_ACK is 14 bytes: transfer ID (4), last accepted chunk index (4), total bytes written (4), status byte (1; zero is success), reserved byte (1).

A disconnect/reconnect does not resume a previous transfer. Session-generation checks reject stale queued packets; the transfer is cancelled/cleaned up where possible. A new upload starts from byte zero with a new transfer ID.

### 7.5 Error behavior

Errors are returned as bounded JSON with numeric code and a safe message. The implementation distinguishes frame/session/sequence errors, malformed control JSON/fields, resource-busy, transfer reservation, chunk, and cancel failures. The numeric code set is implementation-specific in this initial version; use the returned message and TransferManager error name rather than assuming every planned draft code is emitted.

## 8. Legacy commands and resource arbitration

Legacy commands remain available, including:
- `SYNC_TIME:<epoch>`
- `SYNC_WEATHER`
- `GPS:<lat>,<lon>`
- `SCAN_WIFI`
- `WIFI:<ssid>,<pass>`
- `SLEEP_ON`, `SLEEP_OFF`
- Auto Sleep get/set commands

The WFS v1 Command message is currently a structured dispatch path, but not every draft command/event type is implemented. Do not assume message IDs imply full command coverage.

Wi-Fi scan, weather fetch, startup work, location/credential updates and WFS transfer use explicit arbitration. Weather fetch is deferred while a WFS transfer is active; WFS transfer is rejected while conflicting resource work is active. BLE callbacks never perform blocking network work.

## 9. Power and diagnostics

- Auto Light Sleep applies only on the clock screen after the configured idle period (15 seconds default).
- Auto Sleep is suppressed while BLE is connected or background work is active.
- Manual Light Sleep is available from the menu. Before sleep, Wi-Fi and BLE are shut down; the GC9A01 remains initialized but the panel is blanked. GPIO9 wakes the ESP32-C3.
- After wake, the clock is redrawn and radios remain off until explicitly enabled.

Diagnostics include free heap, minimum free heap, largest free block, fragmentation estimate and current-task stack high-water mark. Worker-task snapshots are emitted before task exit. WFS transfer logs include receive/verify/apply durations, byte count and heap snapshots. ESP-IDF stack high-water values are reported in bytes.

## 10. Verification and release boundary

Host suite: `python scripts/run_host_tests.py`. It includes protocol golden-frame, transfer sequencing/lifecycle, digest parser, storage policy, BLE/session guards, project/image import, device compatibility, image element/RGB565, parser and renderer tests.

The PlatformIO GitHub Actions workflow checks shared schemas, device compatibility, canvas/profile boundaries, Web assets/JavaScript, host tests, the ESP32-C3 firmware build and artifact upload. Pages deployment is a separate workflow.

A green workflow proves only those checks and compilation passed for its commit. It does not prove physical BLE pairing, browser interoperability, TFT appearance, actual heap margin, SPIFFS fault tolerance, weather TLS behavior or sleep/wake stability.

## 11. Known limitations and hardware-pending work

1. Only ESP32-C3 + GC9A01/TFT_eSPI is implemented.
2. Built-in legacy UI/boot terminal still has direct TFT access outside the canvas boundary.
3. Browser/firmware font pixel parity requires physical comparison.
4. Alpha blending, rotation, arbitrary fonts, compressed image decoding and advanced weather bindings are unsupported.
5. BLE Just Works pairing has no MITM protection.
6. Actual SPIFFS/NVS fault injection and power-loss behavior have not been demonstrated.
7. Physical browser-to-device handshake, transfer, image rendering, reconnect, repeated sleep/wake and long-run stability remain unverified.

Use `docs/architecture/hardware-regression-checklist.md` after flashing. Never mark hardware integration complete from source review or CI alone.

## 12. Document map

- `README.md` — quick start and user operation
- `docs/README.md` — document index
- `docs/architecture/phase-completion-matrix.md` — phase acceptance
- `docs/architecture/rebuild-implementation-status.md` — implementation and CI record
- `docs/architecture/hardware-regression-checklist.md` — physical test plan
- `docs/protocol/protocol-v1-draft.md` — WFS BLE v1 wire contract
- `docs/package/wface-v1.md` — binary package specification
- `docs/architecture/portable-project-format.md` — editor project envelope
