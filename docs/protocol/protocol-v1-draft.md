# Watch Face Studio Communication Protocol — WFS BLE v1

**Status:** WFS v1 framing, GATT service, capability/status exchange, Web uploader, transfer verification and apply path are implemented. Host protocol/golden-frame and lifecycle tests pass, and the ESP32-C3 target compiles in CI. Physical browser-to-watch interoperability is not yet verified.  
**Compatibility:** the legacy text BLE service remains available.

This document describes the implemented v1 behavior. Message IDs reserved for future use are not evidence that their full semantics are implemented.

## 1. BLE services

### Legacy service

- Device name: `ESP32_Watch_C3` (Web filter prefix: `ESP32_Watch`)
- Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- Characteristic UUID: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- Properties: READ, WRITE, WRITE_NR, NOTIFY
- Descriptor: BLE2902

Existing text commands remain supported during migration.

### WFS v1 service

| Characteristic | UUID | Properties | Direction |
|---|---|---|---|
| WFS Service | `f2c6b100-2f3a-4c1b-9d70-8e4a5c6b7100` | Primary service | — |
| Control RX | `f2c6b101-2f3a-4c1b-9d70-8e4a5c6b7100` | WRITE, WRITE_NR | Web → Device |
| Control TX | `f2c6b102-2f3a-4c1b-9d70-8e4a5c6b7100` | READ, NOTIFY | Device → Web |
| Data RX | `f2c6b103-2f3a-4c1b-9d70-8e4a5c6b7100` | WRITE, WRITE_NR | Web → Device |
| Data TX | `f2c6b104-2f3a-4c1b-9d70-8e4a5c6b7100` | READ, NOTIFY | Device → Web |

The Web Bluetooth request must include both the legacy service UUID and WFS service UUID in `optionalServices`.

Both legacy and WFS protected characteristics require encrypted access. BLE bonding is configured with IO capability NONE (Just Works). This enables link encryption but does **not** provide MITM protection. Do not treat this as strong authentication.

## 2. Frame envelope

All multi-byte fields are little-endian. Header size is 16 bytes.

| Offset | Size | Field | Rule |
|---:|---:|---|---|
| 0 | 2 | Magic | `0x57 0x46` (ASCII `WF`) |
| 2 | 1 | Major | 1 |
| 3 | 1 | Minor | 0 |
| 4 | 1 | Message type | See table below |
| 5 | 1 | Flags | Must be zero |
| 6 | 2 | Sequence | uint16 |
| 8 | 4 | Session ID | uint32 |
| 12 | 2 | Payload length | Exact payload bytes |
| 14 | 2 | Header CRC | CRC-16/CCITT-FALSE over bytes 0–13 |
| 16 | N | Payload | Message-specific |

The session ID occupies bytes 8–11. Payload length occupies bytes 12–13. The C++ and JavaScript codecs share a byte-exact golden frame test.

Limits:
- Control payload: 220 bytes (maximum complete frame: 236 bytes).
- Data payload: 224 bytes (maximum complete frame: 240 bytes).
- DATA's 8-byte transfer prefix leaves 216 package bytes per chunk.

The firmware rejects malformed magic/version/type/flags/length/CRC/session frames and applies channel-specific payload limits.

### Sequence semantics

There is one incoming sequence space shared across Control RX and Data RX, and one outgoing sequence space shared across Control TX and Data TX. Sequence values wrap as uint16.

- After HELLO, the next client control/data frame must use the next sequence value.
- DATA retransmission may reuse the current last accepted sequence only when it is a byte-identical retry of the last accepted DATA chunk.
- A skipped, stale or modified duplicate sequence is rejected.
- The Web uploader uses stop-and-wait: one DATA chunk is outstanding at a time.

## 3. Message types

| Value | Name | Direction | Implemented behavior |
|---:|---|---|---|
| 0x01 | HELLO | Web → Device | Negotiate major version; must use session ID zero |
| 0x02 | HELLO_ACK | Device → Web | Selected version, device ID and new session ID |
| 0x03 | GET_CAPABILITIES | Web → Device | Request capability snapshot |
| 0x04 | CAPABILITIES | Device → Web | Device profile, element and transfer limits |
| 0x05 | GET_STATUS | Web → Device | Request runtime status |
| 0x06 | STATUS | Device → Web | Wi-Fi/BLE/weather/screen/power/heap snapshot |
| 0x07 | GET_FACE_STATUS | Web → Device | Request active-face and storage status |
| 0x08 | FACE_STATUS | Device → Web | Active face, transfer and SPIFFS status |
| 0x10 | COMMAND | Web → Device | Structured command dispatch |
| 0x11 | RESPONSE | Device → Web | Structured command response |
| 0x12 | EVENT | Device → Web | Event envelope; not all draft event types are emitted |
| 0x20 | TRANSFER_BEGIN | Web → Device | Request inactive-slot reservation |
| 0x21 | TRANSFER_READY | Device → Web | Accepted transfer ID and chunk size |
| 0x22 | DATA | Web → Device | Transfer ID, chunk index and bytes |
| 0x23 | DATA_ACK | Device → Web | Accepted chunk index, bytes written and status |
| 0x24 | TRANSFER_END | Web → Device | Finish receive and verify package |
| 0x25 | VERIFY_RESULT | Device → Web | Package verification result |
| 0x26 | APPLY | Web → Device | Activate the verified package |
| 0x27 | APPLY_RESULT | Device → Web | Activation result and design ID |
| 0x28 | CANCEL | Web → Device | Cancel a cancellable transfer |
| 0x29 | TRANSFER_PROGRESS | Device → Web | Reserved; not currently emitted |
| 0x30 | PING | Web → Device | Liveness request |
| 0x31 | PONG | Device → Web | Liveness response |
| 0x7F | ERROR | Device → Web | Bounded JSON error |

A successful GATT write only confirms that the BLE stack accepted the write. The Web app waits for the protocol response/ACK before advancing.

## 4. Handshake and session lifecycle

1. Web connects to the peripheral and subscribes to Control TX and Data TX.
2. Web sends HELLO with JSON fields `minMajor`, `maxMajor`, and `client`. HELLO's frame session ID must be zero.
3. Device validates the supported major version, aborts any cancellable transfer from a previous session, creates a nonzero random session ID and sends HELLO_ACK.
4. Web stores the session ID and requests CAPABILITIES and STATUS.
5. Web checks the design against the negotiated profile before enabling upload.

HELLO_ACK JSON:
- `major`: selected protocol major
- `minor`: selected protocol minor
- `device`: `ESP32_Watch_C3`
- `session`: nonzero session ID

A repeated HELLO with session ID zero explicitly replaces the prior session. Any cancellable transfer from that session is aborted before the new session is published.

Connect/disconnect callbacks advance an RX generation counter. The application loop applies session reset outside BLE callbacks and discards queued frames stamped with an old generation. Disconnect does not resume a transfer on reconnect; a new upload starts from byte zero.

## 5. Capability and status payloads

### CAPABILITIES

Compact JSON keys:
- `sv`, `pv`: schema and package versions
- `pid`: device profile ID
- `cw`, `ch`, `cs`: canvas width, height and shape
- `bw`, `bh`: maximum bitmap width and height
- `obj`, `assets`: object and asset limits
- `json`: maximum layout JSON bytes
- `img`: image support flag
- `el`: comma-separated supported element codes: `t,dc,ac,d,w,s,i`
- `wc`: weather-condition text support flag
- `upload`: package upload availability
- `chunk`: maximum package bytes per DATA chunk
- `stage`: current inactive-slot staging capacity

The active profile is `esp32-c3-gc9a01-240`, 240×240 circle. Older v1 firmware may omit some newer fields; Web preflight has conservative defaults for those older builds.

The firmware sets local BLE MTU to 247. The current chunk calculation is `min(MTU - 27, 216)`, where 27 bytes account for ATT overhead, the 16-byte WFS header and the 8-byte DATA prefix. The reported `chunk` value is the maximum raw package bytes in one DATA frame. TransferBegin rechecks and reserves storage; the CAPABILITIES `stage` value is only a snapshot.

### STATUS

JSON fields:
- `wifiEnabled`, `wifiConnected`, `bleConnected`
- `weatherValid`, `temperature`
- `screen`, `autoSleepSeconds`, `powerState`
- `heapFree`

### FACE_STATUS

JSON fields:
- `faceId`, `faceName`
- `transfer`, `rx`, `total`
- `fsUsed`, `fsFree`

## 6. Transfer payloads and reliability

### TRANSFER_BEGIN

Control JSON fields:
- `transferId`: nonzero uint32
- `packageLength`: uint32
- `payloadSha256`: exactly 64 hexadecimal characters; SHA-256 of WFS1 payload bytes
- `designId`: non-empty design identifier

Fields are type-checked before conversion. The digest parser accepts only ASCII hexadecimal characters; malformed input is rejected before storage reservation.

### TRANSFER_READY

Returns:
- `transferId`
- `chunkSize`
- `nextChunk` (initially zero)

### DATA

Payload layout:
- offset 0, 4 bytes: `transferId` uint32
- offset 4, 4 bytes: `chunkIndex` uint32, starting at zero
- offset 8 onward: 1–216 package bytes

The receiver accepts only the next chunk index. After a successful flash write and flush, it commits the sequence state. An exact retransmission of the last accepted chunk is ACKed without writing it twice. Same index with different bytes, skipped indexes and invalid lengths are rejected.

### DATA_ACK

Exactly 14 bytes:
- offset 0: transfer ID, uint32
- offset 4: last accepted chunk index, uint32
- offset 8: total bytes written, uint32
- offset 12: status, uint8 (zero means success)
- offset 13: reserved, zero

The Web client validates transfer ID, chunk index, status and cumulative byte count before moving to the next chunk. It retries the same encoded frame up to three attempts when an ACK times out.

### TRANSFER_END, VERIFY_RESULT, APPLY

- TRANSFER_END JSON contains `transferId`. The device flushes/closes the writer, reopens the staging package and validates length, header, hashes, manifest, layout, assets and device capabilities.
- VERIFY_RESULT JSON contains `transferId`, `ok` and `error`.
- APPLY JSON contains `transferId`. Apply is accepted only from Verified state.
- APPLY_RESULT JSON contains `transferId`, `ok` and `designId`.

### CANCEL and disconnect

CANCEL JSON contains `transferId`. It is accepted only in cancellable states. A failed cancel returns an error; the firmware does not emit a false cancellation success. Disconnect cleanup is deferred to the application loop. If staging cleanup fails, the concrete storage error is retained and a critical diagnostic is logged.

Firmware transfer states are:
`Idle`, `Receiving`, `Verifying`, `Verified`, `Applying`, `Complete`, `Failed`, `Cancelled`.

No resume-from-offset protocol is implemented. On reconnect, the client must start a fresh transfer.

## 7. Error behavior

ERROR payload is bounded JSON with `code` and `message`. Current errors distinguish:
- malformed frame/version/session/sequence;
- oversized control/data payload;
- malformed HELLO/control JSON or transfer fields;
- unsupported protocol version;
- resource busy or MTU too small;
- invalid digest or transfer reservation failure;
- transfer/chunk state, sequence or write failures;
- non-cancellable transfer.

The numeric codes are an implementation detail of this initial version and are not yet a separately frozen public error registry. Use the returned message and transfer error name; do not assume all codes from older planning drafts are emitted.

## 8. Structured commands and legacy migration

The firmware has a CommandHandler dispatch path for structured WFS commands, but the current Web editor still uses the legacy characteristic for several existing controls. The legacy command set includes time sync, weather refresh, GPS/location, Wi-Fi scan/configuration, sleep and Auto Sleep get/set.

Do not infer that all planned command names (for example, every typed Wi-Fi/power event) are implemented just because COMMAND/RESPONSE/EVENT message IDs exist. Keep legacy UUIDs and semantics until each operation has an implemented Web path and physical regression coverage.

## 9. Security and hardware verification

BLE bonding uses IO capability NONE (Just Works). The protected characteristics require encrypted access, but there is no MITM protection. A secure product would need a reviewed passkey/authorization design, bond reset/revocation behavior and physical-device tests.

Physical tests remain required for:
- pairing and encrypted characteristic access on Android/browser;
- HELLO/capability/status exchange and repeated reconnect;
- small and image-bearing uploads;
- lost ACK/retry, cancel, disconnect and malformed package;
- reboot after apply and interrupted transfer;
- Wi-Fi/weather arbitration and Light Sleep interaction.

Host tests and PlatformIO compilation are necessary, but are not substitutes for the hardware checklist.
