# ESP32 Very Simple Smart Watch

A lightweight smartwatch / desk-clock project built around the **ESP32-C3** and a **GC9A01 240 × 240 round TFT display**.

The goal is to keep the firmware small and practical: show the time, display weather information, and provide simple one-button navigation. Bluetooth Low Energy (BLE) is used for optional phone/browser control.

> This is an experimental personal project. Hardware revisions and firmware behavior may change.

## Technical documentation

Start with the [Technical Reference](docs/TECHNICAL_REFERENCE.md) for the complete firmware/Web architecture, hardware contract, build commands, Watch Face model, WFS1 package, BLE protocol, storage transaction, diagnostics and test strategy.

The full document set is indexed in [docs/README.md](docs/README.md). For implementation progress, see the [Phase Completion Matrix](docs/architecture/phase-completion-matrix.md) and [Firmware Rebuild Status](docs/architecture/rebuild-implementation-status.md). Physical-board verification is tracked in the [Hardware Regression Checklist](docs/architecture/hardware-regression-checklist.md).

## Features

- **Analog clock** with time synchronization through NTP (UTC+7).
- **Weather forecast** from Open-Meteo, using Ho Chi Minh City as the default location.
- Current temperature and conditions, daily high/low, hourly forecast, wind, pressure, rain, sunrise/sunset, and optional lunar information.
- **Persistent weather cache** stored in ESP32 NVS, so the last saved forecast can still be displayed while offline.
- Weather refresh interval: **30 minutes** while Wi-Fi is enabled and connected.
- **Wi-Fi and Bluetooth controls** in the on-device menu.
- **BLE control page** for time/weather synchronization, location updates, Wi-Fi scanning/configuration, and Light Sleep.
- **Light Sleep** request with Wi-Fi/Bluetooth shutdown before sleep.

## Hardware

| Component | Specification |
|---|---|
| Microcontroller | ESP32-C3 |
| Display | GC9A01 round TFT, 240 × 240 |
| Input | BOOT button |
| Firmware framework | Arduino |
| Build system | PlatformIO |

### Wiring

| GC9A01 / Input | ESP32-C3 GPIO |
|---|---:|
| SCLK | GPIO 4 |
| MOSI | GPIO 6 |
| CS | GPIO 7 |
| DC | GPIO 2 |
| RST | GPIO 3 |
| BOOT button | GPIO 9 |

The display uses SPI. MISO is not connected.

## Getting started

### 1. Install the development tools

1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. Install the **PlatformIO IDE** extension.
3. Install the USB driver required by your ESP32-C3 board, if your operating system does not recognize it automatically.
4. Connect the ESP32-C3 to your computer with a USB data cable.

### 2. Clone the repository

```bash
git clone https://github.com/Quan285001/esp32_very_simple_smart_watch..git
cd esp32_very_simple_smart_watch.
```

Open the project folder in VS Code. PlatformIO should detect the project from `platformio.ini` and install the required libraries during the first build.

### 3. Configure Wi-Fi credentials

The private credential file is intentionally excluded from Git.

Create a file named **`src/secrets.h`** with this content:

```cpp
#pragma once

constexpr char WIFI_SSID[] = "YOUR_WIFI_NAME";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
```

Replace the placeholders with your own Wi-Fi network name and password.

**Do not commit or upload `src/secrets.h` to GitHub.** It is already listed in `.gitignore`. If you have previously exposed real credentials, change the Wi-Fi password.

### 4. Build and upload

The PlatformIO environment is named `c3_clock`.

Build:

```bash
pio run -e c3_clock
```

Upload firmware:

```bash
pio run -e c3_clock -t upload
```

Open the serial monitor:

```bash
pio device monitor -b 115200
```

You can also use the **Build**, **Upload**, and **Monitor** buttons in the PlatformIO toolbar.

## Using the watch

The physical BOOT button supports short press, double press, and long press.

| Input | On the clock screen | On the weather screen | In a menu |
|---|---|---|---|
| Short press | Open weather | Move through forecast/details | Move to the next item |
| Double press | — | Toggle weather details | Select the highlighted item |
| Hold for about 1 second | Open the main menu | Open the main menu | Select the highlighted item |

### Menu structure

- **Main Menu**
  - Clock
  - Weather
  - Connectivity
  - System
- **Connectivity**
  - Bluetooth — enable or disable BLE advertising.
  - Wi-Fi — enable or disable the Wi-Fi station interface.
  - Back
- **System**
  - Sync Time
  - Sync Weather
  - Device Info
  - Light Sleep
  - Restart
  - Back

To return from the Device Info screen, use the BOOT button.

## Startup and connectivity behavior

At startup, the firmware follows this sequence:

1. Restore saved Wi-Fi settings, location, and weather cache from NVS.
2. Attempt to connect to the saved Wi-Fi network.
3. Synchronize local time using NTP (UTC+7).
4. Fetch and save the weather forecast.
5. Request optional sunrise/sunset and lunar information.
6. Initialize BLE.
7. Turn Wi-Fi off after the startup synchronization sequence, then start the watch interface.

This means the radios are not necessarily left on after startup. Enable Wi-Fi or Bluetooth from **Menu → Connectivity** when you need them.

- If Wi-Fi is off or unavailable, the watch can show its saved weather cache.
- Automatic weather updates do not run while Wi-Fi is disabled.
- When Wi-Fi is enabled and connected, the firmware can refresh weather periodically.
- Saved settings and weather cache are stored in NVS; a normal restart does not erase them.

## BLE control page

The repository includes `index.html`, a browser-based control page.

It provides controls for:

- Connecting to the watch over Bluetooth LE.
- Synchronizing time.
- Requesting a weather update.
- Sending the phone/browser location to the watch.
- Requesting Light Sleep.
- Scanning nearby Wi-Fi networks.
- Sending Wi-Fi credentials to the watch for storage and connection.

### Open the page locally

You can open `index.html` in a browser, or serve the repository folder through a local development server. Web Bluetooth requires a supported browser and a secure context, normally **HTTPS** or **localhost**.

For Android, use a browser that supports Web Bluetooth (for example, a compatible version of Chrome). Bluetooth permissions and Location permissions must be granted when requested.

### Optional: publish the page with GitHub Pages

The repository's `index.html` can be hosted as a static page:

1. Open the repository on GitHub.
2. Go to **Settings → Pages**.
3. Under **Build and deployment**, choose **Deploy from a branch**.
4. Select branch `main` and folder `/ (root)`.
5. Click **Save** and wait for GitHub Pages to publish the site.
6. Open the published HTTPS URL shown on the Pages settings screen.

GitHub Pages hosting does not install firmware on the ESP32. It only hosts the browser control page. The watch must be powered on, and Bluetooth must be enabled from its menu before the page can connect.

## Weather data

- Default location: Ho Chi Minh City, Viet Nam.
- Default coordinates: latitude `10.8231`, longitude `106.6297`.
- Forecast provider: [Open-Meteo](https://open-meteo.com/).
- Time zone used by the forecast request: `Asia/Ho_Chi_Minh`.
- Refresh interval: 30 minutes.

The browser control page can send updated coordinates over BLE. The firmware stores the coordinates in NVS and uses them for later weather requests.

## Project structure

```text
.
├── index.html                 # Browser-based BLE control page
├── platformio.ini             # PlatformIO environment and build configuration
├── src/
│   ├── main.cpp               # Firmware setup, main loop, Wi-Fi and sleep control
│   ├── config.h               # Hardware, time, weather and screen configuration
│   ├── secrets.h              # Local Wi-Fi credentials (create this yourself; gitignored)
│   ├── app/                   # Application state and screen/menu controller
│   ├── face/                  # Clock, weather, menu and information screens
│   ├── input/                 # BOOT button handling
│   └── services/              # Time, weather and BLE services
└── scripts/
    └── patch_tft_c3.py        # TFT_eSPI compatibility patch for ESP32-C3
```

## Troubleshooting

**The project cannot find `secrets.h`**

Create `src/secrets.h` as described in the Wi-Fi configuration section. This file is intentionally not stored in the repository.

**The watch starts without current weather**

Check that the Wi-Fi credentials are correct and that the network has internet access. The firmware has a bounded startup connection attempt; if it fails, it continues using the saved cache when one is available.

**The browser cannot find the watch**

Enable Bluetooth from **Menu → Connectivity → Bluetooth**. Make sure the browser supports Web Bluetooth, grant Bluetooth permission, and keep the watch nearby.

**The browser page opens but Bluetooth does not work**

Web Bluetooth requires a supported browser and a secure context. Use the HTTPS URL published by GitHub Pages or `localhost`; opening the page from an insecure network URL may block Bluetooth access.

**The display or button does not respond**

Check the GPIO wiring against the pin table, use a USB data cable, and inspect the serial log at 115200 baud.

## Security notes

- Keep `src/secrets.h` private.
- The BLE control page can transmit Wi-Fi credentials to the watch. Use it only on a trusted device and connection.
- This is a prototype, not a security-hardened consumer device. Review the network/TLS and BLE implementation before deploying it in a sensitive environment.

## License

No license has been specified yet. Until a license is added to the repository, reuse and redistribution are not automatically granted.
