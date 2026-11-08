#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <TFT_eSPI.h>

#include "app_runtime.h"
#include "app_state.h"
#include "app_controller.h"
#include "boot_coordinator.h"
#include "../config.h"
#include "../input/button_manager.h"
#include "../services/time_service.h"
#include "../services/weather_service.h"
#include "../services/weather_coordinator.h"
#include "../services/wifi_service.h"
#include "../services/ble_service.h"
#include "../system/diagnostics.h"
#include "../system/system_manager.h"
#include "../display/display_manager.h"
#include "../watchface/face_manager.h"
#include "../communication/transfer_manager.h"

extern void handleBLECommands();
extern void stopBootTerminalDisplay();
extern void bootTerminalLog(const char* msg, uint16_t color);

extern void drawAnalogClockFace(
    struct tm timeinfo,
    int millisValue,
    int heartRate,
    int steps
);

extern void drawMenuScreen(
    int selectedIndex,
    MenuLevel level,
    bool wifiStatus,
    bool wifiConnected
);

extern void drawWeatherFaceData(const WeatherData& weather);
extern void updateWeatherFaceAnimation(const WeatherData& weather);

namespace {
uint32_t lastClockDrawMs = 0;
uint32_t lastMenuFrameMs = 0;
uint32_t lastWeatherFrameMs = 0;
bool weatherFaceInitialized = false;
bool bootUiReady = false;
}

void loop() {
    // During startup, the boot task owns the display. Do not run normal UI,
    // button, or periodic services until network work and BLE initialization
    // have both reached their terminal state.
    if (!bootUiReady) {
        if (BootCoordinator::takeBleInitRequest()) {
            bootTerminalLog("[BLE] Initializing Bluetooth", TFT_YELLOW);
            beginBLE();
            if (isBLEEnabled()) {
                toggleBLE();
            }
            SystemDiagnostics::logMemory("after BLE init");
            WiFi.disconnect(false, false);
            WiFi.mode(WIFI_OFF);
            wifiEnabled = false;
            WifiService::resetConnectionMonitor(millis());

            bootTerminalLog("[OK] Bluetooth initialized; radios idle", TFT_GREEN);
            bootTerminalLog("[BOOT] Startup complete", TFT_GREEN);
            delay(350);

            tft.fillScreen(TFT_BLACK);
            stopBootTerminalDisplay();
            drawCurrentScreen(true);
            bootUiReady = true;
            SystemManager::resetAutoSleepActivity(
                millis(), getLastButtonActivityMs(), isBLEConnected());
            Serial.println("[BOOT] Clock interface ready.");
        }
        delay(2);
        return;
    }

    const uint32_t now = millis();

    // Bounded diagnostics cadence avoids serial flooding in the UI loop.
    SystemDiagnostics::service();

    serviceTimePersistence();

    // -------------------------------------------------
    // BUTTON INPUT
    // -------------------------------------------------

    updateButtonManager();

    // SystemManager consumes physical button activity for the auto-sleep idle timer.

    handleBLECommands();
    serviceBLE();
    WeatherCoordinator::serviceNotification();
    if (WeatherCoordinator::takeFaceInvalidation()) {
        weatherFaceInitialized = false;
    }
    WifiService::serviceScan();

    // Automatic sleep is deliberately disabled during an active BLE session.
    // Manual Sleep from the menu still bypasses this connection guard.
    const bool backgroundWorkActive =
        BootCoordinator::isBusy() || WeatherCoordinator::isFetchRunning() || WifiService::isScanBusy();
    SystemManager::serviceAutoSleep(
        now, getLastButtonActivityMs(), isBLEConnected(),
        backgroundWorkActive, currentScreen == SCREEN_CLOCK);
    if (SystemManager::sleepRequested()) {
        // Keep the UI loop responsive while HTTPS/TLS work finishes.
        // Sleep is entered on a later loop iteration once radio tasks are idle.
        if (backgroundWorkActive) {
            delay(2);
            return;
        }
        SystemManager::enterLightSleep();
        return;
    }

    // -------------------------------------------------
    // WIFI CONNECTION MONITOR + DIAGNOSTICS
    // -------------------------------------------------

    const WifiService::ConnectionSnapshot wifiLink =
        WifiService::serviceConnectionMonitor(
            now, wifiEnabled, BootCoordinator::isBusy());
    const bool wifiConnected = wifiLink.connected;

    if (wifiLink.justConnected) {
        Serial.printf(
            "[WIFI] Connected. IP: %s, RSSI: %d dBm\n",
            WiFi.localIP().toString().c_str(),
            WiFi.RSSI()
        );

        // Refresh IP-based timezone/location and restart NTP after Wi-Fi reconnect.
        // This also adapts the clock and forecast location when travelling.
        beginTimeService();
        WeatherCoordinator::onWifiConnected(now);
    }

    if (wifiLink.justDisconnected) {
        Serial.println("[WIFI] Connection lost.");
    }

    const bool hasWeatherData = weatherDataValid;
    WeatherCoordinator::service(
        now, wifiEnabled, wifiConnected, hasWeatherData,
        WifiService::isScanBusy(), BootCoordinator::isBusy(),
        TransferManager::isBusy());

    // -------------------------------------------------
    // MENU ANIMATION
    // -------------------------------------------------

    if (
        currentScreen == SCREEN_MENU &&
        now - lastMenuFrameMs >= 40
    ) {
        drawMenuScreen(
            menuIndex,
            currentMenuLevel,
            wifiEnabled,
            wifiConnected
        );

        lastMenuFrameMs = now;
    }

    // -------------------------------------------------
    // ANALOG CLOCK
    // -------------------------------------------------

    if (
        currentScreen == SCREEN_CLOCK &&
        now - lastClockDrawMs >= 40
    ) {
        static int cachedSecond = -1;
        static struct tm cachedTime = {};
        static uint32_t secondStartedAt = 0;
        static bool cachedTimeValid = false;

        struct tm currentTime = {};

        // Accept only a genuinely synchronized calendar time. Some ESP32
        // time reads can briefly return an epoch/default value around radio
        // shutdown or wake; never replace a good clock with 1970.
        if (getLocalTime(&currentTime, 0) &&
            currentTime.tm_year >= (2024 - 1900)) {
            if (!cachedTimeValid || currentTime.tm_sec != cachedSecond) {
                cachedSecond = currentTime.tm_sec;
                cachedTime = currentTime;
                secondStartedAt = now;
                cachedTimeValid = true;
            }
        }

        // Until the first valid NTP time arrives, show a stable placeholder.
        // Once synchronized, preserve the last good time through transient
        // getLocalTime() failures instead of jumping back to a fake time.
        if (!cachedTimeValid) {
            cachedTime.tm_hour = 12;
            cachedTime.tm_min = 0;
            cachedTime.tm_sec = (now / 1000) % 60;
            cachedTime.tm_wday = 0;
            cachedTime.tm_mday = 1;
            secondStartedAt = now - (now % 1000);
        }

        uint32_t elapsed =
            now - secondStartedAt;

        if (elapsed > 999) {
            elapsed = 999;
        }

        if (FaceManager::hasActiveFace()) {
            static int lastCustomFaceSecond = -1;
            if (cachedTime.tm_sec != lastCustomFaceSecond) {
                const uint32_t renderStartedUs = micros();
                const FaceRenderStatus status = FaceManager::renderActiveDynamic(cachedTime, getWeatherSnapshot());
                const uint32_t renderDurationUs = micros() - renderStartedUs;
                // Emit only slow frames so normal one-second updates do not
                // flood Serial or perturb the UI loop being measured.
                if (renderDurationUs >= 8000UL) {
                    Serial.printf("[PERF] Watch Face render=%lu us\\n",
                                  static_cast<unsigned long>(renderDurationUs));
                }
                if (status == FaceRenderStatus::Ok) {
                    lastCustomFaceSecond = cachedTime.tm_sec;
                } else {
                    Serial.printf("[FACE] Custom renderer rejected frame (%u); reverting to built-in face.\n",
                                  static_cast<unsigned int>(status));
                    FaceManager::disableActive();
                    drawAnalogClockFace(cachedTime, static_cast<int>(elapsed), 0, 0);
                    lastCustomFaceSecond = cachedTime.tm_sec;
                }
            }
        } else {
            drawAnalogClockFace(
                cachedTime,
                static_cast<int>(elapsed),
                0,
                0
            );
        }

        lastClockDrawMs = now;
    }

    // -------------------------------------------------
    // WEATHER FACE ANIMATION
    // -------------------------------------------------

    if (currentScreen != SCREEN_WEATHER) {
        weatherFaceInitialized = false;
    } else if (now - lastWeatherFrameMs >= 40) {
        if (!weatherFaceInitialized) {
            // Copy the String-heavy snapshot only once when entering the face.
            // Animation updates use their own captured state and do not need
            // another full WeatherData copy every 40 ms.
            WeatherData weather = getWeatherSnapshot();
            drawWeatherFaceData(weather);
            weatherFaceInitialized = true;
        }

        static const WeatherData unusedAnimationInput{};
        updateWeatherFaceAnimation(unusedAnimationInput);
        lastWeatherFrameMs = now;
    }

    delay(2);
}
