# Face Studio visual editor — firmware compatibility

Status: the browser editor is now a multi-panel visual design workspace. Firmware transfer remains the v0.1 native-renderer profile protocol.

## Browser editor capabilities

The Face Studio workspace in `index.html` provides:

- A three-panel desktop workspace: asset/tools sidebar, live round-watch canvas, and property inspector.
- Responsive stacked layout for narrow screens.
- PNG/JPG/WebP upload, client-side downsampling to a maximum 480-pixel edge, circular crop preview, zoom, and X/Y positioning.
- Text layers, live time/date widgets, draggable positioning, visibility, opacity, font family, weight, size, alignment, and uppercase controls.
- System font selection and custom TTF/OTF font loading for browser preview. Custom font data is included in exported project files.
- A hue/saturation/brightness color picker, HEX entry, swatches, and independent background/dial/hand/accent targets.
- Layer selection, reordering, visibility, deletion, undo/redo, grid toggle, and zoom controls.
- Browser-local project library and import/export of `.wface` project files. Uploaded images are downsampled before storage/export.

## What the current ESP32-C3 firmware can apply

The firmware still renders its native analog face and accepts only:

- Background color (RGB565)
- Dial fill color (RGB565)
- Clock-hand color (RGB565)
- Accent color (RGB565)
- Show/hide seconds (flag bit 0)
- Show/hide date (flag bit 1)

The web editor deliberately keeps image assets, arbitrary text layers, custom fonts, and layout metadata inside the browser project and exported `.wface` file. These are previewable/editable, but they are **not yet rendered by the current firmware**. The inspector displays this compatibility limitation instead of pretending those assets were installed on the watch.

## BLE wire format

The editor sends exactly 20 ASCII bytes:

```text
FS:<BG><DIAL><HAND><ACCENT><FLAGS>
```

- `FS:`: 3-byte command prefix
- Each color: four hexadecimal digits representing one RGB565 value
- Flags: one decimal digit from 0 to 3
- Total: 20 bytes, compatible with the default ATT payload size (20 bytes)

The device responds with `FACE_OK` after the NVS record is written and verified, or `ERR_FACE` if the frame is malformed or persistence fails.

The firmware persists the profile as one 9-byte NVS record in namespace `face`, key `profile`, and loads it at boot.

## Package and VM roadmap

The exported `.wface` file is a browser project document, not executable firmware bytecode. It may contain embedded image/font data and layer metadata. The compact BLE command sends only the supported base profile.

The next firmware stages are:

1. Define versioned asset and layer records with strict size/dimension limits.
2. Add chunked BLE transfer with sequence numbers, acknowledgements, checksum, and staging storage.
3. Add a native asset renderer for the GC9A01 profile and measure actual RAM/flash use.
4. Introduce the bounded WFace bytecode VM only after the asset/rendering path is stable.
5. Add atomic activation and rollback so an invalid face cannot replace the last working face.

No arbitrary C++, native executable, filesystem access, or unrestricted script is accepted by the current firmware.
