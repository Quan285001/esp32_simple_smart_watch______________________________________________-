# Phase Completion and Acceptance Matrix

**Working branch:** `rebuild/watch-face-studio-v2`  
**Main branch:** intentionally untouched.  
**Rule:** source/host/CI completion and physical-board completion are tracked separately.

| Phase | Scope | Software implementation and CI | Physical-board status / remaining acceptance |
|---|---|---|---|
| 0 — Repository audit | Inventory, constraints, dependencies and risks | <badge label="Complete" color="success"/> Audit records and architecture constraints exist | Revisit only if scope/hardware changes |
| 1 — Architecture foundation | Module ownership, runtime lifecycle and shared contracts | <badge label="Complete" color="success"/> Main-loop, service, display, storage and BLE ownership are implemented; transitional TFT wrappers are documented | Measure runtime behavior on target |
| 2 — Web editor/project workflow | Declarative editor, local persistence, import/export, live preview and BLE connection workflow | <badge label="Complete" color="success"/> Web assets, syntax, compatibility preflight, portable project import/export and host guards pass | Browser/phone usability and Web Bluetooth need physical-device validation |
| 3 — Watch Face engine | Model, parser, renderer, canvas, dynamic redraw and compatibility | <badge label="Complete" color="success"/> Parser/renderer host tests and compatibility checks pass | Compare fonts, geometry and dynamic redraw visually on GC9A01 |
| 4 — Image/bitmap pipeline | Source assets, RGB565 conversion, WFS packaging and firmware streaming | <badge label="Complete" color="success"/> RGB565, dimensions, byte ranges, image element and Web import/package host tests pass | Upload/render opaque image-bearing faces and capture heap on hardware |
| 5 — Firmware integration | App runtime, services, display, storage, power and diagnostics | <badge label="Complete" color="success"/> ESP32-C3 PlatformIO build and full host suite pass; transfer/apply/rollback/recovery paths are integrated | Verify boot, storage, UI, memory and sleep/wake on target |
| 6 — Communication/sync | Legacy BLE, WFS framing, capabilities, transfer and session lifecycle | <badge label="Complete" color="success"/> WFS v1 codecs, Web uploader, stop-and-wait retry, strict digest/field validation, session reset and transfer lifecycle tests pass | Pairing, notifications, real upload/retry/cancel/reconnect and security UX remain unverified |
| 7 — Hardware integration | Flash, TFT, button, Wi-Fi, BLE, storage, sleep and stress | <badge label="Pending"/> No physical test can be inferred from CI | Run every applicable item in `hardware-regression-checklist.md`; attach serial logs and outcomes |
| 8 — Polish/release | Documentation, clean regression, release artifact and known-issue review | <badge label="In progress" color="warning"/> Core documentation is being aligned to current source; CI artifact workflow is available | Final docs-only CI, hardware sign-off, version/release notes and final artifact after board validation |

## Software verification baseline

The current code baseline `39c4a55edc004c57a2cace06df0cc59e7097bb34` passed:
- PlatformIO Build workflow run [42577483237](https://github.com/Quan285001/esp32_very_simple_smart_watch./actions/runs/42577483237), including shared schema/profile/canvas checks, Web asset/syntax checks, full host protocol/transfer/parser/renderer tests, ESP32-C3 build and firmware artifact upload.
- Pages deployment run [42577483245](https://github.com/Quan285001/esp32_very_simple_smart_watch./actions/runs/42577483245).

The firmware artifact from that run is a time-limited GitHub Actions artifact named `esp32-c3-firmware`. A newer source or documentation commit requires its own workflow run before it can be called green.

## Execution policy

1. Fix software blockers in the earliest unfinished phase before adding optional features.
2. Keep hardware-only evidence separate from source review, host tests and CI.
3. Run focused tests after changes, then the full host suite and PlatformIO CI at phase gates.
4. Preserve the active custom-face slot during transfer; never promote an uncommitted staging slot during recovery.
5. Keep uploaded watch-face packages declarative. Do not compile or execute arbitrary code from the editor.
6. Continue only on `rebuild/watch-face-studio-v2`; do not merge or write to `main` without a separate explicit instruction.

## Next acceptance gate

Software phases 0–6 have a passing CI baseline. The next decisive gate is physical validation using the hardware regression checklist. Do not claim hardware integration or release completion until the ESP32-C3/GC9A01 has been flashed and the results/logs have been recorded.
