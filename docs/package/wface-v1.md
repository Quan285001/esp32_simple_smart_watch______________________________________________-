# Watch Face Package Format — WFS1 / `.wface`

**Status:** Web package builder, firmware package validator, RGB565 image pipeline and transactional BLE transfer/apply path are implemented. Host package/parser/image tests and ESP32-C3 PlatformIO build pass in CI. Physical browser-to-watch transfer and image rendering remain hardware-pending.  
**Target:** ESP32-C3, GC9A01 240×240, SPIFFS inactive-slot staging.

A WFS1 package is a declarative design container. It is not executable code and contains no C++/bytecode.

## 1. Representations

Do not confuse these formats:

1. Watch Face JSON — editor design document.
2. `.wfsproject.json` — portable editor project containing the design and original source images.
3. `.wface` — WFS1 device package containing a manifest, device-compatible layout and RGB565 image assets.

Only the third format is accepted by the firmware package validator.

## 2. Container layout

All integers are little-endian. The fixed header is exactly 64 bytes.

| Offset | Size | Field | Rule |
|---:|---:|---|---|
| 0 | 4 | Magic | ASCII `WFS1` |
| 4 | 2 | Package version | 1 |
| 6 | 2 | Header size | 64 |
| 8 | 4 | Manifest length | UTF-8 JSON bytes |
| 12 | 4 | Layout length | UTF-8 JSON bytes |
| 16 | 2 | Asset count | Must equal manifest asset count |
| 18 | 2 | Flags | Must be zero in v1 |
| 20 | 4 | Payload length | Manifest + layout + asset bytes |
| 24 | 32 | Payload SHA-256 | Digest of all payload bytes |
| 56 | 4 | Header CRC32 | CRC-32/ISO-HDLC over bytes 0–55 |
| 60 | 4 | Reserved | Must be zero |

Payload order:
1. `manifest.json` UTF-8 bytes (no BOM);
2. `layout.json` UTF-8 bytes (no BOM);
3. raw assets in manifest order.

The exact package length is `64 + payloadLength`. Trailing bytes are rejected.

## 3. Manifest and layout

Example manifest:

```json
{
  "packageFormatVersion": 1,
  "designId": "design_abc123",
  "name": "Minimal Digital",
  "schemaVersion": 1,
  "deviceProfileId": "esp32-c3-gc9a01-240",
  "requiredCapabilities": ["clock.digital", "text.basic"],
  "layoutSha256": "64-hex-character-sha256",
  "assets": [
    {
      "assetId": "asset_background",
      "mediaType": "image/rgb565",
      "encoding": "rgb565le",
      "width": 240,
      "height": 240,
      "byteLength": 115200,
      "offset": 0,
      "sha256": "64-hex-character-sha256"
    }
  ]
}
```

Asset `offset` is relative to the first byte after manifest and layout. Absolute package offset:

```
64 + manifestLength + layoutLength + asset.offset
```

Validation requires:
- unique asset IDs;
- integer JSON values for width, height, byteLength and offset;
- width/height within the active device bitmap limits;
- `byteLength == width × height × 2`;
- ascending, contiguous, non-overlapping asset ranges fully inside payload;
- every layout asset reference resolves to exactly one manifest asset;
- manifest asset count matches header asset count;
- manifest `layoutSha256` matches exact layout bytes;
- each asset digest and complete payload digest match;
- each parsed element's required capability is declared and supported by the active device.

Manifest and layout are parsed with bounded sizes and nesting limits. The current target limits are 8 KiB manifest and 16 KiB layout JSON. The shared schema/profile and live CAPABILITIES response remain authoritative for object/asset/bitmap limits.

## 4. Asset encoding

The implemented device asset type is:
- `mediaType: image/rgb565`
- `encoding: rgb565le`
- two bytes per pixel, RGB565 little-endian
- no compression in WFS1
- exact dimensions and byte length required

The Web editor imports browser-supported source images, applies target sizing/cropping and converts them to RGB565 before packaging. The firmware does not decode PNG/JPEG. Image rows are streamed from package storage to the canvas; no full-screen decompressed bitmap buffer is required.

Transparent source images that would require alpha compositing over visible lower layers are rejected during preflight because firmware alpha blending is not implemented. Fully opaque images and images without visible lower-layer compositing are eligible.

Glyph atlases and compressed asset formats require a separately versioned encoding contract; do not overload the RGB565 asset type.

## 5. Integrity and validation

- Header CRC32 detects malformed header fields.
- Payload SHA-256 covers manifest, layout and all asset bytes in exact payload order.
- `layoutSha256` covers exact layout JSON UTF-8 bytes.
- Each asset SHA-256 covers exact raw asset bytes.
- The BLE TRANSFER_BEGIN field `payloadSha256` is exactly 64 hexadecimal characters and must match the payload digest.
- Firmware rejects invalid lengths, unsupported major package versions, nonzero v1 flags/reserved values, malformed JSON, invalid offsets, duplicate/missing asset references, unsupported capabilities and checksum mismatches.

The Web app performs schema/device compatibility preflight before upload. Firmware independently validates the received package; Web preflight is not a security boundary.

## 6. Storage and atomic installation

The current `huge_app.csv` partition provides a nominal 0xE0000-byte SPIFFS partition. Usable space is lower due to filesystem metadata and must be queried at runtime.

Two package slots are used:
- active slot: selected by the NVS active-slot commit pointer;
- inactive slot: staging target for the next package.

Transfer setup records the exact active and inactive slot reservation, calculates capacity using current free space plus reclaimable inactive-slot bytes, removes only the inactive slot and opens it for writing. The active slot is protected throughout reception.

Installation sequence:
1. reserve the inactive slot;
2. receive sequential chunks and flush writes;
3. flush/close the staging writer;
4. reopen the package and verify exact length, header, payload, manifest, layout, assets and capabilities;
5. transition to Verified;
6. on explicit APPLY, persist the new active-slot pointer and read it back;
7. install the already validated in-memory FaceModel;
8. if model installation fails, restore the previous active pointer; on first install, remove the pointer;
9. if rollback cannot be verified, disable FaceStorage until reinitialization and emit a critical diagnostic.

Power loss before the pointer commit leaves the old slot selected. Boot trusts only the slot named by the NVS pointer. If the pointer is absent/invalid or the selected package is missing/invalid, the custom face is rejected and the built-in face is used. The other slot is never automatically promoted because it may contain an incomplete staging package. Automatic fallback to the previous slot is not implemented.

Two maximum-sized packages are not guaranteed to fit simultaneously. The transfer must be rejected before reception if the inactive slot cannot hold the new package while preserving the active slot.

## 7. Transfer and diagnostics

WFS transfer is stop-and-wait:
- TRANSFER_BEGIN reserves storage and returns the negotiated chunk size.
- DATA carries an 8-byte transfer ID/chunk-index prefix and 1–216 package bytes.
- DATA_ACK confirms the last accepted chunk and cumulative byte count.
- An exact retry of the last accepted chunk is acknowledged without writing twice.
- TRANSFER_END triggers package verification.
- APPLY is separate and required to activate the face.
- CANCEL/disconnect cleanup removes the inactive staging file when possible.

Serial diagnostics include receive/verify/apply durations, package byte count and heap snapshots. These measurements need to be collected on the physical board; CI does not provide runtime heap evidence.

## 8. Implementation and hardware verification

Host tests cover:
- Web WFS1 header/payload/digest generation;
- little-endian RGB565 conversion and image dimensions;
- duplicate asset IDs and transparent overlay preflight;
- firmware RGB565 decode and image element storage-to-canvas behavior;
- parser/package constraints and transfer sequencing.

Still hardware-pending:
- actual SPIFFS usable capacity and fragmentation behavior;
- interrupted writes and power loss during transfer/apply;
- repeated real BLE upload/cancel/disconnect/reconnect;
- visual image alignment and rendering on the GC9A01;
- runtime heap margin for large image-bearing packages.

See `docs/architecture/hardware-regression-checklist.md` before declaring the package path physically validated.
