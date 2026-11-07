
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "app/app_state.h"
#include "app/app_controller.h"
#include "input/button_manager.h"
#include "services/time_service.h"
#include "services/weather_service.h"
#include "services/ble_service.h"
#include "system/diagnostics.h"
#include "display/display_manager.h"

extern void handleBLECommands();


ScreenState currentScreen = SCREEN_CLOCK;
ScreenState screenBeforeMenu = SCREEN_CLOCK;

uint8_t menuIndex = 0;
MenuLevel currentMenuLevel = MENU_MAIN;
uint8_t forecastIndex = 0;

volatile bool weatherDataValid = false;
bool wifiEnabled = true;

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

static uint32_t lastWeatherFetchMs = 0;      // Last successful forecast fetch.
static uint32_t lastWeatherAttemptMs = 0;   // Last scheduled attempt; throttles failures.
static uint32_t lastWeatherRetryMs = 0;
static uint32_t lastManualWeatherRequestMs = 0;
static uint32_t lastClockDrawMs = 0;
static uint32_t lastMenuFrameMs = 0;
static uint32_t lastWeatherFrameMs = 0;

static volatile bool weatherFaceInitialized = false;
static bool previousWifiConnected = false;

static volatile bool weatherFetchRunning = false;
static volatile bool wifiScanRunning = false;
static bool wifiScanRestoreOff = false;
static volatile int8_t weatherNotifyPending = -1;
static volatile bool locationWeatherRefreshPending = false;
static volatile bool bootNetworkRunning = false;
static volatile bool bootNetworkNeedsBle = false;
static bool bootUiReady = false;
static uint32_t lastLocationWeatherRetryMs = 0;

// Countdown for the Weather face. Negative values are status codes:
// -1 = Wi-Fi intentionally disabled, -2 = connecting, -3 = fetch in progress.
/**
 * @brief Implements the get weather refresh countdown seconds operation.

 */
int32_t getWeatherRefreshCountdownSeconds() {
    if (!wifiEnabled) return -1;
    if (WiFi.status() != WL_CONNECTED) return -2;
    if (weatherFetchRunning) return -3;

    const uint32_t elapsed = millis() - lastWeatherFetchMs;
    if (elapsed >= WEATHER_REFRESH_MS) return 0;

    const uint32_t remainingMs = WEATHER_REFRESH_MS - elapsed;
    return static_cast<int32_t>((remainingMs + 999UL) / 1000UL);
}


static constexpr uint32_t WEATHER_RETRY_MS = 15000;
static constexpr uint32_t WEATHER_MANUAL_COOLDOWN_MS = 10000;
static constexpr uint32_t WIFI_RETRY_MS = 15000;
static constexpr uint32_t WIFI_STATUS_LOG_MS = 5000;

static uint32_t lastWifiRetryMs = 0;
static uint32_t lastWifiStatusLogMs = 0;
static bool lightSleepRequested = false;
static uint8_t autoSleepTimeoutSeconds = 15; // 0 disables automatic sleep.
static uint32_t lastUserActivityMs = 0;
static uint32_t lastButtonActivitySeenMs = 0;
static bool previousBleConnected = false;

void setAutoSleepTimeoutSeconds(uint8_t seconds) {
    if (!(seconds == 0 || seconds == 10 || seconds == 12 ||
          seconds == 15 || seconds == 18 || seconds == 20)) {
        Serial.printf("[POWER] Rejected invalid Auto Sleep value: %u seconds.\n", seconds);
        return;
    }
    autoSleepTimeoutSeconds = seconds;
    lastUserActivityMs = millis();
    Serial.printf("[POWER] Auto Sleep timeout applied: %u seconds.\n", autoSleepTimeoutSeconds);
}

uint8_t getAutoSleepTimeoutSeconds() { return autoSleepTimeoutSeconds; }

/**
 * @brief Schedules entry into Light Sleep mode from the main application loop.
 */
void requestLightSleep() { lightSleepRequested = true; }

/**
 * @brief Powers down Wi-Fi, Bluetooth, and the display before entering Light Sleep; restores the display and input after wake-up.
 */
static void enterLightSleep() {
    lightSleepRequested = false;
    while (digitalRead(BUTTON_PIN) == LOW) delay(10);

    // The main loop defers sleep until background radio work has completed.
    // Preserve cached weather/time in RAM and NVS; shut down radios only.
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    wifiEnabled = false;
    previousWifiConnected = false;

    shutdownBLEForSleep();

    // Keep the GC9A01 controller awake and simply paint the panel black.
    // This isolates ESP32-C3 Light Sleep from TFT sleep-state restoration:
    // the first BOOT wake should be followed by a normal redraw.
    tft.fillScreen(TFT_BLACK);

    // GPIO9 is the BOOT button. Keep its pull-up enabled and wake on LOW.
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    gpio_wakeup_enable(static_cast<gpio_num_t>(BUTTON_PIN), GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    Serial.println("[POWER] Light sleep: Wi-Fi OFF, BLE deinitialized, TFT panel blanked.");
    esp_light_sleep_start();
    gpio_wakeup_disable(static_cast<gpio_num_t>(BUTTON_PIN));

    const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
    Serial.printf("[POWER] Wake cause: %d\\n", static_cast<int>(wakeCause));

    // Wait for the wake press to be released before resetting button state.
    while (digitalRead(BUTTON_PIN) == LOW) {
        delay(10);
    }
    delay(BUTTON_DEBOUNCE_MS);

    // The TFT was intentionally kept awake (black screen) during MCU sleep,
    // so no panel reset or sleep-out sequence is needed. Redraw immediately.

    // Radios remain OFF until explicitly enabled from the menu.
    beginButtonManager();
    drawCurrentScreen(true);
    Serial.println("[POWER] Wake complete; radios remain OFF.");
}

/**
 * @brief Converts a Wi-Fi connection state into a readable diagnostic label.
 * @param status Input or state used by this operation.
 */
static const char* wifiStatusName(wl_status_t status) {
    switch (status) {
        case WL_CONNECTED: return "CONNECTED";
        case WL_NO_SSID_AVAIL: return "NO_SSID_AVAILABLE";
        case WL_CONNECT_FAILED: return "CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED: return "DISCONNECTED";
        case WL_IDLE_STATUS: return "IDLE";
        default: return "UNKNOWN";
    }
}

// =====================================================
// WEATHER TASK
// =====================================================

/**
 * @brief Runs the weather request in a background task and optionally retrieves lunar details afterward.
 * @param parameter Input or state used by this operation.
 */
static void weatherFetchTask(void *parameter) {
    (void)parameter;

    Serial.println("[WEATHER] Background fetch started.");

    const bool success = fetchHourlyWeather();
    if (success) {
        lastWeatherFetchMs = millis(); // Advance the refresh countdown only after valid data is committed.
        weatherDataValid = true;
        weatherFaceInitialized = false; // redraw fresh data on the UI thread
    }

    // BLE host operations stay on the application-loop thread. The worker
    // publishes only a small completion code for loop() to notify later.
    weatherNotifyPending = success ? 1 : 0;
    Serial.println(success ? "[WEATHER] Weather stage finished." : "[WEATHER] Weather stage failed.");

    // Lunar TLS runs only after Open-Meteo has completed and released its
    // response/JSON allocations. It is optional and cannot invalidate weather.
    if (success && wifiEnabled && WiFi.status() == WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        Serial.println("[BOOT] Starting deferred lunar stage.");
        fetchLunarDetails();
    }

    weatherFetchRunning = false;

    vTaskDelete(nullptr);
}

// =====================================================
// SCHEDULE WEATHER FETCH
// =====================================================

/**
 * @brief Starts one background weather update when Wi-Fi is connected and no update is already running.
 */
static bool scheduleWeatherFetch() {
    // Never start an automatic/manual request while Wi-Fi is disabled.
    if (!wifiEnabled) {
        Serial.println("[WEATHER] Fetch skipped: Wi-Fi is OFF.");
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    if (weatherFetchRunning || wifiScanRunning || bootNetworkRunning || bootNetworkNeedsBle) {
        return false;
    }

    weatherFetchRunning = true;

    BaseType_t created = xTaskCreate(
        weatherFetchTask,
        "weatherFetch",
        8192,
        nullptr,
        1,
        nullptr
    );

    if (created != pdPASS) {
        weatherFetchRunning = false;

        Serial.println(
            "[WEATHER] Could not create background task."
        );

        return false;
    }

    lastWeatherAttemptMs = millis();

    Serial.println("[WEATHER] Fetch scheduled.");

    return true;
}

// Manual BLE requests use the same single-flight scheduler as automatic updates.
// Return: 0=scheduled, 1=already running, 2=Wi-Fi unavailable, 3=cooldown.
/**
 * @brief Validates a manual weather request and applies the single-flight and cooldown rules.
 */
uint8_t requestWeatherFetch() {
    if (!wifiEnabled || WiFi.status() != WL_CONNECTED) return 2;
    if (weatherFetchRunning || wifiScanRunning) return 1;

    const uint32_t now = millis();
    if (lastManualWeatherRequestMs != 0 &&
        now - lastManualWeatherRequestMs < WEATHER_MANUAL_COOLDOWN_MS) {
        return 3;
    }

    if (!scheduleWeatherFetch()) {
        return weatherFetchRunning ? 1 : 2;
    }

    lastManualWeatherRequestMs = now;
    return 0;
}

/**
 * @brief Requests an immediate weather/timezone refresh after a new phone GPS fix.
 * If another fetch is running, defer one follow-up request until it completes.
 */
uint8_t requestWeatherFetchForLocation() {
    locationWeatherRefreshPending = true;
    lastLocationWeatherRetryMs = 0;

    if (!wifiEnabled || WiFi.status() != WL_CONNECTED) return 2;
    if (weatherFetchRunning || wifiScanRunning) return 1;

    if (scheduleWeatherFetch()) {
        locationWeatherRefreshPending = false;
        return 0;
    }
    // Keep the request pending so the main loop can retry after resources recover.
    return 2;
}

// =====================================================
// ASYNCHRONOUS WI-FI SCAN
// =====================================================

// Return: 0=started, 1=already busy, 2=network work has priority, 3=start failed.
uint8_t requestWifiScan() {
    if (wifiScanRunning) return 1;
    if (weatherFetchRunning || bootNetworkRunning || bootNetworkNeedsBle) return 2;

    wifiScanRestoreOff = !wifiEnabled;
    if (wifiScanRestoreOff) {
        WiFi.mode(WIFI_STA);
    }

    // Async mode returns immediately; the main loop polls scanComplete().
    const int result = WiFi.scanNetworks(true, true);
    if (result == WIFI_SCAN_FAILED) {
        WiFi.scanDelete();
        if (wifiScanRestoreOff) WiFi.mode(WIFI_OFF);
        wifiScanRestoreOff = false;
        Serial.println("[WIFI] Could not start asynchronous scan.");
        return 3;
    }

    wifiScanRunning = true;
    Serial.println("[WIFI] Asynchronous scan started.");
    return 0;
}

static void serviceWeatherNotification() {
    const int8_t result = weatherNotifyPending;
    if (result < 0) return;
    weatherNotifyPending = -1;
    sendBLENotification(result ? "WEATHER_OK" : "WEATHER_FAILED");
}

bool isWifiScanBusy() {
    return wifiScanRunning;
}

static void serviceWifiScan() {
    if (!wifiScanRunning) return;

    const int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) return;

    if (count == WIFI_SCAN_FAILED) {
        Serial.println("[WIFI] Asynchronous scan failed.");
        WiFi.scanDelete();
        if (wifiScanRestoreOff) WiFi.mode(WIFI_OFF);
        wifiScanRestoreOff = false;
        wifiScanRunning = false;
        sendBLENotification("WIFI_SCAN_ERROR");
        return;
    }

    JsonDocument doc;
    JsonArray list = doc.to<JsonArray>();
    // Keep the BLE response bounded. Twelve SSIDs fit comfortably within
    // the small C3 heap and avoid an unbounded notification burst.
    const int returned = count < 12 ? count : 12;
    for (int i = 0; i < returned; ++i) {
        list.add(WiFi.SSID(i));
    }

    String json;
    serializeJson(doc, json);
    Serial.printf("[WIFI] Scan complete: %d found, %d returned, %u JSON bytes.\n",
                  count, returned, static_cast<unsigned int>(json.length()));

    WiFi.scanDelete();
    if (wifiScanRestoreOff) WiFi.mode(WIFI_OFF);
    wifiScanRestoreOff = false;
    wifiScanRunning = false;

    sendBLENotification(json);
}

// =====================================================
// WIFI CONTROL
// =====================================================

/**
 * @brief Enables or disables the Wi-Fi station interface and updates the application state.
 */
void toggleWiFi() {
    if (wifiScanRunning) {
        Serial.println("[WIFI] Toggle ignored while scan is active.");
        return;
    }
    if (wifiEnabled) {
        WiFi.disconnect(false, false);
        WiFi.mode(WIFI_OFF);

        wifiEnabled = false;
        previousWifiConnected = false;

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

        lastWifiRetryMs = millis();
        lastWifiStatusLogMs = 0;
        beginTimeService();
        lastWeatherFetchMs = 0;
        lastWeatherAttemptMs = 0;
        lastWeatherRetryMs = 0;

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
 * @brief Performs Wi-Fi, NTP, weather, and lunar work sequentially.
 * The clock UI is already running while this low-priority task works.
 */
static void bootNetworkTask(void *parameter) {
    (void)parameter;

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

    bootTerminalLog("[BOOT] Connecting to Wi-Fi", TFT_YELLOW);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());

    const uint32_t wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - wifiStart < 15000UL) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    if (WiFi.status() == WL_CONNECTED) {
        char wifiMsg[48];
        snprintf(wifiMsg, sizeof(wifiMsg), "[OK] Wi-Fi connected (%d dBm)", WiFi.RSSI());
        bootTerminalLog(wifiMsg, TFT_GREEN);

        // IP fallback and NTP setup happen away from the UI loop.
        bootTerminalLog("[TIME] Synchronizing NTP", TFT_YELLOW);
        beginTimeService();

        struct tm syncedTime = {};
        bool timeSynced = false;
        const uint32_t timeStart = millis();
        while (millis() - timeStart < 15000UL) {
            if (getLocalTime(&syncedTime, 100) &&
                syncedTime.tm_year >= (2024 - 1900)) {
                timeSynced = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(150));
        }

        if (timeSynced) {
            saveTimeCheckpoint();
            bootTerminalLog("[OK] NTP synchronized", TFT_GREEN);
        } else {
            bootTerminalLog("[INFO] NTP unavailable; keeping saved time", TFT_YELLOW);
        }

        // Keep TLS stages strictly sequential and prevent the normal loop
        // from scheduling another weather request during this boot phase.
        weatherFetchRunning = true;
        bootTerminalLog("[WEATHER] Fetching forecast", TFT_YELLOW);
        const bool weatherUpdated = fetchHourlyWeather();
        if (weatherUpdated) {
            lastWeatherFetchMs = millis();
            weatherDataValid = true;
            weatherFaceInitialized = false;
        }
        bootTerminalLog(weatherUpdated
            ? "[OK] Weather updated and cached"
            : "[INFO] Weather unavailable; cache retained",
            weatherUpdated ? TFT_GREEN : TFT_YELLOW);

        if (weatherUpdated && WiFi.status() == WL_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            bootTerminalLog("[LUNAR] Fetching lunar details", TFT_YELLOW);
            fetchLunarDetails();
            bootTerminalLog("[OK] Lunar stage complete", TFT_GREEN);
        }
        weatherFetchRunning = false;
    } else {
        bootTerminalLog("[INFO] Wi-Fi unavailable; using saved data", TFT_YELLOW);
    }

    // BLE is initialized by loop() after this task exits, so its allocation
    // cannot overlap either HTTPS/TLS request.
    bootTerminalLog("[OK] Network startup stage complete", TFT_GREEN);
    bootNetworkRunning = false;
    bootNetworkNeedsBle = true;
    vTaskDelete(nullptr);
}

/**
 * @brief Restores persistent state, keeps the boot terminal visible while
 * startup services run, then hands the display to the clock UI.
 */
void setup() {
    Serial.begin(115200);
    delay(250);

    Serial.println("\n[BOOT] ESP32-C3 Smart Watch");
    Serial.printf("[BOOT] Reset reason: %d\n", static_cast<int>(esp_reset_reason()));
    SystemDiagnostics::begin();

    beginButtonManager();
    beginDisplay(0);

    showBootTerminal();
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
    bootTerminalLog(cacheLoaded
        ? "[OK] Weather cache restored"
        : "[INFO] No weather cache", cacheLoaded ? TFT_GREEN : TFT_YELLOW);

    // Keep the terminal visible until all startup stages have finished.
    bootTerminalLog("[BOOT] Starting startup services", TFT_GREEN);

    wifiEnabled = true;
    bootNetworkRunning = true;
    const BaseType_t created = xTaskCreate(
        bootNetworkTask,
        "bootNetwork",
        8192,
        nullptr,
        1,
        nullptr
    );

    if (created != pdPASS) {
        bootNetworkRunning = false;
        bootNetworkNeedsBle = true;
        bootTerminalLog("[WARN] Network task unavailable; skipping Wi-Fi", TFT_YELLOW);
        WiFi.disconnect(false, false);
        WiFi.mode(WIFI_OFF);
        wifiEnabled = false;
        Serial.println("[BOOT] Could not create network task; staying offline.");
    }
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
        if (bootNetworkNeedsBle) {
            bootNetworkNeedsBle = false;
            bootTerminalLog("[BLE] Initializing Bluetooth", TFT_YELLOW);
            beginBLE();
            if (isBLEEnabled()) {
                toggleBLE();
            }
            SystemDiagnostics::logMemory("after BLE init");
            WiFi.disconnect(false, false);
            WiFi.mode(WIFI_OFF);
            wifiEnabled = false;
            previousWifiConnected = false;
            lastWifiRetryMs = millis();
            lastWifiStatusLogMs = 0;

            bootTerminalLog("[OK] Bluetooth initialized; radios idle", TFT_GREEN);
            bootTerminalLog("[BOOT] Startup complete", TFT_GREEN);
            delay(350);

            tft.fillScreen(TFT_BLACK);
            stopBootTerminalDisplay();
            drawCurrentScreen(true);
            bootUiReady = true;
            lastUserActivityMs = millis();
            lastButtonActivitySeenMs = getLastButtonActivityMs();
            previousBleConnected = isBLEConnected();
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

    // Count real physical button activity, not periodic UI redraws.
    const uint32_t buttonActivityMs = getLastButtonActivityMs();
    if (buttonActivityMs != lastButtonActivitySeenMs) {
        lastButtonActivitySeenMs = buttonActivityMs;
        lastUserActivityMs = now;
    }

    // A connection/disconnection is a meaningful state transition. Start a
    // fresh idle window after disconnect rather than sleeping immediately.
    const bool bleConnectedNow = isBLEConnected();
    if (bleConnectedNow != previousBleConnected) {
        previousBleConnected = bleConnectedNow;
        lastUserActivityMs = now;
    }

    handleBLECommands();
    serviceBLE();
    serviceWeatherNotification();
    serviceWifiScan();

    // Automatic sleep is deliberately disabled during an active BLE session.
    // Manual Sleep from the menu still bypasses this connection guard.
    const bool backgroundWorkActive =
        bootNetworkRunning || bootNetworkNeedsBle || weatherFetchRunning || wifiScanRunning;
    if (!lightSleepRequested &&
        !isBLEConnected() &&
        !backgroundWorkActive &&
        lastUserActivityMs != 0 &&
        currentScreen == SCREEN_CLOCK &&
        autoSleepTimeoutSeconds > 0 &&
        (now - lastUserActivityMs) >= static_cast<uint32_t>(autoSleepTimeoutSeconds) * 1000UL) {
        Serial.printf("[POWER] Auto Light Sleep: %u seconds without user activity.\n",
                      autoSleepTimeoutSeconds);
        lightSleepRequested = true;
    }

    if (lightSleepRequested) {
        // Keep the UI loop responsive while HTTPS/TLS work finishes.
        // Sleep is entered on a later loop iteration once radio tasks are idle.
        if (backgroundWorkActive) {
            delay(2);
            return;
        }
        enterLightSleep();
        return;
    }

    // -------------------------------------------------
    // WIFI CONNECTION MONITOR + DIAGNOSTICS
    // -------------------------------------------------

    const wl_status_t wifiStatus = WiFi.status();

    if (!wifiScanRunning && wifiEnabled && !bootNetworkRunning && !bootNetworkNeedsBle && wifiStatus != WL_CONNECTED) {
        if (now - lastWifiRetryMs >= WIFI_RETRY_MS) {
            Serial.printf(
                "[WIFI] Still not connected (status=%s, code=%d); retrying.\n",
                wifiStatusName(wifiStatus),
                static_cast<int>(wifiStatus)
            );

            WiFi.reconnect();
            lastWifiRetryMs = now;
        }

        if (
            lastWifiStatusLogMs == 0 ||
            now - lastWifiStatusLogMs >= WIFI_STATUS_LOG_MS
        ) {
            Serial.printf(
                "[WIFI] Status: %s (code=%d).\n",
                wifiStatusName(wifiStatus),
                static_cast<int>(wifiStatus)
            );
            lastWifiStatusLogMs = now;
        }
    }

    const bool wifiConnected =
        wifiEnabled &&
        wifiStatus == WL_CONNECTED;

    if (wifiConnected && !previousWifiConnected && !bootNetworkRunning && !bootNetworkNeedsBle) {
        Serial.printf(
            "[WIFI] Connected. IP: %s, RSSI: %d dBm\n",
            WiFi.localIP().toString().c_str(),
            WiFi.RSSI()
        );

        // Refresh IP-based timezone/location and restart NTP after Wi-Fi reconnect.
        // This also adapts the clock and forecast location when travelling.
        beginTimeService();
        lastWeatherFetchMs = now - WEATHER_REFRESH_MS;
        lastWeatherAttemptMs = 0;
        lastWeatherRetryMs = 0;
    }

    if (!wifiConnected && previousWifiConnected) {
        Serial.println("[WIFI] Connection lost.");
    }

    previousWifiConnected = wifiConnected;

    // A GPS update received during an active request is refreshed as soon as
    // that request releases its TLS/JSON resources. Keep it pending while offline.
    if (locationWeatherRefreshPending && wifiConnected && !weatherFetchRunning && !wifiScanRunning &&
        (lastLocationWeatherRetryMs == 0 ||
         now - lastLocationWeatherRetryMs >= WEATHER_RETRY_MS)) {
        lastLocationWeatherRetryMs = now;
        if (scheduleWeatherFetch()) {
            locationWeatherRefreshPending = false;
        }
    }

    // -------------------------------------------------
    // WEATHER SNAPSHOT
    // -------------------------------------------------

    const bool hasWeatherData = weatherDataValid;

    // -------------------------------------------------
    // WEATHER RETRY
    // -------------------------------------------------

    if (
        wifiConnected &&
        !bootNetworkRunning &&
        !bootNetworkNeedsBle &&
        !hasWeatherData &&
        !weatherFetchRunning &&
        !wifiScanRunning &&
        (
            lastWeatherRetryMs == 0 ||
            now - lastWeatherRetryMs >= WEATHER_RETRY_MS
        )
    ) {
        if (scheduleWeatherFetch()) {
            lastWeatherRetryMs = now;

            Serial.println(
                "[WEATHER] Retrying weather fetch..."
            );
        }
    }

    // -------------------------------------------------
    // PERIODIC WEATHER UPDATE
    // -------------------------------------------------

    if (
        wifiConnected &&
        !bootNetworkRunning &&
        !bootNetworkNeedsBle &&
        hasWeatherData &&
        !weatherFetchRunning &&
        !wifiScanRunning &&
        now - lastWeatherFetchMs >= WEATHER_REFRESH_MS &&
        (lastWeatherAttemptMs == 0 || now - lastWeatherAttemptMs >= WEATHER_RETRY_MS)
    ) {
        scheduleWeatherFetch();
    }

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

        drawAnalogClockFace(
            cachedTime,
            static_cast<int>(elapsed),
            0,
            0
        );

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
