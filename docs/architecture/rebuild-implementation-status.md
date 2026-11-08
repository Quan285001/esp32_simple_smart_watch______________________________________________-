# Firmware Rebuild — Implementation Status

**Working branch:** `rebuild/watch-face-studio-v2`  
**Last source audit:** `47aa20b`  
**Scope:** ESP32-C3 firmware, WFS BLE v1, Watch Face renderer, shared Web editor contract.

This document records implementation state. It is not a declaration that the full rebuild prompt is complete.

## Implemented in this pass

### Shared design and device compatibility

- Web preflight rejects several invalid or unsupported element/property combinations before packaging.
- Parser host tests cover invalid JSON field types, schema versions, unsupported properties, invalid time modes, non-ASCII text and object-count limits.
- A central compile-time active display profile now holds the profile ID, dimensions, bitmap limits, object/asset limits and shape.
- Parser, face manager, package validator, image bounds and renderer limits consume the centralized profile values.
- CI checks parity between firmware profile constants and the shared JSON device profile.

### Display/rendering boundary

- `DisplayCanvas` defines the logical drawing API for geometry, text, clipping and RGB565 row output.
- The existing TFT_eSPI/GC9A01 driver is wrapped by `TftDisplayCanvas`.
- Watch Face renderer, image element and app screen controller use the canvas API rather than directly calling TFT_eSPI.
- CI checks those source boundaries and compiles a small fake canvas implementation without TFT_eSPI.

### WFS BLE and Web editor

- `WifiService` owns station credential persistence/configuration, saved-credential enable, station disable, asynchronous scan start/poll/result handling, reconnect retry/log cadence, and connected/disconnected transition tracking. `main.cpp` retains UI-level arbitration and reacts to connection transitions to refresh time/weather.
- `WeatherCoordinator` owns the single-flight background fetch task, manual cooldown, location-refresh pending/retry behavior, periodic refresh policy, boot-fetch lifecycle and BLE completion notifications.
- `BootCoordinator` owns persistent-state restoration (storage, location, clock checkpoint, Auto Sleep and weather cache), ordered Wi-Fi/NTP/weather/lunar startup, and the handoff signal for BLE initialization. Its state transitions are protected by a FreeRTOS critical section; the main loop yields a scheduler tick after the worker signals completion so the worker can self-delete and the idle task can reclaim its stack before BLE initialization.
- `AppRuntime` now owns the Arduino `loop()` entry point, startup-to-UI handoff, button/BLE servicing, periodic service scheduling and screen animation/render cadence. `main.cpp` retains `setup()` and compatibility service wrappers.
- BLE initialization publishes `bleStackInitialized` only after the legacy GATT server, service, characteristic and advertising instance are available. Partial failures deinitialize the stack, clear WFS state and permit a later retry instead of leaving a false-ready flag. If the optional WFS GATT service alone fails to initialize, its RX queue and protocol state are rolled back while legacy BLE remains available.
- WFS write callbacks copy bounded packets into a FreeRTOS queue; protocol parsing and transfer/storage operations run in the application loop, not the Bluetooth host callback.
- WFS session epochs invalidate packets queued before connect/disconnect transitions. Session reset clears protocol sequencing and aborts any cancellable in-progress transfer. Explicit cancel commands now return a rejection when the transfer ID/state is not cancellable, rather than emitting a false cancellation event.
- BLE shutdown deinitializes the host/controller before deleting the WFS and legacy command queues, preventing queue handles from outliving their callback producer.
- Radio arbitration is applied to both the legacy BLE command path and WFS structured commands: Wi-Fi credential changes and location updates are rejected while startup/weather network work is active; location updates are also rejected during Wi-Fi scanning.
- WFS capability exchange advertises the active profile ID, canvas size/shape, bitmap limits, object/asset limits, JSON size, image support, upload availability, chunk size and current staging capacity.
- Web preflight consumes the negotiated profile and resource limits. Older WFS v1 firmware that omits the newer fields retains conservative ESP32-C3 defaults.
- Portable `.wfsproject.json` files bundle the design and its referenced source images.
- The project envelope has a shared schema and a golden fixture. Legacy raw Watch Face JSON remains importable when referenced images already exist in the browser's IndexedDB.

### Power and diagnostics

- Light Sleep entry/wake lifecycle and Auto Sleep idle policy are owned by `SystemManager`, not embedded in the main loop.
- Runtime diagnostics report free heap, minimum free heap, largest free block, a heap-fragmentation estimate and current-task stack high-water mark. The boot-network and weather-fetch workers also emit exit snapshots so their own stack high-water marks are captured after TLS/network work. ESP-IDF returns this high-water mark in bytes (unlike vanilla FreeRTOS), so the diagnostic stores the value directly without multiplying by the stack word size.
- `main.cpp` is approximately 145 lines; it retains hardware setup and compatibility wrappers while `AppRuntime` owns the main loop.

## Remaining work

1. **Application orchestration:** narrow remaining compatibility wrappers and make service/resource arbitration explicit without duplicating ownership of the TFT instance or UI state.
2. **Display backend completion:** only the GC9A01/TFT_eSPI backend exists. The active profile is compile-time selected; runtime profile selection and alternative backends (ST7789, OLED, monochrome/e-paper, RGB888) are not implemented.
3. **Legacy UI migration:** built-in clock, weather, menu and boot-terminal renderers still use the transitional global `tft` handle.
4. **Renderer parity:** browser and firmware fonts are not pixel-identical; advanced schema elements and weather bindings remain unsupported. Keep Web preflight aligned with parser/renderer capabilities.
5. **Storage fault coverage:** add host/mock tests for interrupted writes, full storage, corrupt metadata, rollback and power loss. Preserve the two-slot rule: never replace the active package before the candidate verifies.
6. **Runtime profiling:** capture representative heap/stack snapshots during image rendering, TLS weather fetch, BLE transfer and repeated screen transitions; use measured values to tune JSON/package limits.
7. **Hardware regression:** execute the step-by-step [ESP32-C3 hardware regression checklist](hardware-regression-checklist.md), including BLE MTU/notifications, repeated connect/disconnect, package retry/cancel/apply, screen rendering, Wi-Fi/weather concurrency, sleep/wake and recovery. CI cannot substitute for this.

## Verification record

- Commit `310ddaa`: PlatformIO Build completed successfully; schema validation, device compatibility, canvas boundary, host canvas contract, profile contract, Web asset validation, JavaScript syntax, protocol host tests and parser host tests all passed.
- Commit `2b9dc1f`: PlatformIO Build completed successfully after moving Auto Sleep policy into `SystemManager`.
- Commit `42502d2`: PlatformIO Build completed successfully after extracting asynchronous Wi-Fi scanning into `WifiService`.
- Commit `3a7e0b7`: PlatformIO Build completed successfully after moving Wi-Fi reconnect monitoring and connection transition tracking into `WifiService`.
- Commit `4a3348e`: PlatformIO Build and Pages deployment completed successfully after adding radio-arbitration guards to WFS and legacy BLE location/network commands.
- Commit `d161ef4`: PlatformIO Build and Pages deployment completed successfully after moving persistent-state restoration into `BootCoordinator`.
- Commit `83e8235`: PlatformIO Build and Pages deployment completed successfully after extracting the Arduino `loop()` into `AppRuntime`.
- Commit `fda3881`: PlatformIO Build and Pages deployment completed successfully after adding BLE partial-initialization rollback.
- Commit `a955927`: PlatformIO Build and Pages deployment completed successfully; host protocol tests passed and the ESP32-C3 firmware build, Watch Face parser tests and artifact upload completed successfully.
- Commit `caa1b50`: PlatformIO Build and Pages deployment completed successfully after adding WFS session-generation and queue-teardown guard tests.
- Commit `509ad06`: PlatformIO Build and Pages deployment completed successfully after correcting ESP-IDF stack high-water units and adding worker-task memory snapshots. Static build usage was 103,476 / 327,680 bytes RAM (31.6%) and 1,802,826 / 3,145,728 bytes flash (57.3%). These are link-time image metrics, not runtime free-heap measurements.
- Commit `70da942`: PlatformIO Build and Pages deployment completed successfully after making transfer cancellation return an explicit result; host BLE/session, transfer transaction, worker diagnostics and parser tests passed, and the firmware artifact was uploaded.
- Commit `484cd61`: PlatformIO Build and Pages deployment completed successfully after adding the physical-board regression checklist and clarifying BLE advertising-toggle test cases.
- Commit `11af28a`: PlatformIO Build and Pages deployment completed successfully after rolling back optional WFS GATT setup failures and releasing the unused RX queue while retaining legacy BLE.
- Commit `47aa20b`: PlatformIO Build and Pages deployment completed successfully after closing the boot-network worker handoff window; the boot coordinator guard verifies the worker logs before signalling completion and yields a tick before BLE initialization.
- No physical ESP32-C3 test is claimed. The user performs board flashing and hardware regression.

## Migration constraints

- Continue only on `rebuild/watch-face-studio-v2`; do not modify `main`.
- Keep legacy BLE identifiers/commands and existing face package compatibility until a separately reviewed migration is complete.
- Keep packages declarative; do not compile or execute arbitrary C++ from the Web editor.
- Commit logical units and report exact test evidence. A queued/running workflow is not a pass.
