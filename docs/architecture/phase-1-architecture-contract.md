# Phase 1 — Firmware Architecture and Web Integration Contract

**Target branch:** `rebuild/watch-face-studio-v2`  
**Baseline:** Phase 0 audit at `6afd283b14e1be2bc38da6d8a6c2cd76fbdf9e2d`  
**Status:** Architecture decision record. Implementation changes must follow the phases below.  
**Hardware:** ESP32-C3 DevKitM-1 + GC9A01 240×240, Arduino / PlatformIO.

## 1. Architecture decisions

### 1.1 Preserve the working platform

Keep the current `c3_clock` environment, espressif32@6.9.0, Arduino framework, `huge_app.csv`, TFT_eSPI, Adafruit GFX, ArduinoJson and existing pin mapping. Keep one TFT_eSPI instance owned by DisplayManager. Do not change partition allocation or SPI frequency without measurements and a separate decision.

### 1.2 Module ownership

Use the existing source layout rather than creating parallel abstractions.

| Module | Owns | Must not own |
|---|---|---|
| `app/main.cpp` + AppController | Lifecycle orchestration, screen transitions, service scheduling, sleep policy | BLE packet parsing, JSON schema parsing, drawing primitives |
| DisplayManager | One TFT instance, init/rotation/power/blanking, display serialization boundary | Face data or BLE |
| ButtonManager | Debounce and short/double/long events | Screen drawing or service calls |
| FaceModel/Parser | Bounded in-memory model, JSON decode and field validation | Flash, BLE and drawing |
| FaceManager | Active model, candidate validation, fallback/activation coordination | BLE transport |
| FaceRenderer | Deterministic drawing of a validated model | JSON parsing, storage and network |
| BLEService | Legacy GATT identity and lifecycle | Long-running command execution |
| BLEManager/Protocol | WFS GATT, framing, session, queueing, capability/status messages | Rendering and network operations |
| TransferManager | Transfer state, chunk ordering, staging writes, package verification | GATT callbacks and UI |
| PackageValidator | WFS1 header/manifest/layout/asset integrity and semantic checks | BLE and active-slot pointer changes |
| StorageManager/FaceStorage | SPIFFS mount, file access, two-slot active pointer | JSON interpretation |
| Time/Weather/WiFi services | Time and network state, cached snapshots | Display drawing |
| SystemManager/Diagnostics | Power-state transitions and bounded telemetry | Feature-specific business logic |

Transitional direct use of the shared `tft` symbol may remain until each legacy face is migrated. No second display object may be created.

### 1.3 Runtime ownership and loop model

Keep a single application-loop coordinator. BLE callbacks copy bounded packets into queues only. The loop drains a bounded number of packets per iteration, updates state machines, services background completion flags, handles button events, and renders only when invalidated or due.

No HTTP/TLS, filesystem package validation, large JSON parse, or blocking waits may execute inside BLE callbacks or the clock rendering path. Background weather/network work may use a dedicated task only where already required; do not add tasks for every module.

## 2. Application lifecycle

### 2.1 Boot

1. Initialize serial, diagnostics, button and display.
2. Mount SPIFFS without auto-formatting.
3. Load the committed face slot and validate it; fall back to the previous valid slot or built-in face.
4. Restore time and weather cache from NVS.
5. Start network initialization as bounded background work.
6. Initialize BLE after boot network work has reached a terminal state, following current allocation ordering.
7. Enter ACTIVE and render the selected screen.

A missing/corrupt custom face must never prevent the built-in clock from booting.

### 2.2 Sleep/wake

States: `ACTIVE → PREPARING_SLEEP → SLEEPING → WAKING → RESTORING_SERVICES → ACTIVE`.

- Do not enter sleep while a BLE session, transfer, weather fetch, Wi-Fi scan or boot network task is active unless the user explicitly cancels that operation.
- Before sleep: stop accepting new work, finish/cancel bounded operations, turn Wi-Fi off, stop/deinitialize BLE using the supported Arduino-ESP32 lifecycle, blank display, configure GPIO9 wake.
- During Light Sleep, BLE is unavailable and the Web must treat the device as disconnected.
- After wake: restore button/display state, redraw from retained model/cache, then make radios available only according to the chosen power policy.
- Never tell the Web that a BLE session survived sleep.

Physical tests are required before changing the current lifecycle.

## 3. Canonical design document

The canonical editable document is `shared/schemas/watch-face.schema.json` schema v1. The Web owns the editable source design and original browser image assets. Firmware owns the active runtime model, time/weather snapshots, physical drawing, validation and installed package.

### 3.1 Coordinate contract

- Device canvas: exactly 240×240 logical pixels for profile `esp32-c3-gc9a01-240`.
- `transform.x/y/width/height` are logical canvas pixels, not CSS pixels or percentages.
- Browser preview converts logical pixels to CSS percentages; firmware uses logical pixels directly.
- `anchor` is an editor/layout semantic and must be explicitly mapped. v1 device implementation accepts top-left only unless a golden fixture proves another anchor mapping.
- zIndex ordering is ascending; ties preserve original object order.
- Values are range-checked before narrowing to integer display coordinates.

### 3.2 Runtime capability tiers

The schema describes what a design document may express; it does not imply every device can render every field.

Initial capability set may advertise only tested support:
- `text.basic`
- `clock.digital`
- `clock.analog`
- `date.basic`
- `weather.temperature`
- `shape.basic`
- `image.rgb565`

Do not advertise moon, sunrise/sunset, gauges, progress rings, groups, glyph overrides, arbitrary font families, rotation, opacity or advanced analog hand styles until implemented and tested.

### 3.3 Typography contract

Keep three independent concepts:
1. Object transform bounds: placement and clipping/layout region.
2. Nominal font metrics: font family/id, font size, weight, alignment and advance widths.
3. Glyph transform: optional scaleX/scaleY, width/height, rotation and per-glyph overrides.

Changing a bounding box does not itself change glyph shape. A Web resize gesture may intentionally update font size, glyph scaleX/scaleY, or both; the serialized model must state which. For browser-only arbitrary fonts, use generated device glyph assets/atlas plus metadata, or reject the font with an explicit compatibility message. Do not silently substitute a visually unrelated font and claim parity.

### 3.4 Unsupported properties

Parser/validator must distinguish:
- malformed field/type/range → reject package;
- valid schema field not implemented by this device → reject with a stable unsupported-feature code before activation;
- editor-only fields such as `locked` → may be ignored by runtime by explicit contract;
- optional fields with defined defaults → apply documented defaults.

Never silently drop a property that changes appearance or behavior.

## 4. WFS v1 BLE contract

### 4.1 Services

Legacy service remains during migration:
- Device name prefix: `ESP32_Watch`; current advertised name: `ESP32_Watch_C3`
- Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- Characteristic UUID: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- Properties: READ, WRITE, WRITE_NR, NOTIFY; BLE2902.

WFS v1 service:
- Service: `f2c6b100-2f3a-4c1b-9d70-8e4a5c6b7100`
- Control RX: `f2c6b101-2f3a-4c1b-9d70-8e4a5c6b7100`
- Control TX: `f2c6b102-2f3a-4c1b-9d70-8e4a5c6b7100`
- Data RX: `f2c6b103-2f3a-4c1b-9d70-8e4a5c6b7100`
- Data TX: `f2c6b104-2f3a-4c1b-9d70-8e4a5c6b7100`

### 4.2 Exact 16-byte frame header

All integers are little-endian. The Web and firmware must use these exact offsets:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Magic bytes `57 46` ("WF") |
| 2 | 1 | Protocol major (1) |
| 3 | 1 | Protocol minor (0) |
| 4 | 1 | Message type |
| 5 | 1 | Flags (zero in v1) |
| 6 | 2 | Sequence |
| 8 | 4 | Session ID |
| 12 | 2 | Payload length |
| 14 | 2 | CRC16-CCITT-FALSE over bytes 0–13 |
| 16 | N | Payload |

Historical note: the 6afd283 audit found the firmware using offset 10. Current C++ and Web codecs both execute against the same byte-exact golden frame in `tests/host/protocol_codec_test.cpp` and `tests/host/web_protocol_codec_test.cjs`; the shared host runner checks both. This establishes codec-level compatibility, not a live BLE exchange. Physical BLE interoperability remains untested.

Control payloads are UTF-8 JSON bounded to 220 bytes. Data frames are bounded to 240 total bytes. The negotiated maximum data chunk is `min(216, ATT_MTU - 27)`, subject to the actual stack-reported MTU and packet buffer limit.

### 4.3 Handshake and session

1. Web discovers device using name prefix and requests both legacy and WFS services.
2. Web subscribes to Control TX and Data TX notifications.
3. Web sends HELLO with supported major range and client name, session ID zero.
4. Device selects supported major, creates nonzero random session ID and returns HELLO_ACK.
5. Web stores session ID and requests capabilities.
6. Web requests status.
7. Every subsequent frame must carry the current session ID. A new connection starts a new session; old transfer state is not silently resumed unless a future protocol revision defines persistent resume tokens.

A GATT write success only confirms transport acceptance, not command execution. Wait for the matching application response.

### 4.4 Message set

Preserve current message IDs:
- 01 HELLO; 02 HELLO_ACK
- 03 GET_CAPABILITIES; 04 CAPABILITIES
- 05 GET_STATUS; 06 STATUS
- 10 COMMAND; 11 RESPONSE; 12 EVENT
- 20 TRANSFER_BEGIN; 21 TRANSFER_READY
- 22 DATA; 23 DATA_ACK
- 24 TRANSFER_END; 25 VERIFY_RESULT
- 26 APPLY; 27 APPLY_RESULT
- 28 CANCEL; 29 TRANSFER_PROGRESS
- 30 PING; 31 PONG
- 7F ERROR

Command responses must include requestId, ok, stable error code and safe message. Passwords and credential values must never be echoed or logged.

### 4.5 Chunk transfer semantics

- One active transfer per device.
- Transfer ID is nonzero and bound to the current BLE session.
- TRANSFER_BEGIN reserves the inactive slot only after checking storage and package-size limits.
- DATA payload: transferId uint32, chunkIndex uint32, then 1–216 package bytes.
- Stop-and-wait is the initial reliability mode: Web sends one chunk and waits for ACK before sending the next.
- ACK identifies transferId, highest contiguous accepted chunk index, bytes written and status.
- A duplicate of the immediately previous accepted chunk with identical bytes must be idempotently ACKed; a duplicate with different bytes is rejected. Out-of-order chunks are rejected with expected index.
- Web retries the same chunk/index a bounded number of times after timeout; it must not increment sequence or chunk index as if a lost ACK meant the chunk was not written.
- Transfer cancellation closes and removes only the inactive staging file. The active slot is untouched. A failed transfer remains cancellable so an explicit cancel or BLE session loss can reclaim its partial staging file; the active-slot guard remains the final safety boundary.
- TRANSFER_END triggers file digest, package header, manifest, layout, asset and capability validation.
- APPLY is allowed only after successful verification. Device switches the active pointer only after the candidate is fully valid and preserves the previous slot for rollback.
- Web reports success only after a matching successful APPLY_RESULT and a refreshed device status.

Current source status: firmware accepts an identical retransmission of the immediately previous chunk and rejects conflicting/out-of-order chunks; the Web uploader retries the same encoded frame up to three attempts, preserving sequence and chunk index. The sequencing policy has been extracted into `src/communication/transfer_sequence.h` and covered by `tests/host/transfer_sequence_test.cpp` for accepted chunks, byte-identical duplicates, conflicting duplicates, skipped indexes, invalid payloads and reset. These host tests do not yet exercise SPIFFS write failures, BLE disconnects or power loss; those still require integration and hardware verification.

## 5. WFS1 package contract

Keep the selected 64-byte WFS1 header and manifest/layout/raw-asset payload arrangement in `docs/package/wface-v1.md`. Package bytes are declarative data only.

- Header magic WFS1, version 1, header length 64.
- Header includes manifest length, layout length, asset count, payload length, SHA-256 of complete payload, CRC32/ISO-HDLC over header bytes 0–55, and reserved zero bytes.
- Payload order: manifest JSON, layout JSON, then raw assets in manifest order.
- Asset v1 format: RGB565 little-endian, dimensions 1–240, exact byte length width×height×2.
- Asset offsets must be contiguous, ordered, non-overlapping and in bounds.
- Every asset digest, layout digest and whole payload digest must be checked.
- Unknown required capabilities, duplicate asset IDs, invalid object references, unsupported properties, oversized strings and out-of-range transforms reject the candidate.

The Web must not send Base64/Data URLs or browser Blob URLs as device assets.

## 6. Storage and activation

- SPIFFS remains the package filesystem because the current partition layout already reserves a filesystem region.
- NVS stores only small metadata/settings, including active slot pointer.
- Slot A/B strategy: active slot is immutable during receive; staging writes only to inactive slot.
- Verify file and parse candidate from staging before commit.
- Commit pointer only after validation. Preserve previous slot for recovery.
- Never format SPIFFS automatically on mount failure.
- Report free, used and total filesystem bytes from the actual device.

Package maximum must be the minimum of: configured hard cap, currently available staging capacity while preserving active package, parser/heap-safe limit and negotiated transport limits. Do not claim nominal partition bytes equal usable file capacity.

## 7. Resource budget (provisional, not measured)

| Resource | Current design / estimate | Required measurement |
|---|---|---|
| Face objects | 16 fixed entries | sizeof(FaceModel), static RAM map |
| Face assets | 16 manifest entries | sizeof(FaceModel), asset metadata |
| JSON layout | 16 KiB parser ceiling | peak heap and largest block |
| Manifest | up to 8 KiB validator ceiling | peak heap while parsing |
| BLE packet | 240-byte bounded packet | queue storage and stack heap |
| Data chunk | up to 216 bytes | negotiated MTU on target phone |
| Image draw | one row up to 240 RGB565 pixels (~480 bytes) | renderer stack and SPI time |
| Legacy clock sprite | 160×160 RGB565 (~51.2 KiB) | allocation success and fragmentation |
| WFS models | active/candidate/verified copies | static RAM map and model lifetime |
| Network/TLS | bounded HTTP timeouts; worker task | minimum free heap during TLS/weather |

Before raising limits, record free heap, minimum free heap, largest free block, SPIFFS used/free, parser peak, BLE idle heap, TLS peak and stack watermark on hardware.

## 8. Legacy command mapping

| Legacy command | New command/service mapping | Migration |
|---|---|---|
| `SYNC_TIME:<epoch>` | COMMAND `TIME_SET` | Preserve legacy parser until Web migration verified |
| `SYNC_WEATHER` | COMMAND `WEATHER_REFRESH` | Same service |
| `GPS:<lat>,<lon>` | COMMAND `LOCATION_SET` | Validate finite coordinates and persist |
| `SCAN_WIFI` | COMMAND `WIFI_SCAN` + EVENT/RESPONSE | Asynchronous scan |
| `WIFI:<ssid>,<pass>` | COMMAND `WIFI_CONFIG` | Never echo password |
| `SLEEP_ON` | COMMAND `SLEEP_REQUEST` | Deferred after response; disconnect expected |
| `SLEEP_OFF` | Wake is physical GPIO9 / lifecycle | Legacy no-op/status semantics must be documented |
| BLE enable/disable | Power/connectivity command | Keep legacy menu behavior during migration |

## 9. Migration phases and acceptance

### Phase 0 — Audit
Completed as GitHub source inspection. No build or hardware claim.

### Phase 1 — Architecture and contract
- [x] Module ownership documented.
- [x] Application/sleep lifecycle documented.
- [x] Canonical schema ownership and runtime capability distinction documented.
- [x] WFS UUIDs and exact frame layout documented.
- [x] Transfer/package/storage contract documented.
- [x] C++ codec checked against a fixed golden frame matching the documented Web DataView layout.
- [x] Browser encoder independently checked against the same golden frame by the Node host test.
- [x] Web BLE acknowledgement waiters now have explicit cancellation/timeout cleanup, covered by the Node host test.
- [x] Firmware parser rejects unknown keys at the root, canvas, object and supported nested-object levels instead of silently discarding unrecognized properties.
- [ ] Remaining historical audit/protocol notes reconciled against the latest branch.

### Phase 2 — Core foundation
Acceptance: clean PlatformIO build; single display init; boot fallback; bounded diagnostics; button behavior; storage failure remains non-fatal.

### Phase 3 — Watch-face engine
Acceptance: fixtures from actual editor exports; parser rejects malformed/unsupported data; renderer supports only advertised features; dynamic updates do not redraw whole display each loop.

### Phase 4 — BLE protocol
Acceptance: cross-language frame fixtures; handshake/capabilities/status; legacy command regression; malformed frames rejected without reboot.

### Phase 5 — Package transfer
- [x] Pure stop-and-wait sequencing policy extracted and host-tested (next chunk, identical retry, conflicting retry, out-of-order chunk, invalid payload and reset).
- [x] Staging cancellation policy is host-tested, including cleanup eligibility after a failed transfer.
- [ ] TransferManager + SPIFFS integration tests for write failure, cancellation, disconnect and length/digest mismatch.
- [ ] Two-slot rollback and reboot recovery tests.
Acceptance: ACK retry, duplicate handling, cancel, disconnect recovery, digest validation, two-slot rollback and reboot recovery.

### Phase 6 — Web integration
Acceptance: actual Web Bluetooth session; compatibility preflight; transfer progress; successful activation confirmed by device; status refresh; failure never reports success.

### Phase 7 — Reliability/optimization
Acceptance: repeated idle/connect/disconnect/transfer/sleep/wake/weather tests; measured heap/flash report; no unexplained resets.

## 10. Verification vocabulary

- **Static inspection:** source reviewed; not executed.
- **Host test:** native test executed on host.
- **PlatformIO build:** target firmware compiled.
- **Protocol simulation:** both implementations exchanged or validated known frames in a controlled test.
- **Hardware test:** firmware flashed and behavior observed on the physical ESP32-C3.
- **End-to-end integration:** browser connected to the physical device and completed the workflow.

Never mark one category as passed based on another.
