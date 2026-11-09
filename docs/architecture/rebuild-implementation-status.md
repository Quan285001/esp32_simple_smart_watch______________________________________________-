# Firmware Rebuild — Implementation Status

**Working branch:** `rebuild/watch-face-studio-v2`  
**Main branch:** untouched.  
**Code verification baseline:** `39157c68939708d8e3410bde5980866235e4e916`  
**Scope:** ESP32-C3 firmware, Watch Face Studio Web editor, WFS BLE v1, package/image pipeline and shared contracts.

This file distinguishes software verification from physical-board evidence. A green CI run is not a hardware test.

## 1. Implemented software scope

### Architecture and runtime

- `AppRuntime` owns the main Arduino loop; `main.cpp` retains setup and compatibility wrappers.
- `BootCoordinator` owns persistent restore and ordered startup network work.
- `WifiService` owns Wi-Fi connection monitoring, reconnect policy and asynchronous scans.
- `WeatherCoordinator` owns single-flight fetch, cooldown, location refresh and notification lifecycle.
- `SystemManager` owns Auto Sleep and Light Sleep state transitions.
- `DisplayManager` owns the physical TFT; Watch Face rendering uses the backend-neutral `DisplayCanvas` interface.
- Built-in clock/weather/menu/boot-terminal rendering still has transitional direct TFT access.

### Watch Face engine and device compatibility

- Fixed-capacity FaceModel, bounded JSON parser, renderer, element modules and package validator are integrated.
- Active profile: `esp32-c3-gc9a01-240`, 240×240 circular GC9A01, RGB565.
- Supported objects: text, digital clock, analog clock, date, weather temperature/condition, shape and image.
- Device compatibility preflight uses the negotiated profile/capabilities and rejects unsupported features before upload.
- Dynamic redraw invalidates changed regions and redraws intersecting objects in z-order.
- Font parity is not pixel-identical to the browser. Rotation, alpha blending, arbitrary fonts, compressed image decoding and advanced weather bindings are unsupported.

### Image and portable project pipeline

- Source images are persisted in browser IndexedDB.
- `.wfsproject.json` bundles the design and referenced source image data for cross-browser backup/import.
- Web preflight validates references and transparent overlays, converts device images to RGB565 little-endian and builds WFS1 packages.
- Firmware validates asset dimensions/byte lengths/offsets and streams RGB565 rows from package storage to the canvas.
- Host coverage includes image import, duplicate IDs, transparent compositing checks, RGB565 byte order, image element bounds and storage-to-canvas behavior.

### Firmware storage and activation

- Two SPIFFS package slots are governed by an NVS active-slot commit pointer.
- Transfer setup preserves the exact active/inactive slot reservation and never deletes the active package.
- Receive, flush/close, reopen, package verification and Apply are separate stages.
- Apply persists and reads back the active pointer before treating the model as installed.
- Failed model installation restores the previous pointer; first-install rollback removes the pointer.
- If rollback cannot be verified, storage is disabled until reinitialization.
- Boot does not promote an uncommitted/other slot automatically; it falls back to the built-in face if the committed slot is invalid.

### BLE and WFS communication

- Legacy BLE service and commands remain for compatibility.
- WFS v1 GATT has Control RX/TX and Data RX/TX characteristics.
- The 16-byte WFS frame codec has C++/JavaScript golden-frame coverage.
- BLE callbacks only copy bounded packets into a FreeRTOS queue; the app loop parses and processes them.
- Session generations reject queued stale packets after connect/disconnect. Repeated HELLO with session ID zero aborts cancellable prior-session transfers before establishing a new session.
- Control and Data channels share one incoming sequence space; retransmission is limited to an identical retry of the last accepted DATA chunk.
- Transfer uses stop-and-wait ACKs, bounded same-frame retry, strict transfer-field types and strict 64-character hexadecimal SHA-256 parsing for transfer, manifest-layout and asset digests.
- Failed GATT writes cancel their response waiters so stale promises do not accumulate.
- TransferManager handles receive, verify, apply, cancel and disconnect cleanup; failed cleanup preserves its concrete storage error.
- Capability exchange reports profile, canvas, elements, bitmap/object/asset limits, image support, chunk size and staging capacity.

### Diagnostics and power

- Diagnostics capture free heap, minimum free heap, largest free block, fragmentation estimate and task stack high-water values.
- Worker tasks emit memory snapshots before exit; ESP-IDF stack high-water values are reported in bytes.
- WFS receive/verify/apply timing and heap deltas are logged.
- Auto Light Sleep applies only on the clock screen after the configured idle timeout (15 seconds default), and is suppressed during active BLE or background work.
- Manual Light Sleep shuts down Wi-Fi/BLE, blanks the TFT and wakes on GPIO9. Radios remain off after wake until explicitly enabled.

## 2. Current verification record

The last fully green code/docs baseline `39157c68939708d8e3410bde5980866235e4e916` passed:

- [PlatformIO Build workflow run 42578621977](https://github.com/Quan285001/esp32_very_simple_smart_watch./actions/runs/42577483237)
  - shared JSON schema validation;
  - device compatibility and display profile/canvas boundary checks;
  - Web asset validation and `node --check web/app.js`;
  - complete host protocol/transfer/digest/session/Web tests;
  - ESP32-C3 PlatformIO firmware build;
  - Watch Face parser host tests;
  - Watch Face renderer host tests;
  - firmware artifact upload.
- [GitHub Pages deployment run 42578621242](https://github.com/Quan285001/esp32_very_simple_smart_watch./actions/runs/42577483245).

The uploaded artifact is named `esp32-c3-firmware` (artifact ID `13655913958`; expires 2026-11-23) and is available from the workflow run's **Artifacts** section. The artifact is a CI build output; it is not a record of flashing or running on the physical board.

The current branch has since received documentation-only refinements. The final workflow for the current branch head must pass before the branch is called fully green.

## 3. Remaining work and explicit limits

### Physical hardware validation — not complete

Run `docs/architecture/hardware-regression-checklist.md` on the actual ESP32-C3/GC9A01 and record:
- cold boot, serial startup and button/menu behavior;
- clock/weather rendering, NTP, Wi-Fi and cached weather;
- BLE bonding/encrypted access, WFS handshake, status and reconnect;
- small and image-bearing Watch Face upload;
- ACK retry, cancel, disconnect, invalid digest/package and reboot recovery;
- Wi-Fi/weather/transfer resource arbitration;
- Light Sleep and repeated wake cycles;
- heap, largest-block, fragmentation and task stack snapshots.

### Known engineering limitations

1. Only the ESP32-C3 + GC9A01/TFT_eSPI backend is implemented; no runtime profile switching or alternate display backend.
2. Built-in legacy UI/boot-terminal code still has direct TFT access.
3. Browser and firmware font rendering require visual comparison on hardware.
4. Alpha blending, arbitrary rotation/fonts, compressed PNG/JPEG device decoding and advanced weather fields are unsupported.
5. BLE Just Works bonding encrypts the link but does not provide MITM protection.
6. SPIFFS/NVS power-loss fault injection and actual storage-capacity behavior remain hardware-pending.
7. Physical BLE/browser interoperability, image rendering, reconnect stability, sleep/wake and long-duration reliability have not been demonstrated.

## 4. Phase assessment

- Phases 0–6: software implementation and CI acceptance are complete at the current code baseline, subject to the physical verification items above.
- Phase 7: pending physical-board testing.
- Phase 8: documentation/release polish in progress; final CI after documentation changes and hardware sign-off remain required.

## 5. Migration constraints

- Continue on `rebuild/watch-face-studio-v2`; do not modify or merge into `main` without explicit instruction.
- Preserve legacy BLE IDs/commands until a separately reviewed migration.
- Keep watch-face packages declarative; never compile or execute arbitrary code from the editor.
- Do not claim hardware testing unless the physical board has actually been flashed and the results recorded.
