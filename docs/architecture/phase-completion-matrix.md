# Phase Completion and Acceptance Matrix

**Working branch:** `rebuild/watch-face-studio-v2`  
**Main branch:** intentionally untouched.  
**Rule:** source/host/CI completion and physical-board completion are tracked separately.

| Phase | Scope | Software implementation and CI | Physical-board status / remaining acceptance |
|---|---|---|---|
| 0 — Repository audit | Inventory, constraints, dependencies and risks | Complete Audit records and architecture constraints exist | Revisit only if scope/hardware changes |
| 1 — Architecture foundation | Module ownership, runtime lifecycle and shared contracts | Complete Main-loop, service, display, storage and BLE ownership are implemented; transitional TFT wrappers are documented | Measure runtime behavior on target |
| 2 — Web editor/project workflow | Declarative editor, local persistence, import/export, live preview and BLE connection workflow | Complete Web assets, syntax, compatibility preflight, portable project import/export and host guards pass | Browser/phone usability and Web Bluetooth need physical-device validation |
| 3 — Watch Face engine | Model, parser, renderer, canvas, dynamic redraw and compatibility | Complete Parser/renderer host tests and compatibility checks pass | Compare fonts, geometry and dynamic redraw visually on GC9A01 |
| 4 — Image/bitmap pipeline | Source assets, RGB565 conversion, WFS packaging and firmware streaming | Complete RGB565, dimensions, byte ranges, image element and Web import/package host tests pass | Upload/render opaque image-bearing faces and capture heap on hardware |
| 5 — Firmware integration | App runtime, services, display, storage, power and diagnostics | Complete ESP32-C3 PlatformIO build and full host suite pass; transfer/apply/rollback/recovery paths are integrated | Verify boot, storage, UI, memory and sleep/wake on target |
| 6 — Communication/sync | Legacy BLE, WFS framing, capabilities, transfer and session lifecycle | Complete WFS v1 codecs, Web uploader, stop-and-wait retry, strict digest/field validation, session reset and transfer lifecycle tests pass | Pairing, notifications, real upload/retry/cancel/reconnect and security UX remain unverified |
| 7 — Hardware integration | Flash, TFT, button, Wi-Fi, BLE, storage, sleep and stress | Pending No physical test can be inferred from CI | Run every applicable item in `hardware-regression-checklist.md`; attach serial logs and outcomes |
| 8 — Polish/release | Documentation, clean regression, release artifact and known-issue review | In progress Core documentation is being aligned to current source; CI artifact workflow is available | Final docs-only CI, hardware sign-off, version/release notes and final artifact after board validation |

## Software verification baseline

The current code baseline `39157c68939708d8e3410bde5980866235e4e916` passed:
- PlatformIO Build workflow run [42578621977](https://github.com/Quan285001/esp32_very_simple_smart_watch./actions/runs/42578621977), including shared schema/profile/canvas checks, Web asset/syntax checks, full host protocol/transfer/parser/renderer tests, ESP32-C3 build and firmware artifact upload.
- Pages deployment run [42578621242](https://github.com/Quan285001/esp32_very_simple_smart_watch./actions/runs/42578621242).

The firmware artifact from that run is a time-limited GitHub Actions artifact named `esp32-c3-firmware` (artifact ID `13655913958`, expires 2026-11-23). Download it from the workflow run's **Artifacts** section. No physical-board test is implied.

## Execution policy

1. Fix software blockers in the earliest unfinished phase before adding optional features.
2. Keep hardware-only evidence separate from source review, host tests and CI.
3. Run focused tests after changes, then the full host suite and PlatformIO CI at phase gates.
4. Preserve the active custom-face slot during transfer; never promote an uncommitted staging slot during recovery.
5. Keep uploaded watch-face packages declarative. Do not compile or execute arbitrary code from the editor.
6. Continue only on `rebuild/watch-face-studio-v2`; do not merge or write to `main` without a separate explicit instruction.

## Next acceptance gate

Software phases 0–6 have a passing CI baseline. The next decisive gate is physical validation using the hardware regression checklist. Do not claim hardware integration or release completion until the ESP32-C3/GC9A01 has been flashed and the results/logs have been recorded.
