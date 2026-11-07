# Watch Face Studio Communication Protocol — WFS BLE v1

**Status:** WFS v1 GATT, Web transport and package-transfer paths now exist in source. They are not yet verified by a current PlatformIO build or physical browser-to-device test. The frame payload-length offset defect was corrected in commit 679f610; golden host coverage was added in commit 51590e7.  
**Legacy compatibility:** the existing text service remains available during migration.

## 1. BLE services

### Legacy service (preserved)

- Device name: ESP32_Watch_C3 (the Web filter prefix remains ESP32_Watch).
- Service: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
- Characteristic: beb5483e-36e1-4688-b7f5-ea07361b26a8
- Properties: READ, WRITE, WRITE_NR, NOTIFY
- Descriptor: BLE2902

This characteristic remains for the existing text commands until regression tests confirm the new control service can replace each operation. Do not change its current meaning silently.

### New v1 service

| Characteristic | UUID | Properties | Direction |
|---|---|---|---|
| WFS Service | f2c6b100-2f3a-4c1b-9d70-8e4a5c6b7100 | Primary service | — |
| Control RX | f2c6b101-2f3a-4c1b-9d70-8e4a5c6b7100 | WRITE, WRITE_NR | Web → Device |
| Control TX | f2c6b102-2f3a-4c1b-9d70-8e4a5c6b7100 | READ, NOTIFY | Device → Web |
| Data RX | f2c6b103-2f3a-4c1b-9d70-8e4a5c6b7100 | WRITE, WRITE_NR | Web → Device |
| Data TX | f2c6b104-2f3a-4c1b-9d70-8e4a5c6b7100 | READ, NOTIFY | Device → Web |

The Web client must request both the legacy service UUID and the new WFS service UUID in optionalServices during migration.

## 2. Frame envelope

All multi-byte integers are little-endian. Header size is 16 bytes.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 2 | Magic | 0x57 0x46 (\"WF\") |
| 2 | 1 | Protocol major | 1 |
| 3 | 1 | Protocol minor | 0 for initial implementation |
| 4 | 1 | Message type | Enumerated below |
| 5 | 1 | Flags | Must be zero in v1 |
| 6 | 2 | Sequence | Per-direction monotonically increasing sequence |
| 8 | 4 | Session ID | Nonzero session identifier |
| 12 | 2 | Payload length | Exact payload length |
| 14 | 2 | Header CRC16 | CRC-16/CCITT-FALSE over bytes 0–13 |
| 16 | N | Payload | Message-specific bytes |

**Implementation invariant:** session ID occupies bytes 8–11; payload length occupies bytes 12–13. Do not use offset 10 for payload length. The host golden fixture uses a JavaScript-compatible frame with session ID 0x12345678 and payload length 4.

Reject invalid magic/version, unknown mandatory message types, nonzero reserved flags, oversized payloads, bad header CRC, truncated frames, wrong session IDs and invalid sequence transitions.

Control payloads are compact UTF-8 JSON with a negotiated maximum. Data payloads are binary. BLE callbacks only copy bounded bytes into queues; parsing, flash writes and command execution happen in the application loop.

## 3. Message types

| Value | Name | Direction | Purpose |
|---:|---|---|---|
| 0x01 | HELLO | Web → Device | Offer protocol range and client nonce |
| 0x02 | HELLO_ACK | Device → Web | Selected protocol, session and device identity |
| 0x03 | GET_CAPABILITIES | Web → Device | Request static and runtime capabilities |
| 0x04 | CAPABILITIES | Device → Web | Schema, elements, formats, memory/storage and transfer limits |
| 0x05 | GET_STATUS | Web → Device | Request current runtime status |
| 0x06 | STATUS | Device → Web | Wi-Fi, BLE, weather, screen, auto-sleep and heap snapshot |
| 0x10 | COMMAND | Web → Device | Typed device command with requestId |
| 0x11 | RESPONSE | Device → Web | Correlated command result |
| 0x12 | EVENT | Device → Web | State change or asynchronous result |
| 0x20 | TRANSFER_BEGIN | Web → Device | Package length, digest, design ID and transfer ID |
| 0x21 | TRANSFER_READY | Device → Web | Reservation result, accepted chunk size and transfer ID |
| 0x22 | DATA | Web → Device | Chunk index and binary bytes |
| 0x23 | DATA_ACK | Device → Web | Highest contiguous accepted chunk and byte count |
| 0x24 | TRANSFER_END | Web → Device | Declare all bytes sent |
| 0x25 | VERIFY_RESULT | Device → Web | Package integrity/schema/capability result |
| 0x26 | APPLY | Web → Device | Request activation of validated inactive slot |
| 0x27 | APPLY_RESULT | Device → Web | Activation result and active design identity |
| 0x28 | CANCEL | Web → Device | Cancel a cancellable operation |
| 0x29 | TRANSFER_PROGRESS | Device → Web | Stage and verified byte progress |
| 0x30 | PING | Either | Liveness request |
| 0x31 | PONG | Either | Liveness response |
| 0x7F | ERROR | Device → Web | Stable error code and safe diagnostic context |

Every COMMAND/RESPONSE pair includes a requestId. Events include eventId. Transfer messages include transferId and sequence/chunk indexes. A successful GATT write is not proof of command processing.

### DATA payload layout

The DATA frame payload begins with an 8-byte transfer prefix followed by raw package bytes:
- transferId: uint32 little-endian;
- chunkIndex: uint32 little-endian, starting at zero;
- chunkBytes: 1–216 bytes.

DATA_ACK payload is exactly 14 bytes:
- transferId: uint32 little-endian;
- highest contiguous accepted chunk index: uint32 little-endian;
- total bytes written: uint32 little-endian;
- status code: uint16 little-endian (zero means accepted).

TRANSFER_BEGIN JSON fields are transferId, packageLength, payloadSha256 (hex SHA-256 of the WFS1 payload), and designId. TRANSFER_READY returns transferId, chunkSize and nextChunk. TRANSFER_END and APPLY JSON payloads contain transferId. VERIFY_RESULT returns transferId, ok and error. APPLY_RESULT returns transferId, ok and designId.

## 4. Negotiated chunk size and reliability

- Request an ATT MTU suitable for bulk transfer where the stack/platform allows it.
- Current Web uploader uses stop-and-wait and waits for one ACK per chunk. Bounded retransmission of the same chunk after ACK timeout, idempotent duplicate handling, and reconnect recovery remain required work; they must not be considered implemented merely because the message IDs exist.
- Device capabilities report the maximum complete framed write accepted by the active connection.
- Maximum DATA payload = negotiated framed-write limit minus the 16-byte WFS frame header and the DATA message's fixed payload fields.
- The Web client must use the reported chunk size; it must not assume a 20-byte or 244-byte payload.
- If the browser/OS cannot use the reported size, it must reconnect or renegotiate a smaller size before starting the transfer.
- Start with one unacknowledged DATA chunk at a time. This bounded stop-and-wait behavior is slower but simple to verify on the first ESP32-C3 implementation.
- A chunk is accepted only after sequence, transfer ID, expected length and flash write have succeeded.
- Duplicate last-accepted chunks are acknowledged without writing them twice.
- Out-of-order chunks are rejected with the expected next index.
- Timeout, disconnect, flash error or digest mismatch invalidates the inactive staging session. Retry starts from byte zero with a new transfer ID.
- The active slot is never modified during reception.

## 5. Handshake and capability exchange

1. Web connects and subscribes to Control TX and Data TX notifications.
2. Web sends HELLO with client protocol range and nonce.
3. Device returns HELLO_ACK with selected protocol, device ID, firmware build ID and session ID.
4. Web requests GET_CAPABILITIES.
5. Device returns:
   - supported schema and package versions;
   - supported element types and rendering features;
   - display profile;
   - supported asset encodings;
   - maximum object count and JSON size;
   - current free heap and largest contiguous block;
   - SPIFFS total/used/free bytes;
   - maximum package size that can currently be staged while preserving the active package;
   - accepted framed-write and data chunk sizes;
   - legacy feature availability.
6. Web runs compatibility validation before enabling Upload.

The STATUS JSON payload contains wifiEnabled, wifiConnected, bleConnected, weatherValid, temperature, screen, autoSleepSeconds, powerState and heapFree. The current compact CAPABILITIES JSON payload uses these keys: sv (schema version), pv (package version), obj (maximum objects), json (maximum layout JSON bytes), frame (maximum framed write bytes), mtu (negotiated ATT MTU), chunk (maximum raw DATA chunk bytes after frame and transfer headers), el (comma-separated supported element types), img (1 if image assets are supported), upload (1 if package installation is enabled), free (SPIFFS free bytes), stage (maximum current inactive-slot staging capacity), and heap (free heap bytes).

Runtime heap/storage values are snapshots. TRANSFER_BEGIN must reserve storage again and can still reject if resources changed.

## 6. Transfer lifecycle

IDLE → PREPARING → NEGOTIATING → RECEIVING → VERIFYING → VALIDATING → READY_TO_APPLY → APPLYING → COMPLETE

Terminal states: FAILED, CANCELLED.

TRANSFER_BEGIN declares exact package length and expected package payload digest. TRANSFER_READY confirms that the inactive slot has been reserved and reports the accepted chunk size. DATA frames are written sequentially. TRANSFER_END triggers full validation and read-back verification. APPLY is a separate explicit action. APPLY_RESULT is the only success confirmation.

Progress must distinguish received bytes, written bytes, verified bytes and applied state. Do not show 100% success when only the final GATT write has completed.

## 7. Typed command mapping

| Command | New v1 command name | Legacy path during migration |
|---|---|---|
| Time sync | TIME_SET {unixEpoch, timezone/offset} | SYNC_TIME:<epoch> |
| Weather refresh | WEATHER_REFRESH | SYNC_WEATHER |
| Location | LOCATION_SET {latitude, longitude} | GPS:<lat>,<lon> |
| Wi-Fi scan | WIFI_SCAN | SCAN_WIFI |
| Wi-Fi credentials | WIFI_CONFIG {ssid, password} | WIFI:<ssid>,<pass> |
| Sleep request | POWER_SLEEP | SLEEP_ON |
| Wake acknowledgement/status | POWER_STATUS | SLEEP_OFF remains status-only |
| Auto-sleep read/write | AUTO_SLEEP_GET / AUTO_SLEEP_SET | GET_AUTO_SLEEP / AUTO_SLEEP:<seconds> |

Credentials must be JSON-escaped and must never appear in serial logs or error events. The firmware now configures BLE bonding with link encryption and marks both legacy command access and WFS control/data characteristic access as encrypted. The current pairing mode is BLE bonding with IO capability NONE (Just Works): it encrypts the link but does not provide MITM protection. A user-approved OS pairing/bond is required before accessing protected characteristics. Do not describe this as strong MITM authentication. Production hardening still requires a reviewed passkey/authorization UX, bond reset/revocation behavior, and physical-device pairing tests. Credentials are never logged and the typed WIFI_CONFIG payload is JSON-based rather than comma-delimited.

## 8. Stable error codes

Initial codes:
- 0x0001 PROTOCOL_VERSION_UNSUPPORTED
- 0x0002 FRAME_INVALID
- 0x0003 SESSION_INVALID
- 0x0004 SEQUENCE_INVALID
- 0x0010 COMMAND_INVALID
- 0x0020 TRANSFER_BUSY
- 0x0021 TRANSFER_ID_INVALID
- 0x0022 TRANSFER_SIZE_INVALID
- 0x0023 STORAGE_RESERVATION_FAILED
- 0x0024 CHUNK_SEQUENCE_INVALID
- 0x0025 CHUNK_WRITE_FAILED
- 0x0026 TRANSFER_DIGEST_MISMATCH
- 0x0030 PACKAGE_HEADER_INVALID
- 0x0031 PACKAGE_VERSION_UNSUPPORTED
- 0x0032 PACKAGE_LAYOUT_INVALID
- 0x0033 PACKAGE_ASSET_INVALID
- 0x0034 SCHEMA_INVALID
- 0x0035 CAPABILITY_UNSUPPORTED
- 0x0040 APPLY_FAILED
- 0x0041 ROLLBACK_ACTIVE
- 0x0050 DEVICE_BUSY
- 0x0051 MEMORY_RESERVATION_FAILED

Error context must be bounded and must not contain credentials or full package contents.

## 9. Legacy compatibility and migration exit

Keep the existing service/characteristic and command semantics while the new service is introduced. Migrate one operation at a time and run regression tests for each. Remove legacy support only in a separately reviewed change after the Web client no longer depends on it and physical hardware tests pass.
