# Watch Face Studio — Phase 1 Architecture Foundation

**Status:** Architecture proposal, version 1.0-draft  
**Target baseline:** ESP32-C3 + GC9A01 240 × 240 circular display  
**Branch:** `rebuild/watch-face-studio-v2`  
**Depends on:** [Phase 0 Repository Audit](./repository-audit.md)

## 1. Architectural decision

Build one offline-first product with three clearly separated layers:

1. **Web application:** TypeScript + Vite + React PWA. It owns editing UX, image processing, local design storage, preview, package creation and connection workflows.
2. **Shared contracts:** versioned declarative watch-face schema, Device Profile schema, renderer capability descriptors, package manifest and communication protocol definitions.
3. **Firmware:** Arduino/C++ PlatformIO application. It owns physical display rendering, clock/weather data, device state, package validation, persistent storage and transport endpoints.

This is a proposed implementation baseline, not a claim that the current repository already contains these technologies. Introduce dependencies only when the first vertical slice needs them.

### Why this split

- The Web editor can evolve without embedding editor-specific code into the firmware.
- Both sides consume the same versioned design semantics.
- Firmware receives declarative data and assets, never executable C++ or JavaScript.
- The ESP32-C3 can reject designs requiring resources or rendering features it does not support.
- Local design and export remain usable without a connected watch or Internet access.

## 2. Repository target structure

Use a monorepo while keeping the existing root PlatformIO project buildable during migration.

```text
.
├── web/
│   ├── src/
│   │   ├── app/                 # app shell, routing, boot flow
│   │   ├── pages/               # Home, Faces, Templates, Device, Settings
│   │   ├── editor/              # canvas, selection, layers, inspector
│   │   ├── typography/          # glyph model, glyph atlas generation
│   │   ├── image/               # import, crop, conversion, dithering
│   │   ├── preview/             # deterministic preview and simulation
│   │   ├── device/              # device profile and capability model
│   │   ├── transport/           # BLE, Wi-Fi adapter, transfer manager
│   │   ├── storage/             # IndexedDB, drafts, packages
│   │   ├── templates/
│   │   ├── localization/
│   │   └── styles/
│   ├── public/
│   ├── tests/
│   ├── package.json
│   └── vite.config.ts
├── shared/
│   ├── schemas/
│   │   ├── watch-face.schema.json
│   │   └── device-profile.schema.json
│   ├── capabilities/
│   │   └── esp32-c3-gc9a01-240.json
│   ├── protocol/
│   └── package/
├── src/                         # current PlatformIO firmware, migrated incrementally
├── tests/
│   ├── light_sleep_isolation/   # existing standalone test
│   ├── protocol/
│   └── fixtures/
├── docs/
│   ├── architecture/
│   ├── protocol/
│   ├── package/
│   ├── hardware/
│   ├── testing/
│   └── user-guide/
├── scripts/
├── platformio.ini
└── README.md
```

Do not move the existing firmware into a new directory until CI, PlatformIO project discovery and flashing instructions have been updated and verified. Avoid empty placeholder folders.

## 3. Ownership boundaries

| Concern | Owner | Contract |
|---|---|---|
| Editing state, selection, undo/redo | Web editor | Internal editor model; serializes to shared schema |
| Watch-face meaning and object order | Shared schema | Versioned JSON Schema |
| Display dimensions and shape | Device Profile | Versioned profile schema |
| Device feature support | Firmware capability report | Runtime capability document |
| Web preview | Web renderer | Must follow shared coordinate, color, z-order and data-binding rules |
| Physical rendering | Firmware renderer | Implements only advertised capabilities |
| Design persistence | Web IndexedDB | Original editable design + package assets |
| Installed design persistence | Firmware storage manager | Validated active package + recoverable fallback |
| Transfer lifecycle | Web transfer manager + firmware transfer manager | Protocol state machine and device acknowledgements |
| Weather/time values | Firmware services initially | Typed data snapshot and explicit freshness/status |

The Web preview may offer richer editing affordances, but it must never silently imply that an effect will render on the physical watch. Run a compatibility check before Apply.

## 4. Rendering contract

### Coordinate system

- Origin: top-left corner.
- X increases to the right; Y increases downward.
- Units: display pixels in the target Device Profile.
- Object bounds use floating-point values in the editor; package serialization clamps/quantizes to a documented precision.
- Rotation is clockwise in degrees.
- Z-order is deterministic: sort by integer `zIndex`, then stable object order from the serialized array.
- Circular clipping is a Device Profile property, not a special case in every object.

### Hybrid rendering

Use dynamic objects for time, date, weather and simple geometry. Use bitmap assets for photos, complex artwork, unsupported custom font outlines and decorative content.

The Web editor should create bitmap/glyph assets before transfer. The firmware must not load multiple full-screen RGB565 buffers. Prefer bounded row/tile streaming from persistent storage and direct TFT writes. A raw 240 × 240 RGB565 image is 115,200 bytes; do not assume it fits as a contiguous heap allocation.

### Typography

A dynamic text object retains a data binding and a glyph-style policy. Advanced Mode may expose individual glyph instances. For arbitrary Web fonts, the export pipeline should generate glyph atlases/bitmap assets for the characters required by the binding, while keeping character positioning and per-glyph style metadata in the layout. The firmware renders the prepared glyph assets and updates the glyph selection when the underlying value changes.

This avoids requiring arbitrary browser fonts to exist on the ESP32-C3. The compatibility checker must reject or warn on unsupported glyphs, effects or missing atlas entries.

## 5. Device Profile and capability negotiation

The editor uses a default 240 × 240 circular profile for this device. A Device Profile describes geometry and nominal pixel format; it does not assert live free heap or free storage.

At connection time, the firmware reports runtime capabilities separately:
- protocol version range;
- supported object types and renderer features;
- supported image formats and maximum dimensions;
- maximum package/asset sizes;
- available persistent storage;
- current free heap and largest contiguous block;
- current active design ID and schema version.

Runtime resource values must be measured on-device and treated as volatile. The Web editor must not hardcode them into the static Device Profile.

## 6. Storage and package lifecycle

### Web side

- IndexedDB stores editable designs, drafts, asset blobs, thumbnails and local preferences.
- Cache Storage stores static application resources for PWA offline use.
- localStorage is reserved for small preferences only.
- Cloud is an optional adapter and must be opt-in; no design/photo upload by default.
- Autosave uses transactional writes and preserves a recoverable draft.

### Device side

- Keep the current active package untouched while receiving a new one.
- Receive into a staging namespace/file.
- Validate package version, lengths, asset references, checksums and renderer compatibility.
- Verify staged bytes from storage.
- Atomically switch the active package pointer only after all checks pass.
- Retain a known-good fallback and recover safely after power loss.
- Delete stale staging data on failed or explicitly restarted transfers.

## 7. Transport architecture

### BLE — primary control and small-data transport

Use BLE for discovery, connection, handshake, capabilities, time/weather commands, Wi-Fi setup, transfer negotiation, status, acknowledgements and small payloads. Keep callbacks short; callbacks enqueue bounded messages and application code processes them outside the BLE host callback.

### Wi-Fi — bulk transport when reachable

Wi-Fi is a separate transport adapter, not an assumed automatic upgrade from BLE. A bulk transfer is allowed only after:
1. BLE handshake reports a supported Wi-Fi method.
2. User completes required Wi-Fi setup.
3. Browser compatibility checks pass.
4. Phone and device network reachability is confirmed.
5. An authenticated transfer session is established.

A secure HTTPS-hosted Web App accessing a device-hosted plain HTTP endpoint can be blocked by mixed-content and local-network browser policies. A BLE connection does not grant arbitrary local-network access. Validate each supported Android/browser combination before committing to the endpoint design. If Wi-Fi cannot be reached, offer BLE only when the package fits the negotiated BLE limit; otherwise explain the limitation.

USB is not part of normal user operation.

## 8. Frontend application structure

The first screen is Device Connection / Welcome, with an explicit **Continue offline** action. The user must be able to create, preview, save and export designs without connecting a device.

Primary destinations:
- Home
- My Watch Faces
- Explore Templates
- Create / Watch Face Studio
- Device
- Transfer Center
- Settings

Desktop and mobile layouts are designed separately. Beginner and Advanced modes edit the same shared schema. The first usable vertical slice should contain a connection state, one editable template, preview, IndexedDB save/reopen and a compatibility report.

## 9. Migration strategy

1. Preserve the current C3 build as the reference baseline.
2. Add automated CI coverage for the development branch before moving firmware files.
3. Introduce shared schemas and golden fixtures.
4. Add a firmware capability-report command without changing existing clock behavior.
5. Add a minimal Web app shell and offline local storage.
6. Implement a static/dynamic renderer vertical slice with the same schema in Web and firmware.
7. Add package validation and staging storage.
8. Add the new BLE protocol beside a temporary legacy path; remove legacy only after migration tests pass.
9. Add Wi-Fi bulk transfer after browser/network compatibility is proven.
10. Expand editor, templates, image processing, glyph editing and onboarding incrementally.

## 10. Phase 1 acceptance criteria

- [x] Proposed monorepo boundaries and folder structure documented.
- [x] Shared watch-face and Device Profile contracts drafted.
- [x] Default ESP32-C3/GC9A01 profile described.
- [x] Renderer capability limits separated from runtime resource telemetry.
- [x] Package lifecycle and atomic-apply requirements documented.
- [x] BLE/Wi-Fi responsibilities and browser limitations documented.
- [x] Migration order and first vertical slice defined.
- [x] Python JSON Schema validator and initial JSON fixtures added to CI; C++ and Web codecs now share a byte-exact golden frame test.
- [x] WFS v1 protocol implementation, handshake/capability/status reporting and bounded transfer state machine are present in source; physical BLE interoperability remains unverified.

The unchecked items are implementation work and are not claimed as complete by this architecture document.
