# Firmware Rebuild — Implementation Status

**Working branch:** `rebuild/watch-face-studio-v2`  
**Last source audit:** `217dfee`  
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

- Asynchronous Wi-Fi scan start/poll/result handling now lives in `WifiService`; `main.cpp` retains only a compatibility wrapper for existing BLE command callers.
- WFS capability exchange now advertises the active profile ID, canvas size/shape, bitmap limits, object/asset limits, JSON size, image support, upload availability, chunk size and current staging capacity.
- Web preflight consumes the negotiated profile and resource limits. Older WFS v1 firmware that omits the newer fields retains conservative ESP32-C3 defaults.
- Portable `.wfsproject.json` files now bundle the design and its referenced source images.
- The project envelope has a shared schema and a golden fixture. Legacy raw Watch Face JSON remains importable when referenced images already exist in the browser's IndexedDB.

### Power and diagnostics

- Light Sleep entry/wake lifecycle and Auto Sleep idle policy are owned by `SystemManager`, not embedded in the main loop.
- Runtime diagnostics report free heap, minimum free heap, largest free block, a heap-fragmentation estimate and current-task stack high-water mark.
- `main.cpp` has been reduced by moving the sleep lifecycle and idle policy into the system module.

## Remaining work

1. **Application orchestration:** `main.cpp` still owns weather fetch scheduling, boot network sequencing, Wi-Fi reconnect policy and several cross-service flags. Extract these into explicit service/coordinator modules with narrow interfaces.
2. **Display backend completion:** only the GC9A01/TFT_eSPI backend exists. The active profile is compile-time selected; runtime profile selection and alternative backends (ST7789, OLED, monochrome/e-paper, RGB888) are not implemented.
3. **Legacy UI migration:** built-in clock, weather, menu and boot-terminal renderers still use the transitional global `tft` handle.
4. **Renderer parity:** browser and firmware fonts are not pixel-identical; advanced schema elements and weather bindings remain unsupported. Keep Web preflight aligned with parser/renderer capabilities.
5. **Storage fault coverage:** add host/mock tests for interrupted writes, full storage, corrupt metadata, rollback and power loss. Preserve the two-slot rule: never replace the active package before the candidate verifies.
6. **Runtime profiling:** capture representative heap/stack snapshots during image rendering, TLS weather fetch, BLE transfer and repeated screen transitions; use measured values to tune JSON/package limits.
7. **Hardware regression:** verify BLE MTU/notifications, repeated connect/disconnect, package retry/cancel/apply, screen rendering, Wi-Fi/weather concurrency, sleep/wake and recovery on the physical ESP32-C3. CI cannot substitute for this.

## Verification record

- Commit `310ddaa`: PlatformIO Build completed successfully; schema validation, device compatibility, canvas boundary, host canvas contract, profile contract, Web asset validation, JavaScript syntax, protocol host tests and parser host tests all passed.
- Commit `2b9dc1f`: PlatformIO Build completed successfully after moving Auto Sleep policy into `SystemManager`.
- Commit `217dfee`: documentation-only follow-up; its own CI run is tracked separately.
- No physical ESP32-C3 test is claimed. The user performs board flashing and hardware regression.

## Migration constraints

- Continue only on `rebuild/watch-face-studio-v2`; do not modify `main`.
- Keep legacy BLE identifiers/commands and existing face package compatibility until a separately reviewed migration is complete.
- Keep packages declarative; do not compile or execute arbitrary C++ from the Web editor.
- Commit logical units and report exact test evidence. A queued/running workflow is not a pass.
