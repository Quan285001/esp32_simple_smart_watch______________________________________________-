# Face Studio static photo background v1

Status: first bounded firmware implementation on `feature/face-image-background`. This is a static background asset, not the WFace bytecode VM.

## Supported scope

- One active 160 × 160 photo background for the native 160 × 160 clock-face renderer.
- Browser converts the selected/cropped photo to RGB565 before transfer.
- RGB565 payload size: 51,200 bytes.
- The current native clock renderer draws its clock text and hands over the photo. Browser-only text layers, custom fonts, and alternate face templates are not sent to the watch.
- Removing the photo in Face Studio and applying the design clears the active photo on the watch.

## BLE framing

The ESP32 requests an ATT MTU of 185 bytes. The browser first attempts 176-byte image payloads. If the first large write is rejected by the negotiated MTU, it retries with 12-byte payloads compatible with the default 20-byte ATT write payload.

All multi-byte integers are little-endian.

| Frame | Layout |
|---|---|
| BEGIN (14 bytes) | `F0, version, width:u16, height:u16, dataLength:u32, crc32:u32` |
| DATA | `F1, offset:u32, RGB565 bytes...` |
| END (1 byte) | `F2` |
| CLEAR (1 byte) | `F3` |

The firmware accepts only 160 × 160 RGB565 assets with exactly 51,200 bytes. Data offsets must be sequential, four-byte aligned, and in range. The final CRC32 is verified by reading the staging image back from Flash.

Notifications:
- `IMG_READY`: staging slot erased and ready.
- `IMG_OK`: CRC passed and the new slot was activated.
- `IMG_CLEARED`: active photo cleared.
- `IMG_ERR:<reason>`: rejected or failed operation.

## Flash storage and activation

The partition table reserves:
- `face_a`: 64 KiB
- `face_b`: 64 KiB

The active slot is recorded in NVS. A transfer erases and writes only the inactive slot. The old active image remains selected until the new image is fully received, CRC-verified, and the NVS slot selector is written. A failed transfer therefore does not replace the last known-good image.

The renderer reads four scanlines at a time into a 1.25 KiB tile buffer. It does not allocate a second full-screen image buffer.

## Partition table implications

`partitions_face.csv` uses a 0x2F0000-byte factory application partition plus the two image slots. This accommodates the current firmware size while reserving persistent photo storage. This layout does not include OTA application slots; firmware updates are performed over USB with PlatformIO.

Changing from the previous OTA partition table requires flashing the bootloader, partition table, and application from the same build. Do not flash only `firmware.bin` at 0x10000 when changing partition layouts.

## Validation still required

- PlatformIO CI build.
- Browser JavaScript syntax validation.
- Physical ESP32-C3 tests for BLE MTU negotiation, transfer throughput, CRC rejection, power loss during staging, photo rendering, clear-photo behavior, and reboot persistence.
