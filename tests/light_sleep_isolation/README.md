# ESP32-C3 Light Sleep isolation test

This is a separate minimal PlatformIO project. It does not modify the main watch firmware.

## Build and flash (from repository root)

If `pio` is available in PowerShell:

```powershell
pio run -d tests/light_sleep_isolation -e c3_sleep_test
pio run -d tests/light_sleep_isolation -e c3_sleep_test -t upload
pio -d tests/light_sleep_isolation device monitor -b 115200
```

If PowerShell does not recognize `pio`, use the PlatformIO virtual-environment executable:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -d .\tests\light_sleep_isolation -e c3_sleep_test
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -d .\tests\light_sleep_isolation -e c3_sleep_test -t upload
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -b 115200
```

Confirm the build output says `Processing c3_sleep_test`, not the main `c3_clock` environment.

## Test 2: GPIO9 + timer wake

The firmware configures:
- GPIO9 / BOOT as an active-low, level-triggered wake source.
- An 8-second timer as a fallback wake source.

After the screen turns red:
1. **GPIO test:** press and hold BOOT. The screen should turn green shortly after the button is pressed.
2. **Timer test:** reset the board and do not press BOOT. The screen should turn green after about 8 seconds.

A successful return from `esp_light_sleep_start()` changes the display to green. The firmware then waits briefly before printing the wake result because USB Serial/JTAG may disconnect during Light Sleep and reconnect after wake.

## Display and serial results

- **Red:** initialized and about to enter Light Sleep.
- **Green:** Light Sleep returned; the chip woke from GPIO9 or the timer.
- **Yellow:** one of the wake-source configuration calls failed.
- Serial line `wake_cause=...` reports the source. ESP-IDF uses `ESP_SLEEP_WAKEUP_GPIO` for GPIO wake and `ESP_SLEEP_WAKEUP_TIMER` for timer wake.

This test intentionally does not initialize Wi-Fi, BLE, NVS, or application tasks. Use a stable power source. The TFT panel should retain the red frame while the ESP32-C3 sleeps.
