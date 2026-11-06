# Face Studio / WFace Bytecode Specification v1 (Draft)

Status: architecture draft; no VM runtime is implemented by this document.

## 1. Product goal

Face Studio lets a user design a watch face on a phone or web dashboard, compile the design into a portable package, save multiple designs, and transfer one to a watch over BLE.

A watch face is an isolated program running inside a constrained virtual machine. It is not a replacement firmware image and must never be able to execute arbitrary native machine code.

Pipeline:

1. Face Studio editor creates a design document and asset set.
2. Face Compiler validates the design against a target Display Profile and Hardware Capability Profile.
3. Compiler emits WFace bytecode plus assets and metadata.
4. Phone/browser stores the package locally; optional account storage can sync it.
5. BLE transfers the package in chunks to a staging area on the device.
6. Device validates compatibility and integrity, then atomically activates it.
7. Native Renderer APIs perform drawing; the VM controls layout, state and permitted behavior.

The compiler may accept a constrained C++-like Watch Face language/API, but v1 must not promise support for arbitrary C++. A future compiler may use a C++ subset or a dedicated source DSL and lower it to the same bytecode.

## 2. Core architecture

- **Face Studio UI:** canvas, layer panel, properties inspector, asset manager, live preview and saved-face library.
- **Design Engine:** normalized layout, anchors, clipping, typography, opacity, z-order and animation timeline.
- **Face Compiler:** validates, optimizes and emits bytecode/assets.
- **WFace Package Manager:** package parsing, transfer, checksums, storage, activation and rollback.
- **Face VM:** bounded execution of approved instructions.
- **Native Renderer:** drawing primitives, text, images, clock hands and weather components.
- **Display Adapter:** maps renderer operations to GC9A01, ST7789, ILI9341 or future display drivers.
- **Capability Profiles:** describe display geometry separately from MCU/RAM/Flash/CPU capabilities.

## 3. WFace package layout

Use a versioned container. Initial logical layout:

| Section | Purpose |
|---|---|
| Header | Magic, container version, header size, total package size |
| Manifest | Face ID, name, compiler version, VM ABI, renderer API version |
| Compatibility | Display constraints and required capabilities |
| Program | Bytecode entry point, code length and instruction-set version |
| Assets | Images, glyphs/fonts, palettes and optional animation frames |
| Integrity | Per-section lengths and CRC32; optional signature for trusted distribution |

Suggested extension: `.wface`.

Do not embed large images as base64 inside JSON. Manifest metadata may be JSON during prototyping, but the production package should use bounded binary sections or a compact serialization format.

All offsets and lengths must be checked for overflow and bounds before reading. Reject unknown mandatory sections, malformed lengths, invalid asset references, unsupported VM versions and packages exceeding configured limits.

## 4. Compatibility model

A package declares minimum requirements; the device reports its capabilities.

### Display Profile

- width, height and shape (circle, square, rectangle)
- driver identifier and pixel format (for example RGB565)
- rotation and safe area
- refresh-rate target
- clipping/alpha-blending support
- available drawing primitives

### Hardware Capability Profile

- MCU family and CPU class
- free/usable RAM budget for a face
- Flash/storage budget
- supported image decoders and asset formats
- maximum supported animation complexity
- VM instruction budget and renderer API features

A 240x240 circular GC9A01 display and a 466x466 AMOLED are different profiles. The latter is an example target, not an assumption about the current device.

Use normalized coordinates (0.0–1.0) with explicit anchor points. The Display Adapter converts them to pixels and applies shape clipping/safe areas.

## 5. VM execution model

The VM is a small, deterministic, sandboxed interpreter. It must not expose raw pointers, arbitrary memory access, filesystem access, network sockets, GPIO access, dynamic library loading or native-code execution.

The VM calls a fixed native API table. Examples:

- `canvas.clear(color)`
- `draw.text(textId, x, y, styleId, opacity)`
- `draw.image(assetId, x, y, width, height, opacity)`
- `draw.line(x1, y1, x2, y2, color, thickness)`
- `draw.circle(cx, cy, radius, color, fill)`
- `clock.get_time()`
- `weather.get_snapshot()`
- `layer.set_visible(layerId, visible)`
- `animation.set_value(propertyId, value)`

The VM does not call hardware drivers directly. Native APIs validate all arguments and translate operations to the Renderer.

### Initial instruction families

The exact opcode numbers are reserved until the binary encoding is approved.

- constants and literals
- load/store bounded local variables
- integer/fixed-point arithmetic
- comparisons and boolean operations
- bounded conditional branches
- bounded loops
- native API calls
- event/frame return
- controlled animation/tween operations

Prefer fixed-point arithmetic in v1 to avoid requiring floating-point behavior to be identical across MCUs.

## 6. Safety and resource limits

Every execution must have enforceable limits:

- maximum bytecode size
- maximum stack depth and local-variable count
- maximum instruction count per frame/event
- maximum execution time per frame
- maximum asset count and individual asset size
- maximum decoded image dimensions
- maximum layer count
- maximum animation tracks and frame rate
- maximum memory allocated by a face

The VM must yield or abort when the instruction/time budget is exhausted. A faulting face must be disabled and the previous known-good face restored.

Initial numeric limits are intentionally not fixed in this draft. They must be measured on the ESP32-C3 with BLE enabled, weather operations, display rendering and worst-case image decoding. The compiler should target a capability profile, not assume one global limit.

## 7. Asset and animation strategy

- Current ESP32-C3 target: static RGB565 background, bounded fonts/glyphs and lightweight procedural animation.
- Avoid keeping multiple full-screen frames in RAM. A 240x240 RGB565 frame is 115,200 bytes uncompressed.
- Stream assets from Flash where possible; use bounded line/tile buffers and partial updates.
- Stronger hardware profiles may enable multi-frame animation, larger assets and more concurrent effects.
- Web input may include common image formats, but the compiler converts them into device-supported assets before packaging.

## 8. Storage, transfer and activation

Use two logical slots:

- **active slot:** last known-good package currently used by the renderer
- **staging slot:** incoming package being received and validated

BLE transfer protocol should include package ID, total length, chunk index, chunk length, transfer/session ID, per-chunk acknowledgment/retry and final CRC. Do not assume one BLE write can carry a whole package.

Activation sequence:

1. Receive into staging storage.
2. Validate header, lengths, checksums, VM ABI and capabilities.
3. Verify all asset references and resource budgets.
4. Mark staging package valid.
5. Switch active package atomically.
6. If startup/render validation fails, roll back to the previous active package.

Keep multiple saved faces where storage permits. Reuse unchanged assets by content hash so changing text color does not retransmit a large background image.

## 9. Versioning

Track these independently:

- WFace container version
- bytecode instruction-set version
- VM ABI version
- native Renderer API version
- Display Profile identifier/version
- compiler version

The device must reject unsupported required versions with a clear error code. Optional unknown metadata may be ignored.

## 10. Development phases

1. Finalize package header, manifest and VM ABI.
2. Implement a host-side reference VM and bytecode test suite.
3. Implement native renderer stubs and preview parity tests.
4. Measure runtime memory on ESP32-C3 before selecting hard limits.
5. Port the VM to firmware behind a feature flag.
6. Add package storage and BLE staging/rollback.
7. Add compiler integration to Face Studio.
8. Add saved-face library and capability-aware design warnings.

## 11. Explicit non-goals for v1

- arbitrary user-supplied C++ compilation
- native machine code execution on the watch
- arbitrary peripheral access
- unrestricted scripts or dynamic plugins
- full-motion video playback on ESP32-C3
- assuming all displays and MCUs support the same effects
