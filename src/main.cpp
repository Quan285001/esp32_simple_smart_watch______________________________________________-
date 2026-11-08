
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_system.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "app/app_state.h"
#include "app/app_controller.h"
#include "app/boot_coordinator.h"
#include "input/button_manager.h"
#include "services/time_service.h"
#include "services/weather_service.h"
#include "services/weather_coordinator.h"
#include "services/wifi_service.h"
#include "services/ble_service.h"
#include "system/diagnostics.h"
#include "display/display_manager.h"
#include "storage/storage_manager.h"
#include "storage/face_storage.h"
#include "watchface/face_manager.h"
#include "communication/transfer_manager.h"
#include "system/system_manager.h"

extern void handleBLECommands();


extern void drawAnalogClockFace(
    struct tm timeinfo,
    int millisValue,
    int heartRate,
    int steps
);

extern void showBootTerminal();
extern void stopBootTerminalDisplay();
extern void bootTerminalLog(const char* msg, uint16_t color);

extern void drawMenuScreen(
    int selectedIndex,
    MenuLevel level,
    bool wifiStatus,
    bool wifiConnected
);

extern void drawWeatherFaceData(const WeatherData& w);
extern void updateWeatherFaceAnimation(const WeatherData& w);

static uint32_t lastClockDrawMs = 0;
static uint32_t lastMenuFrameMs = 0;
static uint32_t lastWeatherFrameMs = 0;

static volatile bool weatherFaceInitialized = false;


// Prevent a new bulk face transfer from overlapping TLS/weather/Wi-Fi scan work.
bool canStartWfsTransfer() {
    return WeatherCoordinator::canStartTransfer(
        WifiService::isScanBusy(),
        BootCoordinator::isBusy());
}
static bool bootUiReady = false;

int32_t getWeatherRefreshCountdownSeconds() {
    return WeatherCoordinator::refreshCountdownSeconds(
        wifiEnabled, WiFi.status() == WL_CONNECTED);
}

void setAutoSleepTimeoutSeconds(uint8_t seconds) {
    SystemManager::setAutoSleepTimeoutSeconds(seconds);
}

uint8_t getAutoSleepTimeoutSeconds() {
    return SystemManager::getAutoSleepTimeoutSeconds();
}

/**
 * @brief Schedules entry into Light Sleep mode from the main application loop.
 */
void requestLightSleep() { SystemManager::requestSleep(); }

uint8_t requestWeatherFetch() {
    return WeatherCoordinator::requestManual(
        wifiEnabled, WiFi.status() == WL_CONNECTED,
        WifiService::isScanBusy(), bootNetworkRunning || bootNetworkNeedsBle,
        TransferManager::isBusy());
}

uint8_t requestWeatherFetchForLocation() {
    return WeatherCoordinator::requestForLocation(
        wifiEnabled, WiFi.status() == WL_CONNECTED,
        WifiService::isScanBusy(), bootNetworkRunning || bootNetworkNeedsBle,
        TransferManager::isBusy());
}


// =====================================================
// ASYNCHRONOUS WI-FI SCAN
// =====================================================

// Return: 0=started, 1=already busy, 2=network work has priority, 3=start failed.
uint8_t requestWifiScan() {
    return WifiService::requestScan(
        WeatherCoordinator::isFetchRunning(),
        bootNetworkRunning || bootNetworkNeedsBle,
        TransferManager::isBusy());
}

bool isWifiScanBusy() {
    return WifiService::isScanBusy();
}

// WIFI CONTROL
// =====================================================

/**
 * @brief Enables or disables the Wi-Fi station interface and updates the application state.
 */
void toggleWiFi() {
    if (TransferManager::isBusy()) {
        Serial.println("[WIFI] Toggle ignored while a face transfer is active.");
        return;
    }
    if (WifiService::isScanBusy()) {
        Serial.println("[WIFI] Toggle ignored while scan is active.");
        return;
    }
    if (wifiEnabled) {
        WiFi.disconnect(false, false);
        WiFi.mode(WIFI_OFF);

        wifiEnabled = false;
        WifiService::resetConnectionMonitor(millis());

        Serial.println(
            "[WIFI] Disabled; cached weather retained."
        );
    } else {
        // Reuse credentials saved through BLE instead of always falling back
        // to compile-time defaults.
        Preferences prefs;
        String savedSsid = WIFI_SSID;
        String savedPass = WIFI_PASSWORD;
        if (prefs.begin("watch", true)) {
            savedSsid = prefs.getString("ssid", WIFI_SSID);
            savedPass = prefs.getString("pass", WIFI_PASSWORD);
            prefs.end();
        }
        if (savedSsid.length() == 0) savedSsid = WIFI_SSID;
        if (savedPass.length() == 0) savedPass = WIFI_PASSWORD;

        wifiEnabled = true;
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.begin(savedSsid.c_str(), savedPass.c_str());

        WifiService::resetConnectionMonitor(millis());
        beginTimeService();
        WeatherCoordinator::onWifiEnabled();

        Serial.println("[WIFI] Enabled; reconnecting with saved credentials.");
    }
}

// =====================================================
// SETUP
// =====================================================

/**
 * @brief Implements the setup operation.
 */
// =====================================================
// ORDERED BACKGROUND BOOT
// =====================================================

/**
 * @brief Restores persistent state, keeps the boot terminal visible while
 * startup services run, then hands the display to the clock UI.
 */
void setup() {
    SystemManager::begin();
    Serial.begin(115200);
    delay(250);

    Serial.println("\n[BOOT] ESP32-C3 Smart Watch");
    Serial.printf("[BOOT] Reset reason: %d\n", static_cast<int>(esp_reset_reason()));
    SystemDiagnostics::begin();

    beginButtonManager();
    beginDisplay(0);

    showBootTerminal();
    StorageManager::begin();
    FaceStorage::begin();
    FaceManager::begin();
    bootTerminalLog("[BOOT] Restoring saved state", TFT_YELLOW);

    Preferences prefs;
    float savedLat = NAN;
    float savedLon = NAN;
    if (prefs.begin("watch", true)) {
        savedLat = prefs.getFloat("lat", NAN);
        savedLon = prefs.getFloat("lon", NAN);
        const uint8_t savedAutoSleep = prefs.getUChar("autoSleepSec", 15);
        prefs.end();
        setAutoSleepTimeoutSeconds(savedAutoSleep);
    }
    if (isfinite(savedLat) && isfinite(savedLon)) {
        setWeatherCoordinates(savedLat, savedLon);
    }

    const bool timeRestored = restoreTimeFromNvs();
    bootTerminalLog(timeRestored
        ? "[OK] Saved clock restored"
        : "[INFO] Waiting for time sync", timeRestored ? TFT_GREEN : TFT_YELLOW);

    const bool cacheLoaded = loadWeatherCache();
    weatherDataValid = cacheLoaded;
    WeatherCoordinator::begin();
    bootTerminalLog(cacheLoaded
        ? "[OK] Weather cache restored"
        : "[INFO] No weather cache", cacheLoaded ? TFT_GREEN : TFT_YELLOW);

    // Keep the terminal visible until all startup stages have finished.
    bootTerminalLog("[BOOT] Starting startup services", TFT_GREEN);

    BootCoordinator::begin();
}

// =====================================================
// MAIN LOOP
// =====================================================

/**
 * @brief Implements the loop operation.
 */
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
        bootNetworkRunning || bootNetworkNeedsBle || WeatherCoordinator::isFetchRunning() || WifiService::isScanBusy();
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
            now, wifiEnabled, bootNetworkRunning || bootNetworkNeedsBle);
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
        WifiService::isScanBusy(), bootNetworkRunning || bootNetworkNeedsBle,
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
                const FaceRenderStatus status = FaceManager::renderActiveDynamic(cachedTime, getWeatherSnapshot());
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
