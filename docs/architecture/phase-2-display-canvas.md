# Phase 2 — Display Canvas Boundary

**Branch:** `rebuild/watch-face-studio-v2`  
**Implementation baseline:** `310ddaa`  
**Status:** first renderer/backend separation is implemented and CI-verified.

## What changed

- `src/display/display_canvas.h` defines the logical drawing contract: dimensions, primitive geometry, text drawing, clipping, and RGB565 row output.
- `TftDisplayCanvas` in `src/display/display_manager.cpp` is the first concrete backend and owns the TFT_eSPI-specific implementation.
- `src/watchface/face_renderer.cpp` now issues drawing operations through `DisplayCanvas`; it no longer includes or calls TFT_eSPI directly.
- `src/watchface/elements/image.cpp` streams image rows through the canvas contract instead of calling `tft.pushImage()`.
- `src/app/app_controller.cpp` uses the canvas for screen clearing and no longer imports TFT_eSPI.
- `src/display/display_profile.h` centralizes the active profile ID, dimensions, bitmap limits, object/asset counts, and display shape.
- WFS capability exchange advertises the active profile and resource limits; Web preflight consumes those negotiated values.
- The shared JSON profile is checked against the firmware constants by `scripts/validate_display_profile.py`.
- The renderer compares the face canvas dimensions with the active backend dimensions and uses those dimensions for dirty-region clipping.
- `scripts/validate_canvas_boundary.py` is part of CI and prevents the renderer, image element, and app controller from regaining a direct dependency on the physical driver.
- A native host test compiles the `DisplayCanvas` interface without linking TFT_eSPI.

## Compatibility and memory

The refactor preserves the existing 240×240 RGB565 rendering path and the 6×8 glyph scratch strategy. The backend continues to rasterize one glyph at a time; no full-screen framebuffer or second TFT instance was introduced.

Legacy clock, weather, and menu modules still use the transitional global `tft` handle. This is intentional migration scaffolding, not the target architecture.

## Explicit limitations

This is a backend boundary, not yet a complete multi-display implementation:

1. `FaceModel`, parser, package validator, and image limits currently enforce the active compile-time profile, which is still ESP32-C3/GC9A01 240×240. The constants are centralized, but selecting a different profile at runtime is not implemented.
2. Only the TFT_eSPI/GC9A01 backend is implemented. No ST7789, OLED, e-paper, monochrome, or RGB888 backend has been validated.
3. The image asset format remains RGB565 little-endian; other pixel formats require an explicit conversion or alternate asset contract.
4. Legacy clock, weather, and menu rendering modules still use the transitional global `tft` handle; only the Watch Face renderer, image element, and app controller are on the logical canvas.
5. No physical-board regression has been performed; CI validates compilation and source boundaries only.

## Verification

The PlatformIO workflow validates shared schemas, the firmware/shared profile contract, the canvas source boundary, a native canvas-interface compile test, protocol host tests, parser host tests, Web syntax, and the ESP32-C3 PlatformIO build. A successful CI run is evidence of source/build correctness, not proof of physical display behavior or multi-driver compatibility.
