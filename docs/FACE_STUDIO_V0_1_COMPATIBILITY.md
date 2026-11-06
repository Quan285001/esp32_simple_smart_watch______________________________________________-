# Face Studio visual editor — firmware compatibility

Status: the browser editor is now a multi-panel visual design workspace. Firmware transfer remains the v0.1 native-renderer profile protocol.

## Browser editor capabilities

The Face Studio workspace in `index.html` provides:

- A two-page dashboard: **Watch Control** and **Face Studio**, with separate tab navigation.
- A Google Photos-inspired category home. Users select one task (style, background, colors, clock/date, photos, text/fonts, layout, layers, or saved designs) and only that task's controls are shown.
- Five browser preview styles: Classic analog, Minimal, Digital, Pilot, and Diver.

- A three-panel desktop workspace: asset/tools sidebar, live round-watch canvas, and property inspector.
- Responsive stacked layout for narrow screens.
- Image upload through a visible Add Photo action, accepting browser-supported `image/*` formats up to 20 MB, then client-side downsampling to a maximum 512-pixel edge, circular crop preview, zoom, and X/Y positioning.
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


## Weather refresh behavior

Manual weather refresh and model changes now queue a fetch request and wake the Wi-Fi station using the credentials stored in Preferences when the radio is intentionally off. If a startup Wi-Fi scan is still running, the request is queued until the scan releases the radio. When the radio was woken only for this one-shot request, it is powered down again after the forecast task completes. The selected model is persisted independently of the forecast result. The UI reports model-save and forecast-download outcomes separately. A queued reconnect is bounded to 60 seconds; if Wi-Fi cannot reconnect, the previous weather cache is retained and the pending request is cleared so automatic sleep can resume.

The five Face Studio style templates are currently browser-preview/project styles. They are not yet applied by the ESP32 native renderer; the current firmware BLE profile remains colors plus seconds/date flags.
