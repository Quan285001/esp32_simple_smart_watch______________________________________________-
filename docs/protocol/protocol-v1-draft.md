# Watch Face Studio Communication Protocol — v1 Draft

**Status:** design draft; not implemented  
**Primary transport:** BLE  
**Bulk transport:** Wi-Fi only after explicit reachability and authentication checks  
**Legacy protocol:** current single-characteristic text commands remain a temporary compatibility path until the new implementation passes regression tests.

## 1. Requirements

The protocol must provide:
- version negotiation and device identity;
- capability and runtime-resource reporting;
- typed commands, responses and asynchronous events;
- bounded payloads and explicit packet limits;
- request correlation and sequence checking;
- transfer negotiation, progress, cancellation and final acknowledgement;
- integrity validation before installation;
- retry from the beginning after a failed transfer;
- atomic activation while preserving the previously active face.

A successful GATT write is not proof that the command was processed. The device must return a correlated response.

## 2. BLE service model

The first implementation should use one control service with separate characteristics:
- **Control RX:** Web client writes framed commands.
- **Control TX:** device notifications for responses and events.
- **Data RX:** optional bulk data characteristic for BLE package transfer.
- **Data TX:** optional data acknowledgements / transfer status.

Exact UUIDs, properties and negotiated ATT payload sizes are implementation constants to be added after the protocol implementation is selected. The existing service/characteristic UUID pair is reserved for the legacy service during migration; do not silently change its meaning.

BLE callbacks must only validate the outer length and enqueue bounded bytes. Parsing, storage writes, Wi-Fi operations and rendering must happen outside BLE host callbacks.

## 3. Frame envelope

All multi-byte integers are little-endian. A frame is:

| Field | Bytes | Meaning |
|---|---:|---|
| Magic | 2 | Fixed bytes `0x57 0x46` ("WF") |
| Protocol major | 1 | Major version |
| Protocol minor | 1 | Minor version |
| Message type | 1 | Enum below |
| Flags | 1 | Reserved bits must be zero in v1 |
| Sequence | 2 | Per-direction sequence number |
| Session ID | 4 | Random/nonzero session identifier |
| Payload length | 2 | Exact payload byte count |
| Header CRC16 | 2 | CRC-16/CCITT-FALSE over preceding header bytes |
| Payload | N | Message-specific payload |

The receiver must reject invalid magic, unsupported major version, unknown mandatory message types, nonzero reserved flags, oversized payloads, bad header CRC and truncated frames. A negotiated maximum payload is reported in the handshake. The implementation must account for ATT MTU and fragmentation rather than assuming a fixed 20-byte packet.

Control payloads may use compact JSON during early development; binary assets must use raw bytes under the same envelope. Define a strict maximum for control JSON and reject excess input before allocating.

## 4. Message types

| Type | Direction | Purpose |
|---|---|---|
| HELLO | Web → Device | Begin session and offer supported protocol range |
| HELLO_ACK | Device → Web | Selected version, device ID, session ID and basic status |
| GET_CAPABILITIES | Web → Device | Request renderer and runtime capabilities |
| CAPABILITIES | Device → Web | Supported object types, formats and resource limits |
| COMMAND | Web → Device | Time/weather/settings/device operation |
| RESPONSE | Device → Web | Correlated command result |
| EVENT | Device → Web | Unsolicited connection, weather, sleep or transfer state |
| TRANSFER_BEGIN | Web → Device | Declare package ID, total bytes and expected digest |
| TRANSFER_READY | Device → Web | Staging allocation result and negotiated chunk size |
| DATA | Web → Device | Numbered payload chunk |
| DATA_ACK | Device → Web | Highest accepted sequence / chunk result |
| TRANSFER_END | Web → Device | Declare that all bytes have been sent |
| VERIFY_RESULT | Device → Web | Device-side integrity and package validation result |
| APPLY | Web → Device | Request atomic activation of validated staging package |
| APPLY_RESULT | Device → Web | Explicit activation result and active design ID |
| CANCEL | Web → Device | Cancel a cancellable operation |
| ERROR | Device → Web | Structured error code and safe diagnostic context |
| PING / PONG | Both | Liveness check |

Each COMMAND includes a request ID. Each RESPONSE echoes it and contains a stable result code. Events include a monotonic event ID so the Web client can ignore duplicates.

## 5. Handshake

1. Web opens GATT and subscribes to Control TX.
2. Web sends HELLO with supported major/minor range and a client nonce.
3. Device validates the request and returns HELLO_ACK with selected protocol, device identifier, session ID, firmware build identifier, and required security mode.
4. Web requests CAPABILITIES.
5. Device returns supported schema versions, object types, color formats, bitmap dimensions, package limits, maximum BLE chunk size, available storage, free heap and largest contiguous heap block.
6. Web compares the active design against those capabilities before enabling Apply.

Runtime heap/storage values are snapshots, not guarantees. Reserve resources before accepting a transfer and return a clear error if reservation fails.

## 6. Transfer state machine

`IDLE → PREPARING → NEGOTIATING → CONNECTING_TRANSPORT → TRANSFERRING → VERIFYING → VALIDATING → APPLYING → COMPLETED`

Terminal failure states: `FAILED`, `CANCELLED`.

The Web and device each maintain an explicit state machine. Invalid transitions are rejected. Progress events identify stage, transport, bytes sent, bytes received, bytes validated and bytes applied. These values must not be conflated.

### Retry semantics

The user selected restart-from-beginning semantics:
- A failed transfer invalidates its staging session.
- The next attempt uses a new transfer ID/session and begins at byte zero.
- Old staging bytes must not be reused.
- The active package remains untouched.
- The device acknowledges the final package digest before Apply is enabled.

### Atomic activation

The device must:
1. receive into staging storage;
2. validate declared and actual lengths;
3. verify the package digest and every asset digest;
4. validate schema and renderer compatibility;
5. persist the staging metadata;
6. atomically switch the active-design pointer;
7. retain a recoverable previous pointer/package.

If power fails before the pointer switch, the old design remains active. If power fails during/after the switch, boot recovery chooses the last fully validated active record.

## 7. Wi-Fi bulk transport

Wi-Fi is not automatically reachable just because BLE is connected. The protocol must negotiate a supported method and the UI must confirm actual reachability.

Candidate methods:
- device SoftAP + local endpoint;
- shared LAN / station mode + local endpoint;
- BLE-only fallback.

Before bulk transfer:
- confirm the selected network method and IP/endpoint;
- authenticate the transfer session with a short-lived token negotiated over BLE;
- verify a challenge/response and endpoint reachability;
- enforce CORS/origin policy and request-size limits;
- reject unauthenticated or replayed transfer IDs;
- never log Wi-Fi passwords or session tokens.

Browser HTTPS-to-local-HTTP mixed-content and private-network access policies are a major compatibility risk. A device-hosted HTTPS server with a self-signed certificate is not automatically a solution because browsers may reject the certificate. Do not implement Wi-Fi bulk transfer until target Android/browser combinations are tested. If it is unavailable, explain why and offer BLE only when the negotiated package size is within the BLE limit.

## 8. Error model

Use stable machine-readable codes, including:
- `ERR_PROTOCOL_VERSION`
- `ERR_BAD_FRAME`
- `ERR_BAD_SEQUENCE`
- `ERR_SESSION_EXPIRED`
- `ERR_UNAUTHORIZED`
- `ERR_UNSUPPORTED_FEATURE`
- `ERR_PACKAGE_TOO_LARGE`
- `ERR_STORAGE_FULL`
- `ERR_LOW_MEMORY`
- `ERR_TRANSFER_BUSY`
- `ERR_TRANSFER_TIMEOUT`
- `ERR_CHECKSUM_MISMATCH`
- `ERR_SCHEMA_INVALID`
- `ERR_ASSET_MISSING`
- `ERR_RENDERER_INCOMPATIBLE`
- `ERR_APPLY_FAILED`
- `ERR_CANCELLED`

Error responses must include a safe human-readable message and retryability flag. Do not expose credentials, raw memory contents or secrets.

## 9. Implementation gates

Before protocol code is considered complete:
- write frame parser/serializer unit tests;
- fuzz invalid lengths, sequences, flags and malformed control payloads;
- test queue saturation and disconnect during transfer;
- test duplicate/out-of-order chunks;
- test full restart-from-zero retry;
- test checksum failure and corrupted package;
- test power loss before and after atomic pointer switch;
- measure BLE throughput, heap and stack on the real ESP32-C3;
- verify no display-loop starvation and no regression to clock/weather/sleep.

This document is a protocol proposal only. No protocol behavior is claimed as implemented.
