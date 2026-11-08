# Portable Watch Face Studio Project Format

## Purpose

The browser editor stores source images in IndexedDB and keeps the declarative design document separate. A raw Watch Face JSON export therefore cannot move image layers to another browser by itself.

The portable project envelope solves that gap. The editor exports a JSON file with the `.wfsproject.json` suffix. It contains the normal Watch Face document plus every source image referenced by an image layer.

## Envelope

- `format`: exactly `watch-face-studio-project`.
- `version`: currently `1`.
- `design`: a schema-versioned Watch Face document validated against `shared/schemas/watch-face.schema.json`.
- `assets`: source image records with `id`, `name` and base64 data URL `data`.

The envelope is validated by `shared/schemas/watch-face-project.schema.json`. Version 1 supports up to 16 referenced assets and an 8 MB aggregate source-image data limit during import.

## Import behavior

1. Validate the project envelope and version.
2. Validate the embedded Watch Face design against the shared design schema.
3. Verify every image object references exactly one bundled asset; reject duplicate IDs and unused bundled assets.
4. Persist the bundled assets to IndexedDB in one read/write transaction.
5. Load the design only after validation and asset persistence succeed.

Legacy raw Watch Face JSON files remain importable. If a legacy file contains image layers, their asset IDs must already exist in the current browser's IndexedDB; otherwise import reports the missing asset rather than silently producing a broken design.

## Device upload

A portable project file is an editor interchange/backup format, not a firmware package. Before upload, the Web app validates the design against the connected device's negotiated profile and converts source images to the firmware's RGB565 asset representation. The WFS1 package builder then creates the binary manifest/layout/assets payload and uses the existing verified transfer protocol.

Never send the portable JSON envelope directly to the firmware package validator.
