# Web ↔ Firmware Design Contract Audit

> **Historical snapshot:** this audit predates the current firmware parser/renderer, WFS1 package validator, BLE transfer manager and Web package uploader. Its implementation-status statements are superseded by `phase-0-current-state-addendum-2026-11-07.md` and `phase-1-architecture-contract.md`.


**Contract target:** Watch Face schema v1 and ESP32-C3 GC9A01 240×240  
**Audited Web source:** web/app.js at 50130f7  
**Schema revision:** schema typography alignment patch f5e625f  
**Status:** mapping document; firmware parser and renderer are not implemented.

## 1. Canonical ownership

- The Web editor owns the editable design document and source image files in browser storage.
- shared/schemas/watch-face.schema.json is the canonical serialized design contract.
- The Device Profile describes static display geometry.
- Runtime capabilities must be reported by the connected firmware and must not be inferred from the static Device Profile.
- The firmware owns live time/weather values, physical rendering, validation, installed package persistence and activation.

The Web must not send executable code. The ESP32 interprets a validated declarative document and preprocessed resources.

## 2. Root document mapping

| Field | Web behavior | Firmware requirement |
|---|---|---|
| schemaVersion | integer 1 | Reject unsupported major schema |
| designId | generated stable string | Validate length and allowed characters |
| name | editable design name | Bounded UTF-8 string |
| description | optional text | Bounded UTF-8 string |
| deviceProfileId | esp32-c3-gc9a01-240 | Must match supported profile |
| createdAt / updatedAt | ISO timestamps | Optional metadata; do not use for runtime |
| canvas.width / height | 240 × 240 for target | Validate against device profile |
| canvas.shape | circle | Apply device circular clipping |
| canvas.backgroundColor | #RRGGBB | Convert to RGB565 |
| canvas.clipToShape | boolean | Apply clipping if supported |
| objects | ordered object array | Parse once into bounded in-memory model |

## 3. Object mapping

All objects have id, type, name, visible, locked and transform. The firmware only needs visible and render properties; locked is an editor affordance and may be ignored at runtime.

Transform fields observed:
- x, y, width, height in display-pixel coordinate space;
- anchor;
- zIndex;
- rotation;
- opacity.

The firmware must sort deterministically by zIndex and preserve stable input order for ties. Rotation and opacity must be capability-checked; unsupported effects must be reported before installation rather than silently dropped.

Object types currently represented by the shared schema include background, image, shape, analog-clock, digital-clock, date, text, weather, moon, sunrise-sunset, gauge, progress-ring and group. This is a schema vocabulary, not a claim that all types are currently implemented by the editor or firmware. The initial device capability report should advertise only renderer paths that have passed tests.

### Type-specific data

| Type | Serialized properties | Runtime data |
|---|---|---|
| digital-clock | dataBinding.source=time, format, timeFormat, textStyle, showSeconds/showAmPm | Time service snapshot |
| analog-clock | clockMode, handConfig, transform, style/textStyle | Time service snapshot; renderer-owned hand geometry |
| date | dataBinding.source=date/weekday, format, textStyle | Time service snapshot |
| text | text or custom-text binding, textStyle | Static string or explicitly supported binding |
| weather | dataBinding, weatherFields, textStyle | Weather service snapshot and freshness |
| image | assetId | Asset reader from installed package storage |
| shape | shape, style, transform | No dynamic service required |
| moon / sunrise-sunset | dataBinding and/or textStyle | Weather/astronomy snapshot if available |
| gauge / progress-ring / group | schema-specific properties | Do not advertise until renderer semantics and tests exist |

## 4. Typography contract

The editor currently reads/writes:
- fontSize;
- fill;
- fontFamily;
- fontWeight;
- textAlign;
- letterSpacing.

The shared glyphStyle schema originally omitted fontFamily and textAlign despite additionalProperties=false. This was corrected in schema commit f5e625f by adding bounded fontFamily and textAlign enum left/center/right.

The ESP32 should not attempt to load arbitrary browser font files. Initial device implementation should support a measured built-in/system-font subset and prepared glyph bitmaps for arbitrary fonts. The Web package builder must eventually generate glyph atlas assets and glyph layout metadata for selected characters and dynamic time/date values.

Font glyph size, object bounding box and per-glyph scaling are separate concepts:
- object transform defines layout bounds;
- fontSize defines nominal glyph em size;
- optional glyph width/height/scaleX/scaleY define explicit glyph deformation;
- text alignment determines placement inside the layout bounds.

The renderer must measure actual glyph advances and apply glyph scaling. Resizing an editor box must not be interpreted as font deformation unless the serialized glyph transform explicitly requests it.

## 5. Image contract and current blocker

Current browser import reads an image as a Data URL, stores {id, data, name} in IndexedDB, and writes only assetId into the design object.

Current exportJSON() exports JSON.stringify(design) and does not collect the corresponding IndexedDB records. Thus the JSON alone is not a portable image-bearing design.

Required Web package-builder behavior:
1. Resolve every assetId against IndexedDB.
2. Decode source image in the browser.
3. Resize/crop to target display bounds.
4. Convert to an explicitly supported firmware pixel format (initial candidate RGB565 tiles).
5. Generate a stable manifest entry with dimensions, encoded length and digest.
6. Fail packaging if an asset is missing or unsupported.

Required firmware behavior:
1. Validate each manifest entry and resource bounds.
2. Stream asset bytes from staging/active storage.
3. Never allocate multiple full-screen RGB565 images.
4. Draw bounded rows/tiles through the display manager.

## 6. Compatibility blockers to resolve before parser freeze

- Keep schema and editor typography fields aligned; this patch resolves the known fontFamily/textAlign mismatch.
- Confirm every object type emitted by the editor is represented by the schema.
- Define defaults and null/missing-field behavior explicitly.
- Define rotation, opacity, text overflow, font fallback and clipping semantics.
- Decide how weather values and time formats map from dataBinding to firmware snapshots.
- Add package-level asset references and verify every image asset exists.
- Add a real schema-validation call to Web import/export and CI; current validateAgainstSchema() only fetches the schema and displays a toast. It does not validate a document.
- Replace basic validateDesign() with validation against the canonical schema plus device capability checks.
- Do not claim preview parity until browser and firmware render the same golden fixtures.

## 7. Initial implementation support tiers

Tier 1 candidate for first firmware renderer vertical slice:
- canvas background;
- static text using built-in font;
- digital clock;
- date;
- basic shape;
- z-order and basic x/y/width/height.

Tier 2 after measured renderer and memory tests:
- analog clock styles;
- weather fields;
- rotation, opacity and advanced text alignment;
- tiled RGB565 images;
- glyph atlas and dynamic glyph selection.

Tier 3 only after explicit design and test work:
- nested groups;
- gauges and progress rings;
- complex antialiasing, arbitrary effects and large image compositions.

These tiers are implementation sequencing, not permission to silently drop unsupported properties. The compatibility checker must identify unsupported required features before package transfer.

## 8. Acceptance criteria

- A Web-generated design validates against the shared schema.
- A design with an image cannot be exported as a portable package unless the image asset is included.
- A device capability response is compared against the design's required features.
- The firmware parser rejects malformed or out-of-range data without rebooting.
- The renderer consumes a parsed model without reparsing JSON every frame.
- Golden design fixtures produce predictable Web and firmware render output.
- Unsupported features produce an explicit compatibility error before apply.
