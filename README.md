# ESP32-C3 Smart Watch — Watch Face Studio

An experimental ESP32-C3 smartwatch/desk clock with a GC9A01 240×240 round TFT, built-in clock/weather screens, BLE controls and a browser-based **Watch Face Studio** editor.

The editor creates declarative watch-face designs, previews them, stores projects and source images locally in the browser, checks the connected watch's capabilities, then uploads a validated WFS1 package over BLE. The firmware does not execute uploaded code.

> **Verification boundary:** host tests and a successful PlatformIO build do not prove physical-board behavior. The ESP32-C3 hardware regression checklist remains a separate step.

## Hardware and wiring

- ESP32-C3 DevKitM-1
- GC9A01 240×240 circular SPI TFT
- BOOT button on GPIO 9
- PlatformIO environment: `c3_clock`

| Signal | ESP32-C3 GPIO |
|---|---:|
| TFT SCLK | 4 |
| TFT MOSI | 6 |
| TFT CS | 7 |
| TFT DC | 2 |
| TFT RST | 3 |
| BOOT button | 9 |
| TFT MISO | Not connected |

Display format is RGB565. The active device profile is `esp32-c3-gc9a01-240`.

## Build and flash

Requirements: VS Code + PlatformIO, a USB data cable and the appropriate USB driver.

1. Clone the repository and open it in VS Code.
2. Create the local Wi-Fi credential file `src/secrets.h` if your checkout/build requires it:

   ```cpp
   #pragma once
   constexpr char WIFI_SSID[] = "YOUR_WIFI_NAME";
   constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
   ```

   Keep this file private; it is gitignored. Never commit real credentials.
3. Build and upload:

   ```sh
   pio run -e c3_clock
   pio run -e c3_clock -t upload
   pio device monitor -b 115200
   ```

The PlatformIO workflow also builds the ESP32-C3 firmware and uploads an `esp32-c3-firmware` artifact. Open the [PlatformIO Build workflow](https://github.com/Quan285001/esp32_very_simple_smart_watch./actions/workflows/platformio-build.yml) and download the artifact from a successful run. Artifact retention is time-limited; do not treat an old artifact as the latest firmware.

## Watch Face Studio

Open the repository's root `index.html` locally through a local HTTP server, or use the published HTTPS GitHub Pages URL. Web Bluetooth requires a supported browser and a secure context (HTTPS or localhost); Chrome on Android is a typical option.

1. Power on the watch and enable Bluetooth from **Menu → Connectivity → Bluetooth**.
2. Open Watch Face Studio and choose **Connect**.
3. Select the device named `ESP32_Watch_C3`.
4. The editor negotiates WFS BLE v1, reads the device profile/capabilities and checks the design before enabling upload.
5. Edit the face, then choose **Upload to watch**. The device receives the package into its inactive storage slot, verifies it, and activates it only after the explicit Apply step.

The Web editor stores projects and original images in browser IndexedDB. Export a portable `.wfsproject.json` project to move a design and its source images to another browser/device. A raw Watch Face JSON file does not bundle its images.

### On-device elements currently supported

- Text
- Digital clock
- Analog clock
- Date
- Weather temperature and condition
- Shapes: rectangle, circle, rounded rectangle and line
- Image assets converted to RGB565 little-endian

The current firmware target is one fixed 240×240 circular display. Partial opacity/alpha blending, arbitrary rotation, arbitrary fonts, compressed image decoding on-device and advanced weather fields are not supported. The editor's device-compatibility preflight rejects unsupported combinations before upload.

## Built-in watch operation

The BOOT button supports short press, double press and hold:

| Input | Clock | Weather | Menu |
|---|---|---|---|
| Short press | Open weather | Advance forecast/details | Next item |
| Double press | — | Toggle details | Select |
| Hold about 1 second | Open menu | Open menu | Select |

Menu groups include Clock, Weather, Connectivity and System. Connectivity controls Wi-Fi and Bluetooth; System includes time/weather sync, device information, Light Sleep and restart.

At startup, the firmware restores persistent settings/cache, performs bounded network/time/weather work when configured, initializes BLE, then turns Wi-Fi off. Enable Wi-Fi when a network operation is needed. Cached weather can still be shown offline.

Auto Light Sleep is limited to the clock screen after the configured idle timeout (15 seconds by default) and is suppressed during an active BLE connection or background work. Manual Light Sleep remains available from the menu. Wake with the BOOT button.

## Architecture and technical references

- [Technical Reference](docs/TECHNICAL_REFERENCE.md) — hardware, firmware modules, editor, package, BLE, storage, diagnostics and verification.
- [Documentation index](docs/README.md)
- [Phase Completion Matrix](docs/architecture/phase-completion-matrix.md)
- [Firmware Rebuild Status](docs/architecture/rebuild-implementation-status.md)
- [WFS BLE v1 Protocol](docs/protocol/protocol-v1-draft.md)
- [WFS1 Package Format](docs/package/wface-v1.md)
- [Portable Project Format](docs/architecture/portable-project-format.md)
- [Hardware Regression Checklist](docs/architecture/hardware-regression-checklist.md)

## Test commands

Run the complete host suite:

```sh
python scripts/run_host_tests.py
python scripts/validate_schemas.py
python scripts/validate_device_compatibility.py
python scripts/validate_display_profile.py
python scripts/validate_canvas_boundary.py
python scripts/validate_web.py
pio run -e c3_clock
```

CI checks schemas, profile/canvas contracts, Web assets and JavaScript syntax, host protocol/transfer/parser/renderer tests, then compiles the ESP32-C3 firmware. GitHub Pages deployment is checked separately.

## Important limitations

- No physical ESP32-C3/GC9A01 browser-to-device test is claimed by CI.
- BLE bonding currently uses IO capability NONE (Just Works): link encryption is enabled, but MITM protection is not provided.
- Only the ESP32-C3 + GC9A01 profile is implemented; runtime display-profile switching and alternate display backends are not implemented.
- Built-in legacy screen renderers still have transitional direct TFT access.
- A missing/invalid active-slot commit pointer falls back to the built-in face; the firmware does not automatically promote the other slot.
- This is an experimental prototype, not a security-hardened consumer product.

See the hardware checklist before treating the rebuild as physically validated.

## License

No license has been specified. Reuse and redistribution are not automatically granted.
