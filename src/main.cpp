
#include <Arduino.h>
#include <WiFi.h>
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

extern void handleBLECommands();

TFT_eSPI tft = TFT_eSPI();

ScreenState currentScreen = SCREEN_CLOCK;
ScreenState screenBeforeMenu = SCREEN_CLOCK;

uint8_t menuIndex = 0;
MenuLevel currentMenuLevel = MENU_MAIN;
uint8_t forecastIndex = 0;

bool weatherDataValid = false;
bool wifiEnabled = true;

extern void drawAnalogClockFace(
    struct tm timeinfo,
    int millisValue,
    int heartRate,
    int steps
);

extern void showBootTerminal();
extern void bootTerminalLog(const char* msg, uint16_t color);

extern void drawMenuScreen(
    int selectedIndex,
    MenuLevel level,
    bool wifiStatus,
    bool wifiConnected
);

extern void drawWeatherFaceData(const WeatherData& w);
extern void updateWeatherFaceAnimation(const WeatherData& w);

static uint32_t lastWeatherFetchMs = 0;
static uint32_t lastWeatherRetryMs = 0;
static uint32_t lastManualWeatherRequestMs = 0;
static uint32_t lastClockDrawMs = 0;
static uint32_t lastMenuFrameMs = 0;
static uint32_t lastWeatherFrameMs = 0;

static bool weatherFaceInitialized = false;
static bool previousWifiConnected = false;

static volatile bool weatherFetchRunning = false;
static volatile bool locationWeatherRefreshPending = false;

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

    // Do not enter sleep while a TLS/weather task is still using the radios.
    Serial.println("[POWER] Waiting for weather task before sleep.");
    while (weatherFetchRunning) delay(20);

    // Preserve cached weather/time in RAM and NVS; shut down radios only.
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    wifiEnabled = false;
    previousWifiConnected = false;

    shutdownBLEForSleep();

    // Put the TFT controller into its own sleep mode as well.
    tft.fillScreen(TFT_BLACK);
    tft.writecommand(0x10); // GC9A01 sleep-in
    delay(30);

    gpio_wakeup_enable(static_cast<gpio_num_t>(BUTTON_PIN), GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    Serial.println("[POWER] Light sleep: Wi-Fi OFF, BLE deinitialized, TFT sleeping.");
    esp_light_sleep_start();
    gpio_wakeup_disable(static_cast<gpio_num_t>(BUTTON_PIN));

    // Restore only the display and input. Keep Wi-Fi/Bluetooth OFF until
    // the user explicitly enables them from the menu.
    tft.writecommand(0x11); // GC9A01 sleep-out
    delay(120);
    tft.writecommand(0x29); // Display ON
    tft.fillScreen(TFT_BLACK);
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

    sendBLENotification(success ? "WEATHER_OK" : "WEATHER_FAILED");
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

    if (weatherFetchRunning) {
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

    lastWeatherFetchMs = millis();

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
    if (weatherFetchRunning) return 1;

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

    if (!wifiEnabled || WiFi.status() != WL_CONNECTED) return 2;
    if (weatherFetchRunning) return 1;

    locationWeatherRefreshPending = false;
    return scheduleWeatherFetch() ? 0 : (weatherFetchRunning ? 1 : 2);
}

// =====================================================
// WIFI CONTROL
// =====================================================

/**
 * @brief Enables or disables the Wi-Fi station interface and updates the application state.
 */
void toggleWiFi() {
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
void setup() {
    Serial.begin(115200);

    delay(250);

    Serial.println("\n[BOOT] ESP32-C3 Smart Watch");
    Serial.printf("[BOOT] Reset reason: %d\n", static_cast<int>(esp_reset_reason()));
    Serial.printf("[BOOT] Heap: free=%u, largest_block=%u, min_free=%u bytes\n",
                  static_cast<unsigned int>(ESP.getFreeHeap()),
                  static_cast<unsigned int>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                  static_cast<unsigned int>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)));

    beginButtonManager();

    Serial.println("[BOOT] Button ready");

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    Serial.println("[BOOT] GC9A01 ready");

    showBootTerminal();
    bootTerminalLog("[BOOT] Reading saved settings", TFT_YELLOW);

    // Restore persistent settings/cache first. Reboot does not erase NVS.
    Preferences prefs;
    String savedSsid;
    String savedPass;
    float savedLat = NAN;
    float savedLon = NAN;
    if (prefs.begin("watch", true)) {
        savedSsid = prefs.getString("ssid", WIFI_SSID);
        savedPass = prefs.getString("pass", WIFI_PASSWORD);
        savedLat = prefs.getFloat("lat", NAN);
        savedLon = prefs.getFloat("lon", NAN);
        prefs.end();
    }
    if (savedSsid.length() == 0) savedSsid = WIFI_SSID;
    if (savedPass.length() == 0) savedPass = WIFI_PASSWORD;
    if (isfinite(savedLat) && isfinite(savedLon)) {
        setWeatherCoordinates(savedLat, savedLon);
    }

    const bool cacheLoaded = loadWeatherCache();
    bootTerminalLog(cacheLoaded ? "[OK] Weather cache restored" : "[INFO] No weather cache", cacheLoaded ? TFT_GREEN : TFT_YELLOW);

    // Stage 1: connect using credentials already stored in NVS.
    bootTerminalLog("[NET] Starting saved Wi-Fi", TFT_YELLOW);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());

    const uint32_t wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
        delay(250);
    }

    const bool wifiConnectedAtBoot = WiFi.status() == WL_CONNECTED;
    wifiEnabled = true;
    if (wifiConnectedAtBoot) {
        bootTerminalLog("[OK] Wi-Fi connected", TFT_GREEN);
        Serial.printf("[WIFI] Boot connected. IP: %s, RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());

        // Stage 2: NTP time synchronization, bounded so boot cannot hang forever.
        bootTerminalLog("[TIME] Detecting timezone + syncing NTP", TFT_YELLOW);
        beginTimeService();
        struct tm bootTime = {};
        bool timeSynced = false;
        const uint32_t timeStart = millis();
        while (millis() - timeStart < 15000) {
            if (getLocalTime(&bootTime, 100)) {
                timeSynced = bootTime.tm_year >= (2024 - 1900);
                if (timeSynced) break;
            }
            delay(150);
        }
        bootTerminalLog(timeSynced ? "[OK] Time synchronized" : "[WARN] NTP unavailable", timeSynced ? TFT_GREEN : TFT_YELLOW);

        // Stage 3: fetch and persist core weather before any lunar TLS request.
        bootTerminalLog("[WEATHER] Fetching forecast", TFT_YELLOW);
        const bool weatherUpdated = fetchHourlyWeather();
        bootTerminalLog(weatherUpdated ? "[OK] Weather saved" : "[WARN] Using weather cache", weatherUpdated ? TFT_GREEN : TFT_YELLOW);
        // If the live request failed but cache exists, retry after entering the UI.
        lastWeatherFetchMs = weatherUpdated
            ? millis()
            : millis() - WEATHER_REFRESH_MS;

        // Stage 4: optional lunar request is a separate, later operation.
        if (weatherUpdated) {
            delay(5000);
            bootTerminalLog("[MOON] Fetching lunar data", TFT_YELLOW);
            const bool lunarUpdated = fetchLunarDetails();
            bootTerminalLog(lunarUpdated ? "[OK] Moon data saved" : "[WARN] Keeping moon cache", lunarUpdated ? TFT_GREEN : TFT_YELLOW);
        } else {
            bootTerminalLog("[MOON] Skipped; no fresh weather", TFT_YELLOW);
        }
    } else {
        bootTerminalLog("[WARN] Wi-Fi unavailable", TFT_YELLOW);
        Serial.println("[WIFI] Boot connection timed out; starting offline with cache.");
    }

    // Initialize BLE only after network/time/weather work is complete, then
    // stop advertising to leave the device in a low-radio idle state.
    bootTerminalLog("[BLE] Initializing Bluetooth", TFT_YELLOW);
    beginBLE();
    if (isBLEEnabled()) {
        toggleBLE();
    }
    bootTerminalLog("[OK] Bluetooth initialized; advertising OFF", TFT_GREEN);

    // Wi-Fi is no longer needed after the boot sync stages. Keep credentials
    // and weather cache in NVS; only power down the radio.
    bootTerminalLog("[NET] Turning Wi-Fi OFF", TFT_YELLOW);
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    wifiEnabled = false;
    previousWifiConnected = false;
    lastWifiRetryMs = millis();
    lastWifiStatusLogMs = 0;
    Serial.println("[WIFI] Boot sync complete; radio disabled.");

    // The application UI is released only after the ordered boot sequence.
    bootTerminalLog("[SYS] Starting clock interface", TFT_GREEN);
    delay(350);
    drawCurrentScreen(true);
}

// =====================================================
// MAIN LOOP
// =====================================================

/**
 * @brief Implements the loop operation.
 */
void loop() {
    const uint32_t now = millis();

    // -------------------------------------------------
    // BUTTON INPUT
    // -------------------------------------------------

    updateButtonManager();
    handleBLECommands();
    if (lightSleepRequested) {
        enterLightSleep();
        return;
    }

    // -------------------------------------------------
    // WIFI CONNECTION MONITOR + DIAGNOSTICS
    // -------------------------------------------------

    const wl_status_t wifiStatus = WiFi.status();

    if (wifiEnabled && wifiStatus != WL_CONNECTED) {
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

    if (wifiConnected && !previousWifiConnected) {
        Serial.printf(
            "[WIFI] Connected. IP: %s, RSSI: %d dBm\n",
            WiFi.localIP().toString().c_str(),
            WiFi.RSSI()
        );

        // Refresh IP-based timezone/location and restart NTP after Wi-Fi reconnect.
        // This also adapts the clock and forecast location when travelling.
        beginTimeService();
        lastWeatherFetchMs = now - WEATHER_REFRESH_MS;
        lastWeatherRetryMs = 0;
    }

    if (!wifiConnected && previousWifiConnected) {
        Serial.println("[WIFI] Connection lost.");
    }

    previousWifiConnected = wifiConnected;

    // A GPS update received during an active request is refreshed as soon as
    // that request releases its TLS/JSON resources. Keep it pending while offline.
    if (locationWeatherRefreshPending && wifiConnected && !weatherFetchRunning) {
        locationWeatherRefreshPending = false;
        if (!scheduleWeatherFetch()) {
            locationWeatherRefreshPending = true;
        }
    }

    // -------------------------------------------------
    // WEATHER SNAPSHOT
    // -------------------------------------------------

    WeatherData weatherSnapshot =
        getWeatherSnapshot();

    weatherDataValid = weatherSnapshot.valid;

    // -------------------------------------------------
    // WEATHER RETRY
    // -------------------------------------------------

    if (
        wifiConnected &&
        !weatherSnapshot.valid &&
        !weatherFetchRunning &&
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
        weatherSnapshot.valid &&
        !weatherFetchRunning &&
        now - lastWeatherFetchMs >= WEATHER_REFRESH_MS
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
        WeatherData weather =
            getWeatherSnapshot();

        if (!weatherFaceInitialized) {
            // Render even without a valid forecast so Wi-Fi status and the
            // next-update countdown remain visible while offline/loading.
            drawWeatherFaceData(weather);
            weatherFaceInitialized = true;
        }

        if (weatherFaceInitialized) {
            updateWeatherFaceAnimation(weather);
        }

        lastWeatherFrameMs = now;
    }

    delay(2);
}
