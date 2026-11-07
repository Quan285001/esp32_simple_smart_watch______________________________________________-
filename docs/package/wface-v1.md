# Watch Face Package Format — `.wface` v1 Draft

**Status:** specification draft; parser/exporter not implemented.

## 1. Container

A `.wface` file is a ZIP-compatible archive with bounded entries:

```text
example.wface
├── manifest.json
├── layout.json
├── preview.png             # optional
└── assets/
    ├── bg-main.rgb565
    ├── glyph-time.atlas
    └── ...
```

The package contains declarative JSON and binary assets only. It must never contain executable JavaScript, C++, bytecode, scripts or dynamically loaded firmware.

## 2. manifest.json

Required fields:
- `packageFormatVersion`: integer, currently 1;
- `designId`, `name`;
- `schemaVersion`;
- `createdAt`, `updatedAt` in ISO 8601 UTC;
- `deviceProfileId`;
- `canvas`: width, height, shape and color format;
- `requiredCapabilities`;
- `assets`: array of asset descriptors;
- `integrity`: digest algorithm and package/layout digest.

Each asset descriptor contains:
- stable `assetId`;
- relative archive path;
- media/encoding type;
- width and height where applicable;
- byte length;
- SHA-256 digest;
- optional color palette or tile metadata.

Archive paths must be relative, normalized and confined to the archive. Reject absolute paths, parent traversal, duplicate paths, symlinks, unsupported compression methods and duplicate asset IDs.

## 3. layout.json

The layout document must validate against `shared/schemas/watch-face.schema.json`. Coordinates are expressed in the Device Profile pixel coordinate system. Object order and z-index semantics are deterministic.

Every asset reference must resolve to exactly one manifest asset. Every required capability must be advertised by the target device before installation.

## 4. Asset formats

Initial candidate formats:
- RGB565 raw or tiled RGB565 for photo/background assets;
- indexed/palette format when measurements show it is beneficial;
- glyph atlas with metadata for custom/dynamic typography;
- PNG only for Web preview thumbnails, not as an assumed firmware decoder input.

The actual supported formats must be advertised by runtime capabilities. Compression must be benchmarked; do not assume it saves memory or CPU.

A full 240 × 240 RGB565 bitmap is 115,200 bytes. Firmware must stream or tile asset reads rather than allocating multiple full-size buffers.

## 5. Validation and resource limits

The importer and device parser must enforce:
- maximum archive byte size;
- maximum entry count;
- maximum uncompressed size per entry and total;
- maximum path length;
- maximum JSON document size and nesting depth;
- maximum object/group depth and object count;
- maximum asset dimensions;
- valid color/coordinate ranges;
- valid asset references and checksums;
- no executable content.

Limits are negotiated from firmware capabilities. Until measured on-device, no arbitrary maximum is declared safe.

## 6. Install transaction

1. Browser creates package and computes digests.
2. Device reserves staging capacity.
3. Device receives all bytes into staging.
4. Device verifies lengths and digests.
5. Device validates manifest/layout/assets against supported schema/capabilities.
6. Device verifies staged storage reads.
7. Device atomically activates the new package.
8. Device reports active design ID and digest.
9. Device retains the prior known-good design until a later successful replacement policy permits cleanup.

A failed or cancelled transfer must never modify the active design. Retrying always starts from byte zero with a new transfer ID.

## 7. Version migration

- Package format version and layout schema version are separate.
- Unknown major versions are rejected with a clear compatibility message.
- Minor compatible additions may be ignored only when explicitly marked optional.
- Unsupported required capabilities cause rejection before Apply.
- Web import may offer migration to a newer schema, but must preserve the original file and create a migrated copy.
- Firmware must not attempt an unsafe in-place migration of the active package.

## 8. Open questions before implementation

- Select ZIP library/strategy compatible with ESP32-C3 memory constraints, or use a streaming custom container if ZIP central-directory handling is too costly.
- Benchmark SHA-256 and archive validation cost on device.
- Decide whether package digest covers canonical manifest/layout bytes plus ordered asset digests, avoiding self-referential manifest hashing.
- Set package and asset limits only after flash partition and filesystem capacity are verified.
