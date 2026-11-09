# Phase 0 — Repository Audit

**Audit date:** 2026-11-09  
**Repository:** `Quan285001/esp32_very_simple_smart_watch.`  
**Audited branch:** `rebuild/watch-face-studio-v2`  
**Protected branch:** `main` (read-only during this audit)  
**Checkpoint:** `backup/phase0-checkpoint-2026-11-09`  
**Audit method:** GitHub source inspection of the audited branch and its existing architecture/test documentation. No source files were modified as part of the inspection itself.

> **Evidence boundary:** This is a source-level repository audit. It is not a fresh local build, CI run, browser test or physical ESP32-C3 test. Historical green workflow records in repository documentation are not treated as proof that the current branch head is green.

## 1. Executive summary

The rebuild branch already contains a substantial Watch Face Studio implementation: a declarative design model, shared JSON schemas, a fixed ESP32-C3/GC9A01 Device Profile, a backend-neutral `DisplayCanvas` contract, a firmware Watch Face renderer/parser, WFS1 package validation, BLE transfer management, dual-slot storage/apply logic, diagnostics, host tests and CI validation scripts.

A complete rewrite is **not justified by the audit evidence**. Preserve these working foundations and refactor only where a requirement or reproducible defect demands it.

The most important confirmed product defect remains the user-defined display dimensions/profile control:

- `index.html` renders a Device Profile button.
- `web/app.js` binds that button to a toast identifying the ESP32-C3/GC9A01 240×240 profile.
- There is no user-facing width/height editor attached to that action.
- The editor's blank design starts with a hard-coded 240×240 canvas.
- The Web canvas renderer and pointer conversion contain fixed 240-based geometry assumptions.
- Firmware's active display profile is compile-time fixed to 240×240 and circular.

Therefore, a profile schema and capability JSON file exist, but the product does not yet provide the complete user-authored profile workflow described by the Master Prompt. This is an end-to-end data-flow gap, not merely a missing modal.

Other major gaps against the new product specification:

1. The application is a single-page editor prototype, not yet the full multi-destination product IA described in the prompt.
2. The editor's Advanced Mode contains an explicit “being added” placeholder for advanced typography/per-glyph functionality.
3. The inspected legacy analog face redraws a 160×160 sprite and pushes it as a complete region; Smooth/Step selection is not present in that file. Do not confuse this legacy renderer with the separate Watch Face runtime renderer.
4. The WFS package transfer documented in the branch is BLE-based. Wi-Fi is used for device networking/configuration, but a Watch Face bulk Wi-Fi transport is not evidenced by the inspected docs/source.
5. Firmware support is intentionally narrower than the product vision: text, digital/analog clock, date, weather temperature/condition, basic shapes and RGB565 image.
6. The protocol documentation explicitly states that WFS characteristics are accessible without pairing/bonding/link encryption. Legacy commands can carry Wi-Fi credentials; this is a security gap to address before any consumer/production release.
7. Existing docs mark software phases 0–6 complete at a historical baseline while explicitly leaving physical hardware validation pending. Those historical phase labels must not be reused as acceptance evidence for the expanded requirements.

## 2. Repository and branch facts

| Item | Observed value | Evidence |
|---|---|---|
| Repository | `Quan285001/esp32_very_simple_smart_watch.` | GitHub repository lookup |
| Default branch | `main` | Repository metadata |
| Development branch | `rebuild/watch-face-studio-v2` | Branch lookup and branch file reads |
| Checkpoint branch | `backup/phase0-checkpoint-2026-11-09` | Created from the development branch before audit documentation write |
| PlatformIO environment | `c3_clock` | `platformio.ini` |
| Platform | `espressif32@6.9.0` | `platformio.ini` |
| Board/framework | `esp32-c3-devkitm-1` / Arduino | `platformio.ini` |
| Display | GC9A01, 240×240, RGB565, circular | `platformio.ini`, shared profile, firmware profile |
| Display pins | SCLK 4, MOSI 6, CS 7, DC 2, RST 3; BOOT 9 | `platformio.ini`, technical reference |
| Frontend packaging | No root `package.json` found; root HTML loads plain JS/CSS assets | Branch file lookup and `index.html` |
| Watch-face package | WFS1 / `.wface` | `docs/package/wface-v1.md` |
| BLE protocol | WFS BLE v1 plus legacy BLE service | `docs/protocol/protocol-v1-draft.md` |

The backup branch is a recoverable Git reference to the development branch as it existed before this audit report was committed. No branch force-update, history rewrite or write to `main` was performed.

## 3. Module inventory

Status terms below describe source-level implementation only:
- **Implemented (source):** code/module exists and its responsibility is visible.
- **Partial:** some behavior exists, but the Master Prompt's complete acceptance contract is not met.
- **Unverified:** source/docs alone cannot prove runtime or physical behavior.
- **Gap:** required behavior is not present in the inspected implementation.

| Existing module / path | Current responsibility | Source-level status | Dependencies / observations | Migration decision | Verification boundary |
|---|---|---|---|---|---|
| `index.html` | Application shell, navigation, editor, templates, library, settings and device page | Partial | Single-page app; editor, templates, designs, device and settings views exist. It is not yet the full Home/Explore/Components/Transfer Center product IA. | Refactor incrementally | Source inspected; responsive browser UX not tested |
| `web/app.js` | Editor state, templates, object editing, preview, persistence, BLE, package creation and UI handlers | Partial | Large monolithic file. The profile button only displays a toast. Design/draft storage uses localStorage keys. Fixed 240 geometry appears in canvas rendering and pointer conversion. Advanced typography remains placeholder-level. | Refactor into bounded modules after tests/contracts | Source inspected; no browser interaction test executed |
| `web/styles.css` | Responsive visual styling and editor layout | Implemented (source), UX partial | Single stylesheet; mobile/desktop styling exists. Full mobile workflow not validated. | Keep and refactor by component when needed | Source inspected |
| `web/device-compatibility.js` | Web-side validation of design against advertised device capabilities | Implemented (source), constrained | Enforces profile ID `esp32-c3-gc9a01-240`, schema v1 and current supported element/capability set. | Keep; extend only with versioned contracts | Source inspected |
| `shared/schemas/watch-face.schema.json` | Canonical design JSON schema | Implemented (source), limited v1 | Canvas width/height allow 1–1024; shape enum is only circle/rectangle/rounded-rectangle; profile snapshot/migration model is not evidenced in the inspected schema. | Keep; version/migrate deliberately | Schema inspected |
| `shared/schemas/device-profile.schema.json` | Device Profile JSON schema | Partial | Has width/height, shape, driver, color format, rotation, center, safe area and nominal limits. Shape enum is limited; no full custom mask/profile lifecycle contract. | Keep and evolve with migrations | Schema inspected |
| `shared/capabilities/esp32-c3-gc9a01-240.json` | Shared capability profile for current hardware | Implemented for one target | Fixed 240×240 circular profile; max 16 objects/assets; 240×240 bitmap limit; declared capability set. | Keep as verified target profile; do not generalize its limits to all devices | JSON inspected |
| `src/display/display_profile.h` | Compile-time firmware display constants | Implemented for one target | Fixed ID, width/height 240, bitmap limits, 16 objects/assets and circular flag. | Keep for current board; introduce backend/profile abstraction only when another physical target is actually supported | Header inspected |
| `src/display/display_canvas.h` | Backend-neutral drawing interface for Watch Face renderer | Implemented (source) | Separates Watch Face drawing API from TFT_eSPI driver. | Keep | Header inspected |
| `src/display/display_manager.*` | Physical TFT initialization and canvas backend | Implemented (source), transitional | Owns TFT_eSPI and GC9A01; legacy UI still has direct TFT access according to technical reference. | Keep; progressively isolate legacy UI | Header and technical reference inspected |
| `src/watchface/face_model.h` | Fixed-capacity firmware object model | Implemented (source), constrained | Maximum 16 objects and 16 assets; bounded strings and seven element types. | Keep for constrained target; align with shared schema via tests | Header inspected |
| `src/watchface/face_parser.*` | Bounded JSON parsing into FaceModel | Implemented per status docs | Fixed-capacity model/parser. | Keep; strengthen malformed-input and migration coverage | Status docs inspected; parser tests not run in this audit |
| `src/watchface/face_renderer.cpp` | Watch Face rendering and dynamic invalidation | Implemented (source), fidelity partial | Uses DisplayCanvas and compile-time DisplayProfileConfig. Existing docs state dynamic redraw/z-order but font parity is not pixel-identical and rotation/alpha/arbitrary fonts are unsupported. | Keep; profile decoupling and regression tests required | Renderer source header and status docs inspected |
| `src/watchface/package_validator.cpp` | WFS1 package validation | Implemented (source) | Validates package structure, bounds, digest and supported design semantics; depends on parser, renderer and display profile. | Keep; add version migration/negative fixtures | Source inspected |
| `src/communication/protocol.*` | WFS frame codec and protocol definitions | Implemented per docs | WFS v1 framing and bounded packet limits; legacy BLE remains. | Keep; security and compatibility decisions required | Protocol docs inspected; tests not run |
| `src/communication/ble_manager.*` | BLE GATT services, queues, capability exchange and control | Implemented (source), security gap | BLE callback queues and WFS characteristics exist. Protocol docs say characteristics are accessible without pairing/bonding/encryption. | Keep control path; address threat model before release | Source/docs inspected |
| `src/communication/transfer_manager.*` | Staging, chunk sequence, package verification, apply/cancel lifecycle | Implemented (source) | Uses inactive storage slot and package validator; two-slot transaction documented. | Keep; add physical reliability evidence | Source/status docs inspected |
| `src/storage/*` | Persistent settings, face slots and storage manager | Implemented per status docs | SPIFFS package slots plus NVS active-slot commit pointer; rollback semantics documented. | Keep; test power-loss and corruption on board | Status docs inspected |
| `src/app/*` | Application lifecycle, runtime, screens, boot coordination and state | Implemented (source), transitional | AppRuntime owns loop; main.cpp retains setup/wrappers; legacy UI has transitional driver coupling. | Keep and continue modularizing | Source/docs inspected |
| `src/face/face_clock.cpp` | Legacy built-in analog clock face | Partial / known performance concern | Allocates a 160×160 RGB565 sprite, clears/redraws the sprite and pushes the whole sprite each invocation. Uses fractional `millisValue` for hand angles, but exposes no Smooth/Step mode selector. | Preserve until behavior is understood; isolate and optimize only with baseline/visual tests | Source inspected; flicker/FPS not measured |
| `src/services/*` | Time, weather, Wi-Fi, BLE and coordination services | Implemented per status docs | Weather coordinator and Wi-Fi service coordinate work; weather cache and time sync documented. | Keep; test long-run and sleep/wake on hardware | Status docs inspected |
| `scripts/run_host_tests.py` and `tests/host/*` | Host protocol/digest/transfer/parser/renderer and Web tests | Implemented test harness | Script compiles selected C++ host tests; full inventory is in script and CI workflow. | Keep; expand with profile migration and visual golden fixtures | Script inspected; not executed |
| `scripts/validate_display_profile.py` | Shared/firmware profile and WFS capability consistency check | Implemented guard, limited purpose | Confirms compile-time firmware constants match the one shared profile and checks capability keys. It does not test an editable profile UI. | Keep; add profile-editor and schema integration tests | Script inspected; not executed |
| `scripts/validate_canvas_boundary.py` | Enforces DisplayCanvas dependency boundary | Implemented guard | Checks Watch Face renderer/image/app-controller boundaries and Wi-Fi scan ownership. | Keep | Script inspected; not executed |
| `.github/workflows/platformio-build.yml` | CI schema/profile/Web/host/firmware checks and artifact | Implemented workflow | Builds ESP32-C3 target and uploads firmware artifact according to README/status docs. | Keep; verify latest branch-head run before claiming green | Workflow inspected; no fresh run triggered |
| `docs/*` | Technical reference, phase matrix, protocol/package docs, hardware checklist | Substantial but partly historical | Docs explicitly distinguish CI from hardware verification; phase matrix contains historical completion claims and a hardware-pending gate. | Keep; reconcile with new traceability matrix | Docs inspected |

## 4. Confirmed defects and architectural gaps

### P0 — Display Profile editor is not implemented end-to-end

**Reproduction from source:** `#deviceSettings` click handler calls `toast(...)`; it does not open a form or manager.

Related code paths:
- `index.html`: button `id="deviceSettings"`.
- `web/app.js`: handler for `#deviceSettings`.
- `web/app.js`: `blankDesign()` initializes a fixed 240×240 canvas.
- `web/app.js`: `renderCanvas()` maps object coordinates through 240-based percentages.
- `web/app.js`: drag/resize calculations use 240-based conversion.
- `web/device-compatibility.js`: rejects any profile other than the fixed ESP32-C3 profile.
- `src/display/display_profile.h`: firmware profile is compile-time fixed.

**Decision:** treat the profile editor and generic canvas coordinate pipeline as a Phase 1/2/3 architecture blocker. Do not patch only the toast into a modal; first define the canonical profile model, profile snapshot semantics, transform contract and renderer support boundary.

### P1 — Frontend architecture and persistence

- `web/app.js` is a large single file (~85 KB at the inspected branch ref), mixing UI, state, renderer, image handling, package generation and BLE protocol.
- Design and draft keys use localStorage. The docs describe IndexedDB for projects/assets; confirm the actual image persistence implementation and reconcile docs before changing storage.
- No root `package.json` was found at the audited branch ref. The app is currently plain HTML/CSS/JavaScript, so TypeScript/build tooling is not evidenced.
- The root page contains editor/templates/library/device/settings views, but not the full navigation destinations and transfer center required by the new product prompt.

### P1 — Rendering fidelity and analog behavior

- Firmware FaceModel and display profile are fixed-capacity and single-target, which is appropriate for the current ESP32-C3 resource envelope but not a generic multi-device runtime.
- Web preview uses firmware-inspired GLCD glyph rasterization, but existing status docs acknowledge non-identical font parity.
- The legacy analog renderer redraws a 160×160 sprite as a whole. Fractional time is used in its hand-angle calculation; there is no visible mode switch or explicit Step semantics in the inspected source.
- The separate Watch Face runtime renderer uses DisplayCanvas and dynamic invalidation. Its correctness and performance require tests; do not infer that it shares the legacy renderer's implementation.
- The reported analog flicker/pixel-mask regression remains unverified in this audit and must not be marked fixed based on code presence.

### P1 — Transport and security

- WFS v1 package transfer and apply are documented over BLE.
- Wi-Fi exists for weather/network configuration, but a separate large-package Wi-Fi transport is not evidenced in the inspected source/docs.
- The protocol document states WFS GATT characteristics are accessible without pairing/bonding/link encryption; this conflicts with the new security requirements.
- Legacy command channel can carry Wi-Fi credentials, so those commands must not be treated as confidential.

### P2 — Product completeness

- Current templates are a small initial set, not the target of approximately 100 high-quality templates.
- Components are not evidenced as reusable versioned instances with typed parameters/bindings.
- Per-character live glyph objects, component overrides, declarative animation, advanced geometry/vector paths, custom clipping masks and full complication slots are not evidenced as implemented.
- Beginner/Advanced switch exists, but the Advanced panel explicitly describes several advanced controls as future work.
- PWA/service-worker/offline-install behavior was not established by the files inspected in this pass; verify before claiming full PWA support.

## 5. Existing capabilities worth preserving

1. Declarative watch-face package; no arbitrary code execution on device.
2. Shared schema and profile JSON definitions.
3. Fixed-capacity firmware model and bounded parser.
4. Backend-neutral DisplayCanvas boundary for the Watch Face runtime.
5. WFS1 package header, digest and bounds validation.
6. BLE WFS v1 control/data framing and capability exchange.
7. Transfer staging, explicit Apply and two-slot rollback design.
8. Browser-side compatibility preflight.
9. RGB565 little-endian asset conversion and firmware row streaming, as documented.
10. Weather/time/Wi-Fi services, diagnostics and sleep coordination.
11. Host test harness and CI checks.
12. Hardware regression checklist and explicit separation of hardware verification from CI.

## 6. Resource and hardware constraints

| Resource / constraint | Evidence available | Audit conclusion |
|---|---|---|
| Display raster | 240×240 RGB565 | One full raw frame is 115,200 bytes; avoid assuming multiple full-frame buffers are safe |
| Firmware object count | 16 | Enforced by shared capability profile and FaceModel |
| Firmware asset count | 16 | Enforced by shared capability profile |
| Bitmap dimensions | 240×240 maximum | Current target-specific profile, not a generic limit |
| Package staging | SPIFFS inactive slot and capability-reported staging | Actual free space must be read at runtime; no generic capacity inferred |
| Heap/fragmentation | Diagnostics fields documented | No fresh physical memory measurement available |
| Stack | Task high-water instrumentation documented | No fresh hardware stack measurements available |
| SPI throughput/FPS/power | Hardware checklist requests measurements | Not measured in this audit |
| BLE packet limits | WFS v1 protocol doc: bounded control/data frames | Protocol docs provide limits; physical throughput not verified |
| Wi-Fi bulk transfer | No implementation evidence found in inspected docs/source | Treat as not implemented until proven otherwise |

## 7. Test and verification inventory

Existing commands documented by the branch:

```sh
python scripts/run_host_tests.py
python scripts/validate_schemas.py
python scripts/validate_device_compatibility.py
python scripts/validate_display_profile.py
python scripts/validate_canvas_boundary.py
python scripts/validate_web.py
pio run -e c3_clock
```

CI workflow includes schema/profile/canvas checks, Web asset and JavaScript syntax checks, host protocol/transfer/parser/renderer tests, PlatformIO target compilation and firmware artifact upload.

**Execution status for this audit:** none of these commands was run in a local checkout during this audit. No fresh CI run was triggered. Historical workflow references in existing documentation are recorded as historical evidence only; they do not establish the status of the present branch head.

Physical ESP32-C3/GC9A01 validation remains a user-side gate. The hardware checklist must capture firmware build ID, serial logs, test conditions, heap/stack metrics, rendering observations, BLE lifecycle and transfer/apply/rollback results.

## 8. Migration decisions

| Area | Decision | Rationale |
|---|---|---|
| Existing firmware runtime/services | Keep and refactor selectively | Existing modular ownership and recovery work are valuable |
| WFS1 protocol/package | Keep v1 compatibility; version future changes | Existing codec, validator and transfer path are substantial |
| DisplayCanvas | Keep | Correct abstraction boundary for future display backends |
| Fixed ESP32 profile | Keep as current physical target profile | Do not pretend firmware can render arbitrary profiles today |
| Device Profile UI/model | Replace/extend | Existing button is a toast; profile schema lacks full lifecycle/geometry behavior |
| Web app.js | Refactor incrementally | Monolithic responsibilities create coupling; avoid a risky wholesale rewrite |
| Legacy built-in analog face | Preserve pending baseline and regression tests | It remains part of current watch behavior; optimize only with evidence |
| Existing tests/CI | Keep and extend | They provide useful regression protection |
| Existing docs | Keep; reconcile status | Preserve technical history while introducing current requirement traceability |

## 9. Recommended next execution sequence

1. Freeze the audited development branch with the checkpoint branch already created.
2. Read the actual current branch head CI result and compare it with the documented historical baseline.
3. Complete the source inventory for all firmware, web, tests and workflow paths; record any remaining uninspected files as unknown rather than assuming.
4. Write ADRs for canonical Device Profile ownership, canvas coordinate transforms, profile snapshots and single-target firmware capability handling.
5. Add failing tests that demonstrate the current profile-button defect and hard-coded 240 coordinate assumptions.
6. Define schema migrations before changing existing design documents.
7. Implement Device Profile manager and generic canvas geometry as the first coherent feature slice.
8. Only after profile/schema/geometry contracts are stable, proceed to renderer fidelity, image pipeline and typography work.
9. Do not begin a whole-repository rewrite; preserve working services, protocol, storage and CI unless tests demonstrate a concrete reason to replace them.

## 10. Phase 0 disposition

| Deliverable | Status |
|---|---|
| Repository and branch identified | Complete |
| Development branch confirmed | Complete |
| Recoverable checkpoint branch created | Complete |
| Initial hardware/build facts inspected | Complete |
| Core Web/profile/renderer/firmware/transport/docs paths inspected | Complete |
| Confirmed display-profile defect recorded | Complete |
| Existing modules and migration decisions recorded | Complete |
| Fresh local host tests / PlatformIO build | Not run in this environment |
| Latest branch-head CI status | Not verified in this audit |
| Physical hardware validation | Pending user-side ESP32-C3/GC9A01 testing |
| Full exhaustive path-by-path inventory | Partial; remaining paths must be inventoried before destructive refactoring |

**Phase 0 result:** repository inspection is sufficient to establish the initial architecture and priority blockers, but the audit is not a claim of fresh build/test success. Proceed with a test-first architecture plan; do not perform destructive rebuild operations until the remaining inventory and current branch-head CI evidence are recorded.
