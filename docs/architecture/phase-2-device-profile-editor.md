# Phase 2 — Device Profile Editor and Generic Canvas Geometry

**Branch:** `rebuild/watch-face-studio-v2`  
**Scope:** browser-side Device Profile authoring and preview geometry  
**Firmware target remains:** ESP32-C3 DevKitM-1 + GC9A01, 240 × 240, circular, RGB565

## Defect addressed

The `DEVICE PROFILE` button previously displayed a toast identifying the connected hardware. It did not allow users to define a canvas. Connected-device identity and the design's target profile are separate concepts; the editor now opens a profile dialog.

## Implemented workflow

- Edit profile name, width and height (integer pixels, 1–1024).
- Select circle, square, rectangle, rounded-square or rounded-rectangle canvas shape.
- Set rotation metadata and pixel format.
- Define top/right/bottom/left safe-area margins.
- Choose how existing elements migrate when dimensions change:
  - **Scale proportionally:** scale X/width and Y/height independently and preserve approximate typography proportions.
  - **Keep coordinates:** retain existing pixel coordinates and dimensions.
- Save custom profiles in browser local storage and reuse them in another design in the same browser.
- Import/export a Device Profile JSON file.
- Show safe-area guides on the design canvas when margins are non-zero.
- Persist the applied safe area with the design canvas so portable project export retains it.

## Geometry contract

The editor uses the active design canvas width and height as the logical coordinate system. Object percentage placement, pointer-to-canvas drag conversion, resize conversion and selection-overlay geometry all derive from those dimensions. The preview is sized to fit the available stage while preserving the profile aspect ratio.

The profile editor is not allowed to mutate the connected watch's reported hardware identity. Applying a profile changes only the current design and local profile registry.

## Firmware compatibility boundary

The current firmware is still a single verified target:
- profile ID: `esp32-c3-gc9a01-240`
- display: 240 × 240
- shape: circle
- color format: RGB565
- driver: GC9A01

Custom profiles are valid editor/design profiles, not newly supported firmware targets. The Web compatibility preflight must reject a custom profile ID, unsupported dimensions or non-circular shape before package upload. No firmware display driver, compile-time dimensions, parser limits or BLE capability claims were changed by this editor work.

Rotation and color format are stored as profile metadata; this iteration does not simulate arbitrary panel rotation or implement alternate raster backends. Safe-area guides are editor overlays, not firmware clipping masks.

## Schema and verification

- Device Profile schema and Watch Face canvas shape enums include square and rounded-square variants.
- `web/device-profile.js` is a pure validation/migration module.
- `tests/host/device_profile_test.cjs` covers valid/invalid profiles, fixed-target compatibility, safe-area bounds and coordinate/typography migration.
- `scripts/validate_web.py` guards the modal wiring and dynamic canvas-coordinate path.
- CI includes module syntax checks and the Device Profile unit tests alongside the existing schema, compatibility, host and PlatformIO checks.

## Explicit non-claims

This does not establish:
- physical support for custom display sizes on the ESP32-C3;
- real hardware rendering on arbitrary display drivers;
- physical BLE pairing or upload interoperability;
- device-side custom profile persistence;
- pixel-perfect browser/GC9A01 parity for non-default shapes;
- physical verification of orientation/rotation.

The physical board remains the final authority for firmware behavior.
