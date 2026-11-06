# Face Studio v0.1 — Firmware Compatibility Contract

Status: first end-to-end profile implementation for the current ESP32-C3 / GC9A01 firmware.

## What works in v0.1

The web dashboard contains a Face Studio editor with a live browser preview, a local saved-design library, and import/export of `.wface` profile files. The editor targets the existing 240×240 round GC9A01 clock face.

The current firmware renderer accepts these properties:

- Background color (RGB565)
- Dial fill color (RGB565)
- Clock-hand color (RGB565)
- Accent color (RGB565)
- Show/hide seconds (flag bit 0)
- Show/hide date (flag bit 1)

The profile is persisted as one 9-byte NVS record in namespace `face`, key `profile`. The firmware loads it during boot and applies changes immediately after a successful BLE command.

## BLE wire format

The editor sends exactly 20 ASCII bytes:

```text
FS:<BG><DIAL><HAND><ACCENT><FLAGS>
```

- `FS:`: 3-byte command prefix
- Each color: four hexadecimal digits representing one RGB565 value
- Flags: one decimal digit from 0 to 3
- Total: 20 bytes, compatible with the default ATT payload size (20 bytes)

Example shape (values shown as placeholders):

```text
FS:000018C3FFFFFBE03
```

The device responds with `FACE_OK` after the NVS record is written and verified, or `ERR_FACE` if the frame is malformed or persistence fails.

The handler validates the exact frame length, every hexadecimal digit, and the flag range before calling the renderer. No arbitrary source code, script, pointer, image decoder, or native executable is accepted.

## Saved designs and transfer scope

- Browser storage uses localStorage and is local to that browser profile/device.
- Export/import uses a small JSON `.wface` profile file.
- Only the compact supported properties above are transferred to the watch.
- The profile name is browser-side metadata and is not stored by the firmware yet.
- v0.1 does not upload images, fonts, arbitrary layers, animation tracks, or bytecode.

## Relationship to the planned VM

This profile is a deliberate first vertical slice, not the completed WFace bytecode VM described in `FACE_STUDIO_BYTECODE_SPEC_V1.md`. It establishes the editor → validation → compact package → BLE → persistent firmware renderer path without introducing a new interpreter or significant runtime RAM cost.

Future versions can extend the package format and renderer API while retaining this profile as a compatibility layer. VM execution, chunked asset transfer, multiple device-side face slots, and rollback remain future work and must be implemented and tested separately.
