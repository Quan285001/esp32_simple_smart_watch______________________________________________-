# Watch Face Package Format — WFS1 / .wface

**Status:** selected v1 container design; firmware parser and Web exporter are still under implementation.  
**Target:** ESP32-C3, GC9A01 240×240, SPIFFS staging storage.

## 1. Decision

Use a small custom streaming container instead of ZIP.

Reasons:
- The firmware must validate and write package bytes incrementally.
- A ZIP implementation would require central-directory handling and decompression policy on a memory-constrained MCU.
- The current design only needs two JSON documents plus raw device-ready assets.
- The browser can assemble a Blob from header, manifest, layout and asset byte arrays without a ZIP dependency.

The .wface extension is a transportable binary container. It is not executable code.

## 2. Container layout

All integers are little-endian. The fixed header is exactly 64 bytes.

| Offset | Size | Field | Rule |
|---:|---:|---|---|
| 0 | 4 | Magic | ASCII WFS1 |
| 4 | 2 | Package format version | 1 |
| 6 | 2 | Header size | 64 |
| 8 | 4 | Manifest length | UTF-8 JSON byte length |
| 12 | 4 | Layout length | UTF-8 JSON byte length |
| 16 | 2 | Asset count | Must equal manifest assets length |
| 18 | 2 | Flags | Must be zero in v1 |
| 20 | 4 | Payload length | Manifest + layout + all asset bytes |
| 24 | 32 | Payload SHA-256 | Digest of the complete payload bytes |
| 56 | 4 | Header CRC32 | CRC-32/ISO-HDLC over bytes 0–55 |
| 60 | 4 | Reserved | Must be zero |

Payload order is deterministic:

1. manifest.json bytes (UTF-8, no BOM);
2. layout.json bytes (UTF-8, no BOM);
3. raw asset bytes in the same order as manifest.assets.

Total package length must equal 64 + payloadLength exactly. Trailing bytes are rejected.

## 3. Manifest

Example:

```json
{
  "packageFormatVersion": 1,
  "designId": "design_abc123",
  "name": "Minimal Digital",
  "schemaVersion": 1,
  "deviceProfileId": "esp32-c3-gc9a01-240",
  "requiredCapabilities": ["clock.digital", "text.basic"],
  "layoutSha256": "lowercase-hex-sha256",
  "assets": [
    {
      "assetId": "asset_background",
      "mediaType": "image/rgb565",
      "encoding": "rgb565le",
      "width": 240,
      "height": 240,
      "byteLength": 115200,
      "offset": 0,
      "sha256": "lowercase-hex-sha256"
    }
  ]
}
```

Asset offset is relative to the first byte after manifest and layout. The absolute package offset is:

64 + manifestLength + layoutLength + asset.offset

Asset entries must be ordered by ascending offset, non-overlapping, contiguous, and wholly inside payloadLength. Asset IDs must be unique. Layout assetId references must resolve to exactly one manifest entry.

## 4. Initial asset encoding

Initial device bitmap encoding:
- mediaType: image/rgb565
- encoding: rgb565le
- RGB565 pixels in little-endian byte order
- no compression in v1
- dimensions and byteLength must agree exactly (width × height × 2)

The Web package builder must resize/crop to target bounds, convert colors, and optionally dither before packaging. The firmware must stream rows/tiles from storage; it must not allocate a full-screen decoded bitmap plus a second full-screen buffer.

PNG/JPEG are browser import formats only. Firmware support for compressed PNG/JPEG is not implied.

Glyph atlas assets will use a separate explicit encoding and metadata contract in a later compatible package revision; do not overload the RGB565 asset type.

## 5. Integrity and validation

- Header CRC32 detects malformed header fields.
- Payload SHA-256 covers manifest, layout and every asset byte in the exact payload order.
- layoutSha256 covers the exact layout.json UTF-8 bytes.
- Each asset SHA-256 covers its exact raw bytes.
- Firmware must check declared lengths before reading or allocating.
- The parser must reject unknown major package versions, nonzero v1 flags, invalid offsets, duplicate IDs, missing asset references, malformed JSON, schema errors, unsupported capabilities and checksum mismatches.

The manifest and layout must be parsed with bounded sizes and a bounded nesting depth. Package limits are supplied by runtime capabilities and available staging capacity, not guessed by the Web app.

## 6. Atomic installation

The existing huge_app partition table provides a nominal 0xE0000-byte SPIFFS partition. Actual usable space is lower due to filesystem metadata and must be read from SPIFFS at runtime.

Use two package slots:
- active slot;
- inactive slot used for the next transfer.

The inactive slot may be overwritten only after the active slot is identified and protected. If the complete new package cannot fit while preserving the active package, reject the transfer before receiving bytes.

Commit sequence:
1. reserve inactive slot;
2. stream package into inactive slot;
3. validate header and exact total length;
4. validate payload, layout and asset digests;
5. parse and validate schema/capability requirements;
6. verify the stored bytes can be read back;
7. atomically persist the active-slot selector in NVS;
8. report the active design ID and package digest;
9. retain the previous slot for rollback until a later confirmed boot policy permits cleanup.

Power loss before step 7 leaves the old slot active. Power loss after step 7 is recovered by verifying the selected slot at boot and falling back to the previous valid slot if needed.

The two-slot scheme cannot guarantee that two maximum-sized packages fit in the SPIFFS partition. Runtime capacity checks are mandatory.

## 7. Open implementation details

- Implement CRC32 and incremental SHA-256 using APIs available in the pinned ESP32 Arduino/ESP-IDF toolchain.
- Confirm exact SPIFFS usable capacity and write/rename behavior on the physical board.
- Decide package metadata limits after measuring worst-case manifest and layout parse heap.
- Add binary golden fixtures and corruption tests.
- Implement the Web exporter using IndexedDB asset resolution and streaming Blob assembly.
