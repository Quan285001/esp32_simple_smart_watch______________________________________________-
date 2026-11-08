# Watch Face Studio — Frontend MVP

This branch now contains the first usable Web editor slice. It replaces the old single-page BLE control dashboard at the repository root with a browser-based watch-face workspace.

## Implemented in this slice

- Responsive editor shell with Editor, Templates, and My Designs views, including a mobile quick-insert toolbar.
- Starter templates for digital, analog, weather, monochrome, and daylight layouts.
- Add text, digital time, analog clock, date, weather, image, and basic shape layers.
- Select layers, drag them on the round 240×240 preview, edit basic position/size/rotation/typography/color, toggle visibility, lock, and delete.
- Beginner and Advanced mode entry points. Advanced-specific per-glyph controls are not implemented yet.
- Draft persistence in localStorage; saved design list is local to the browser.
- Uploaded image assets are stored in IndexedDB so the browser can reload them offline.
- Import/export of the JSON design document.
- Shared schema fetch attempt and baseline design validation.
- Existing legacy BLE connection and commands are retained: time sync, weather refresh, Light Sleep, one-shot phone location sync, Wi-Fi scan/configuration, and auto-sleep setting.

## Explicitly not implemented yet

- New framed BLE/Wi-Fi package-transfer protocol and firmware receiver.
- Applying a custom face to the physical watch. The UI deliberately reports this as unavailable instead of pretending the transfer succeeded.
- Full .wface archive creation/import with embedded binary assets.
- Complete schema validation in-browser using a standards-compliant validator.
- Full per-character glyph typography controls and font/bitmap atlas generation.
- Complete Vietnamese localization, photo crop/position tools, undo/redo, and comprehensive responsive/mobile polish.
- Runtime firmware renderers for every object type declared in the shared schema.

## Run locally

The editor is static and has no npm build step:

1. Serve the repository root over HTTP (for example, python -m http.server 8000).
2. Open http://localhost:8000/ in a browser.
3. Web Bluetooth requires a supported browser and a secure context (HTTPS or localhost).

The GitHub Actions workflow checks schema fixtures, frontend asset references, JavaScript syntax, and the existing ESP32-C3 PlatformIO build.

## Important architecture boundary

The editor stores declarative design data. It does not generate or execute arbitrary C++ on the ESP32. A later compiler/package step must convert supported design objects and assets into a versioned device package, and the firmware must acknowledge validation and atomic application before the UI reports success.

## Follow-up implementation — Settings and editor reliability

Implemented on `rebuild/watch-face-studio-v2`:

- Fixed the initialization crash caused by binding `oninput` to the missing `#bgQuick` element. The optional control is now null-checked.
- Added a Settings destination on desktop and mobile, with Light, Dark, and Follow System application themes. The selection is persisted locally and remains separate from the watch-face canvas background.
- Added a language preference selector and persists the selected locale. Full translation of every existing screen is still pending.
- Added an automatic draft-save preference.
- Added direct resize handles to selected unlocked canvas objects.
- Added numeric controls for font family, weight, alignment, opacity, rotation and layer order, plus shape fill/stroke properties.
- Added layer reorder controls and bounded undo/redo history (50 snapshots maximum).
- Preserved the legacy BLE commands and the explicit warning that custom-face transfer is not yet implemented.

### Remaining work

- Full bilingual localization across all application strings.
- Full object-specific inspector controls and proper analog hand rendering.
- Undo/redo regression tests and responsive interaction tests on mobile.
- Complete object grouping, multi-select, smart guides, image crop/bitmap conversion, .wface packaging, and offline PWA validation.
- New BLE/Wi-Fi protocol and matching firmware receiver, staging validation, and atomic apply.
- Real-device verification is not claimed by these frontend changes.
