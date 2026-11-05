# ESP32-C3 Light Sleep isolation test

This is a separate minimal PlatformIO project. It does not modify the main firmware.

## Build and flash (from repository root)

```powershell
pio run -d tests/light_sleep_isolation
pio run -d tests/light_sleep_isolation -t upload
pio device monitor -b 115200
```

## Expected display result

- **Red:** initialization completed; firmware is entering timer-only Light Sleep.
- **Green after about 8 seconds:** `esp_light_sleep_start()` returned successfully.
- **Yellow:** RTC timer wake configuration failed.
- **Stays red beyond 12–15 seconds:** the isolated timer wake test did not return.

The test deliberately does not initialize Wi-Fi, BLE, GPIO wake, NVS, or application tasks. Use a stable external power source for the unplugged test. The TFT panel should retain the red frame while the ESP32-C3 sleeps.
