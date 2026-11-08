# Phase 2 — Display Canvas Boundary

**Branch:** `rebuild/watch-face-studio-v2`  
**Implementation baseline:** `dcf4c2a` and subsequent parser-test commits  
**Status:** first renderer/backend separation is implemented and CI-verified.

## What changed

- `src/display/display_canvas.h` defines the logical drawing contract: dimensions, primitive geometry, text drawing, clipping, and RGB565 row output.
- `TftDisplayCanvas` in `src/display/display_manager.cpp` is the first concrete backend and owns the TFT_eSPI-specific implementation.
- `src/watchface/face_renderer.cpp` now issues drawing operations through `DisplayCanvas`; it no longer includes or calls TFT_eSPI directly.
- `src/watchface/elements/image.cpp` streams image rows through the canvas contract instead of calling `tft.pushImage()`.
- The renderer compares the face canvas dimensions with the active backend dimensions and uses those dimensions for dirty-region clipping.
- `scripts/validate_canvas_boundary.py` is part of CI and prevents the renderer/image element from regaining a direct dependency on the physical driver.

## Compatibility and memory

The refactor preserves the existing 240×240 RGB565 rendering path and the 6×8 glyph scratch strategy. The backend continues to rasterize one glyph at a time; no full-screen framebuffer or second TFT instance was introduced.

Legacy clock, weather, and menu modules still use the transitional global `tft` handle. This is intentional migration scaffolding, not the target architecture.

## Explicit limitations

This is a backend boundary, not yet a complete multi-display implementation:

1. `FaceModel`, parser, package validator, image limits, and the WFS package profile still enforce the ESP32-C3/GC9A01 240×240 contract.
2. Only the TFT_eSPI/GC9A01 backend is implemented. No ST7789, OLED, e-paper, monochrome, or RGB888 backend has been validated.
3. The image asset format remains RGB565 little-endian; other pixel formats require an explicit conversion or alternate asset contract.
4. Legacy UI modules have not yet migrated to `DisplayCanvas`.
5. No physical-board regression has been performed; CI validates compilation and source boundaries only.

## Verification

The PlatformIO workflow now checks the backend boundary and runs native Watch Face parser tests after PlatformIO installs ArduinoJson and completes the firmware build. A successful CI run is evidence of source/build correctness, not proof of physical display behavior or multi-driver compatibility.
