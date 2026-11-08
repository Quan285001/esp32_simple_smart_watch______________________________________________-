# Watch Face Studio — Phase 0 Repository Audit

**Audit date:** 2026-11-07  
**Repository:** `Quan285001/esp32_very_simple_smart_watch.`  
**Audited ref:** `rebuild/watch-face-studio-v2`  
**Audited baseline commit:** `cd76188af1ea319c4d103f4994d6a5a7b92c59fc`  
**Audit scope:** source inspection and GitHub Actions metadata. No firmware was flashed and no physical-device behavior was verified.

## 1. Executive summary

The repository is a compact ESP32-C3 watch firmware project with a single-file browser BLE control page. It is **not yet a Watch Face Studio implementation**. It has useful foundations that should be preserved as a recoverable reference: clock/weather rendering, persistent weather cache, time persistence, one-button navigation, BLE command handling, Wi-Fi setup, and Light Sleep work.

The current web page is a device-control utility, not a visual watch-face editor. The firmware renders fixed C++ faces and has no declarative watch-face schema, asset/package manager, dynamic object renderer, bitmap transfer path, Wi-Fi bulk-transfer server, or atomic design-installation mechanism.

**Recommendation:** retain this repository and hardware target, but treat the next implementation as a controlled architectural rebuild on this dedicated branch. Do not overwrite the only known working firmware until a replacement can boot and preserve the baseline clock/weather/sleep functions.

## 2. Git safety and baseline

- The branch `rebuild/watch-face-studio-v2` exists.
- At audit time it points to the same commit as `main`: `cd76188af1ea319c4d103f4994d6a5a7b92c59fc`.
- GitHub compare reports `main...rebuild/watch-face-studio-v2` as identical (0 ahead, 0 behind).
- This gives the new branch a clean starting checkpoint; it does not yet contain branch-specific implementation work.
- The repository reports `main` as **not protected**. Keep all future work on the feature branch and avoid direct pushes to `main`.

## 3. Actual repository map

The recursive Git tree at the audited commit contains these project areas:

```text
.
├── .github/
│   └── workflows/
│       └── platformio-build.yml
├── .gitignore
├── .vscode/
│   └── extensions.json
├── README.md
├── README_PATCH.txt
├── index.html
├── platformio.ini
├── scripts/
│   ├── build_all.py
│   ├── export_firmware.py
│   ├── flash.py
│   └── patch_tft_c3.py
├── src/
│   ├── app/
│   │   ├── app_controller.cpp
│   │   ├── app_controller.h
│   │   └── app_state.h
│   ├── boot_terminal.cpp
│   ├── config.h
│   ├── face/
│   │   ├── face_clock.cpp
│   │   ├── face_info.cpp
│   │   ├── face_menu.cpp
│   │   └── face_weather.cpp
│   ├── icons.h
│   ├── input/
│   │   ├── button_manager.cpp
│   │   └── button_manager.h
│   ├── main.cpp
│   └── services/
│       ├── ble_command_handler.cpp
│       ├── ble_service.cpp
│       ├── ble_service.h
│       ├── time_service.cpp
│       ├── time_service.h
│       ├── weather_service.cpp
│       └── weather_service.h
└── tests/
    └── light_sleep_isolation/
        ├── README.md
        ├── platformio.ini
        └── src/main.cpp
```

Not present at this ref: a `web/` application source tree, TypeScript build setup, shared schema/protocol package, `include/`, `lib/`, `data/`, image assets, a watch-face package format, or firmware-side dynamic watch-face modules.

## 4. Hardware and build configuration

Verified in `platformio.ini` and `src/config.h`:

| Item | Current configuration |
|---|---|
| MCU / board | ESP32-C3 / `esp32-c3-devkitm-1` |
| Platform | `espressif32@6.9.0` |
| Framework | Arduino |
| Display | GC9A01, 240 × 240 |
| Display driver | TFT_eSPI |
| SPI | SCLK 4, MOSI 6, CS 7, DC 2, RST 3, no MISO |
| Button | GPIO 9, active-low pull-up |
| Partition | `huge_app.csv` |
| Monitor / upload | 115200 baud |
| Libraries | Adafruit GFX, ArduinoJson 7, TFT_eSPI 2.5.43 |
| Wi-Fi credentials | local `src/secrets.h`, excluded by `.gitignore` |

The historical BLE UUIDs in the specification match the current service and characteristic. The current advertised device name in firmware is `ESP32_Watch_C3`; the README still documents `ESP32_Watch`. The web page filters by the prefix `ESP32_Watch`, so the prefix currently matches the firmware name, but the documentation is stale.

### Build evidence

- GitHub Actions has a `PlatformIO Build` workflow for `c3_clock`.
- The latest PlatformIO Build on the audited baseline commit `cd76188...` completed successfully on `main` (run #42068320239).
- The audited feature branch currently has no workflow runs. The workflow triggers on pushes to `main`, pull requests targeting `main`, or manual dispatch; ordinary pushes to this feature branch do not trigger it.
- The CI job creates a temporary build-only `src/secrets.h` before running `pio run -e c3_clock`.
- This is evidence of a successful firmware compile for this commit, **not** evidence of correct behavior on the physical watch.
- The separate `light_sleep_isolation` test project is documented for manual build/flash testing. Its existence does not prove that the main firmware's complete BLE + Wi-Fi + sleep lifecycle has passed hardware testing.

## 5. Existing feature inventory and migration decisions

| Existing module | Current responsibility and evidence | Status from source inspection | Decision |
|---|---|---|---|
| `index.html` | Single-page HTML/CSS/JS Web Bluetooth control panel. Connects, syncs time/weather, sends GPS, controls sleep, reads/writes auto-sleep, scans Wi-Fi and sends credentials. | Implemented as a control utility; no editor, design gallery, image pipeline, local design database, PWA shell, or localization. Hardware behavior not reverified. | **Replace** frontend architecture; retain the old page in Git history as a behavioral reference. |
| `src/main.cpp` | Firmware lifecycle, boot sequence, Wi-Fi control/retry, weather task scheduling, BLE command servicing, screen loop, automatic/manual Light Sleep. | Substantial working foundation; highly centralized orchestration and global state. | **Refactor** into services/application coordinator; preserve behavior during migration. |
| `src/config.h` | Hardware constants, timezone/weather defaults, fixed `WeatherData`, screen enum; includes local secrets. | Useful hardware baseline; mixes configuration, UI state and data model. | **Refactor** into hardware profile, runtime config and shared firmware types. |
| `src/app/*` | Screen state, menu state, input-to-screen transitions and menu actions. | Functional fixed-screen controller; not a generic scene/object model. | **Refactor** into application state machine and input navigation. |
| `src/input/button_manager.*` | Debounced short, double and long BOOT-button events. | Clearly isolated and reusable. | **Keep/refactor** behind a small input interface. |
| `src/face/face_clock.cpp` | Fixed analog clock rendering, text/date, weather icon, connection label and animated hands. | Current fixed analog design; no schema-driven rendering. Allocates a 160 × 160, 16-bit sprite (about 51,200 bytes of pixel storage) when initialized. | **Replace renderer incrementally**; keep as visual reference and baseline test. Reassess sprite/RAM strategy. |
| `src/face/face_weather.cpp` | Weather panel, hourly forecast cards, details and animation. | Rich fixed layout, coupled to current `WeatherData` and TFT API. | **Refactor/port** useful layout and data presentation into renderer components. |
| `src/face/face_menu.cpp`, `face_info.cpp` | Fixed menu and local diagnostics display. | Functional fixed UI. | **Refactor** only where device-side menu/diagnostics remain required. |
| `src/services/ble_service.*` | BLE GATT server, advertising, callbacks, fixed-size FreeRTOS command queue, notifications, stack shutdown before sleep. | Better than doing command work in callbacks: writes are copied to a bounded queue. Still a single text characteristic and no bulk protocol. | **Replace protocol layer**, preserving lessons from callback isolation and lifecycle handling. |
| `src/services/ble_command_handler.cpp` | Parses text commands: time, weather, GPS, Wi-Fi credentials, scan, sleep and auto-sleep. | Works as a prototype command dispatcher; delimiter-based and lacks protocol version/session/sequence/checksum/capability negotiation. | **Replace** with a versioned, bounded protocol parser and explicit responses. |
| `src/services/time_service.*` | NTP, saved epoch checkpoint, UTC offset, timezone and IP-based location fallback. | Useful persistent-time behavior; network and time-zone responsibilities are coupled. | **Refactor/keep behavior**, formalize UTC/timezone contract. |
| `src/services/weather_service.*` | Open-Meteo forecast, fixed-size weather snapshot, mutex, NVS cache and optional lunar/sunrise/sunset data. | Meaningful existing feature; dynamic JSON/TLS memory use needs measurement. | **Keep data behavior and provider mapping**, refactor behind bounded data/service interfaces. |
| `scripts/patch_tft_c3.py` | Patches TFT_eSPI's ESP32-C3 SPI register base in the PlatformIO package. | Required by current C3 build as configured. | **Keep initially**, validate against future TFT_eSPI/core versions. |
| `tests/light_sleep_isolation/*` | Standalone minimal GPIO9/timer Light Sleep test. | Useful isolated hardware test, not an integration suite. | **Keep** and extend with documented full-system regression tests. |
| `scripts/build_all.py`, `export_firmware.py`, `flash.py` | Multi-board build, binary export and flash helpers. | The scripts refer to S3/C6 projects, `c6/`, `src/faces/` and `firmware/` paths that are not present in this audited tree. They appear stale or copied from a broader layout. | **Audit before reuse**; simplify or repair only after confirming intended supported boards. |
| `.github/workflows/platformio-build.yml` | Builds the root C3 firmware and uploads a binary artifact. | Useful CI build, but branch trigger coverage is limited and no frontend/test matrix exists. | **Keep and extend** for the new branch, web checks and firmware tests. |

## 6. Current web application assessment

The existing page contains no framework or module/build pipeline. Its UI has a narrow mobile column and sections for Bluetooth connection, synchronization, power management, location, device sleep, and Wi-Fi.

Current browser/device flow:
1. Request a device whose name starts with `ESP32_Watch`.
2. Connect to the known service/characteristic UUID.
3. Subscribe to notifications.
4. Send time or the latest GPS location.
5. Request auto-sleep state and automatically start a Wi-Fi scan.
6. Send commands as UTF-8 text.
7. Reassemble Wi-Fi scan notifications by concatenating text until a closing bracket is observed.

Missing Watch Face Studio capabilities:
- No device connection wizard, capability handshake, device profile editor or offline-first app shell.
- No design gallery, template browser, object canvas, layer panel, inspector, grouping, undo/redo or Beginner/Advanced modes.
- No per-character typography model.
- No image upload/crop/bitmap conversion pipeline.
- No shared Web/firmware rendering schema.
- No preview simulator or device compatibility analysis.
- No IndexedDB design/asset persistence, package import/export or version migration.
- No Transfer Center, progress state machine, transfer cancellation, atomic install or device-side validation.
- No Vietnamese/English localization or dedicated settings system.

**Conclusion:** this is a control panel, not a foundation that should be extended into the editor by accumulating more sections in `index.html`. Replace it with a structured frontend application.

## 7. Firmware, protocol and synchronization assessment

### Strengths to preserve
- BLE characteristic callback performs a bounded copy into a FreeRTOS queue rather than running command logic in the BLE host callback.
- The command queue has a fixed length and fixed command buffer size.
- Wi-Fi scanning is asynchronous and bounded to 12 returned SSIDs.
- Weather fetch runs in a background task and has single-flight guards against overlapping fetch/scan work.
- Weather data is represented as a fixed-size snapshot and copied under a mutex.
- Weather cache and last valid time are persisted in NVS.
- Light Sleep is requested through the main loop; background network work is checked before entering sleep.
- BLE is deinitialized with memory retained (`BLEDevice::deinit(false)`) so it can be initialized again after wake.
- The main loop has heartbeat and heap diagnostics.

### Architectural gaps
- BLE uses one characteristic for commands and notifications. Commands are unversioned strings.
- No explicit handshake, protocol version, device capabilities, session ID, sequence numbers, bounded binary transfer framing, checksum, transfer negotiation or transfer recovery protocol exists.
- Notification fragmentation uses 20-byte text chunks and a delay; the receiver infers the end of a Wi-Fi JSON response from the closing `]`. This is not a general-purpose framed transport.
- There is no watch-face package parser, asset staging area, package integrity validation, renderer compatibility check or atomic active-design switch.
- There is no Wi-Fi bulk data endpoint/server or implemented BLE-to-Wi-Fi handoff. Current Wi-Fi use is station connectivity for weather and credential setup.
- Wi-Fi credentials are sent inside a comma-delimited BLE command and stored in NVS. The parser does not provide a robust escaping scheme for arbitrary SSIDs/passwords containing delimiters.
- The current GATT implementation does not establish an application-level authenticated session or explicit device identity verification.
- The frontend considers a successful GATT write as command-send success; it has no generic request/response correlation layer.
- The weather UI and firmware are hard-coded to current data structures and drawing functions, not a shared declarative layout model.

### Security observations
- Both weather HTTPS clients call `setInsecure()`, disabling server-certificate verification. HTTPS transport is therefore not authenticated against a trusted server certificate.
- IP geolocation uses plain HTTP to `ip-api.com`.
- The current BLE service exposes writable control characteristics without a new application-layer pairing/session authorization design.
- These are prototype-level choices; the new synchronization protocol must include authentication/authorization decisions and avoid logging credentials.

## 8. Memory and performance assessment

Measured values are not available from source inspection alone. The code does log free heap, largest free block and minimum free heap at boot and around some network stages.

Important known allocations and risks:
- The analog clock creates a 160 × 160 RGB565 sprite: approximately 51,200 bytes for pixel storage alone, excluding allocator overhead.
- Weather and boot network tasks each request an 8,192-byte FreeRTOS stack.
- HTTPS/TLS and ArduinoJson allocate dynamically. The weather code parses from the HTTP stream rather than holding a second complete response string, which is a good optimization, but peak heap still needs measurement.
- Weather cache serialization uses a dynamic `JsonDocument` and `String`.
- Notification sending can delay the calling context while emitting multiple 20-byte chunks.
- No firmware memory budget, largest-asset budget, or stress-test report is present.

Before implementing bitmap rendering, measure free heap, largest contiguous block, minimum free heap and task stack high-water marks under at least: idle clock, BLE connected, weather TLS, Wi-Fi scan, sleep/wake, and simultaneous display/transfer load. Avoid full-screen RGB565 buffers in firmware unless measurements prove they are safe.

## 9. Known lifecycle and behavior risks to regression-test

The source includes deliberate handling for previously reported problems, but the following must be re-tested on the physical ESP32-C3:
- BLE reconnect after Light Sleep and repeated sleep/wake cycles.
- Advertising enable/disable and client disconnect behavior.
- Wi-Fi scan and weather fetch contention.
- Display refresh while network tasks run.
- Time continuity through Wi-Fi shutdown and Light Sleep.
- Weather cache retention after failed refresh.
- Boot behavior when Wi-Fi, NTP or TLS is unavailable.
- Heap fragmentation and low-memory recovery.

One startup behavior deserves attention: `setup()` launches a network startup task, and the boot UI is held until that task finishes and BLE initialization completes. The clock UI is therefore not necessarily available immediately. The code's comments and user experience should be reconciled with the intended fast-boot requirement.

## 10. CI and test coverage

Current automated coverage:
- One PlatformIO workflow that builds the C3 firmware.
- One separate manual Light Sleep isolation project.

Missing automated coverage:
- Frontend lint/typecheck/unit tests.
- Schema validation and package import/export tests.
- Protocol parser and framing tests.
- BLE state-machine tests.
- Transfer failure/retry/atomic-apply tests.
- Firmware unit tests for renderer and package validation.
- CI execution on the Watch Face Studio branch.
- End-to-end browser/device integration tests.

The successful CI run for the baseline commit establishes a compile checkpoint only. It does not establish the correctness of BLE, Wi-Fi, rendering, sleep/wake, or hardware behavior.

## 11. Keep / refactor / replace / remove summary

### Keep as baseline or reusable behavior
- Hardware pin/display configuration, subject to hardware confirmation.
- Button debouncing and event detection.
- NTP/time checkpoint behavior.
- Open-Meteo field mapping and HCMC default.
- Weather cache and fixed-size snapshot concepts.
- Asynchronous Wi-Fi scan and weather task single-flight principles.
- BLE callback-to-queue isolation.
- Light Sleep isolation test.
- TFT_eSPI C3 compatibility patch while the current library/core combination remains in use.

### Refactor
- Main application lifecycle and global state.
- Screen/menu controller.
- Weather/time services and their interfaces.
- BLE service lifecycle and diagnostics.
- CI workflow and build scripts.
- Device diagnostics into a bounded telemetry schema.

### Replace
- The single-file frontend with a modular responsive PWA.
- The text-only BLE command protocol with a versioned request/response and transfer protocol.
- Fixed C++ watch faces with a capability-aware hybrid renderer for declarative dynamic objects and bitmap assets.
- Current ad hoc synchronization UI with a Transfer Center and observable state machine.

### Remove only after migration and verification
- Obsolete fixed face code, legacy command handlers, duplicate scripts or stale board-specific helpers. Do not delete them before the replacement has passed regression tests.

## 12. Recommended architecture direction

Do not begin by generating empty directories or implementing every feature at once. Establish a small vertical slice first:

1. **Shared contract:** versioned design schema, Device Profile, renderer capability table, package manifest and protocol envelope.
2. **Web foundation:** modular app shell, localization, offline storage, connection page and a minimal watch-face preview driven by the shared schema.
3. **Firmware foundation:** preserve a bootable clock/weather build while introducing a renderer interface and bounded package storage.
4. **Protocol:** BLE handshake/status/control first; add a framed, acknowledged transfer path only after protocol limits and memory budgets are measured.
5. **Asset pipeline:** convert a single image to a tested target bitmap format in the browser, then transfer to staging storage and validate before apply.
6. **Atomic apply:** keep the old active face until the new package and assets are completely validated and persisted.
7. **Expand editor:** layers, inspector, typography, templates, undo/redo, image tools and Beginner/Advanced modes in measured increments.
8. **Regression:** clock, weather, NTP, cache, Wi-Fi, BLE reconnect, Light Sleep and button wake after each firmware integration milestone.

The first deliverable after this audit should be an architecture decision record (ADR) and a minimal shared schema/protocol proposal—not a wholesale firmware rewrite.

## 13. Phase 0 exit criteria

- [x] Confirm repository and branch.
- [x] Confirm audited branch commit and compare it with `main`.
- [x] Inventory actual tracked files and folders.
- [x] Inspect frontend, firmware, configuration, scripts, CI and the isolated sleep test.
- [x] Identify reusable functionality and migration risks.
- [x] Record compile evidence and distinguish it from hardware verification.
- [x] Create this audit report on the development branch.
- [ ] Perform a fresh local PlatformIO build from the exact branch checkout.
- [ ] Run the physical-device regression matrix with the actual watch.
- [ ] Measure heap/stack under representative workloads.

The last three items require a local development environment and/or physical hardware; they are intentionally not marked complete.

## 14. Audit conclusion

**Phase 0 source/Git inspection is complete.** The repository can support the transition if the current firmware is treated as a baseline rather than as the final architecture. The existing branch is a safe starting point but currently matches `main` exactly. No firmware or frontend source code has been changed in this audit; only this report is being added.

The next phase should define the shared design model, Device Profile, renderer capability contract, `.wface` package format and versioned BLE protocol before implementation begins.
