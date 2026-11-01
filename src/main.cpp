//main.cpp

#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>

#include "config.h"
#include "app/app_state.h"
#include "app/app_controller.h"
#include "input/button_manager.h"
#include "services/time_service.h"
#include "services/weather_service.h"

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

extern void drawMenuScreen(
    int selectedIndex,
    MenuLevel level,
    bool wifiStatus,
    bool wifiConnected
);

static uint32_t lastWeatherFetchMs = 0;
static uint32_t lastClockDrawMs = 0;
static uint32_t lastMenuFrameMs = 0;

static bool initialWeatherAttempted = false;
static bool previousWifiConnected = false;

static volatile bool weatherFetchRunning = false;

// =====================================================
// WEATHER TASK
// =====================================================

static void weatherFetchTask(void *parameter) {
    (void)parameter;

    Serial.println("[WEATHER] Background fetch started.");

    fetchHourlyWeather();

    Serial.println("[WEATHER] Background fetch finished.");

    weatherFetchRunning = false;

    vTaskDelete(nullptr);
}

// =====================================================
// SCHEDULE WEATHER FETCH
// =====================================================

static bool scheduleWeatherFetch() {

    if (!wifiEnabled) {
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

// =====================================================
// WIFI CONTROL
// =====================================================

void toggleWiFi() {

    if (wifiEnabled) {

        WiFi.disconnect(false, false);
        WiFi.mode(WIFI_OFF);

        wifiEnabled = false;

        initialWeatherAttempted = false;
        previousWifiConnected = false;

        Serial.println(
            "[WIFI] Disabled; cached weather retained."
        );

    } else {

        wifiEnabled = true;

        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);

        WiFi.begin(
            WIFI_SSID,
            WIFI_PASSWORD
        );

        beginTimeService();

        initialWeatherAttempted = false;
        lastWeatherFetchMs = 0;

        Serial.println(
            "[WIFI] Enabled; reconnecting."
        );
    }
}

// =====================================================
// SETUP
// =====================================================

void setup() {

    Serial.begin(115200);

    delay(250);

    Serial.println("\n[BOOT] ESP32-C3 Smart Watch");

    // Button
    beginButtonManager();

    Serial.println("[BOOT] Button ready");

    // Display
    tft.init();

    tft.setRotation(0);

    tft.fillScreen(TFT_BLACK);

    Serial.println("[BOOT] GC9A01 ready");

    // Boot animation
    showBootTerminal();

    drawCurrentScreen(true);

    // WiFi
    WiFi.mode(WIFI_STA);

    WiFi.setAutoReconnect(true);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    beginTimeService();

    Serial.println(
        "[WIFI] Connecting in background."
    );
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

    const uint32_t now = millis();

    // -------------------------------------------------
    // BUTTON INPUT
    // -------------------------------------------------

    updateButtonManager();

    // -------------------------------------------------
    // WIFI CONNECTION MONITOR
    // -------------------------------------------------

    const bool wifiConnected =
        wifiEnabled &&
        WiFi.status() == WL_CONNECTED;

    // WiFi vừa kết nối hoặc kết nối lại
    if (wifiConnected && !previousWifiConnected) {

        Serial.println("[WIFI] Connected.");

        initialWeatherAttempted = false;

        // Cho phép tải thời tiết ngay
        lastWeatherFetchMs = 0;
    }

    // WiFi vừa mất kết nối
    if (!wifiConnected && previousWifiConnected) {

        Serial.println("[WIFI] Connection lost.");

        initialWeatherAttempted = false;
    }

    previousWifiConnected = wifiConnected;

    // -------------------------------------------------
    // INITIAL WEATHER FETCH
    // -------------------------------------------------

    if (
        wifiConnected &&
        !initialWeatherAttempted
    ) {

        if (scheduleWeatherFetch()) {

            initialWeatherAttempted = true;
        }
    }

    // -------------------------------------------------
    // PERIODIC WEATHER UPDATE
    // -------------------------------------------------

    if (
        wifiConnected &&
        initialWeatherAttempted &&
        now - lastWeatherFetchMs >= WEATHER_REFRESH_MS
    ) {

        scheduleWeatherFetch();
    }

    // -------------------------------------------------
    // MENU ANIMATION
    // -------------------------------------------------

    if (
        currentScreen == SCREEN_MENU &&
        now - lastMenuFrameMs >= 16
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

        struct tm currentTime = {};

        // Get synchronized time
        if (getLocalTime(&currentTime, 0)) {

            if (currentTime.tm_sec != cachedSecond) {

                cachedSecond = currentTime.tm_sec;

                cachedTime = currentTime;

                secondStartedAt = now;
            }

        } else {

            // Fallback clock when NTP is unavailable
            cachedTime.tm_hour = 12;

            cachedTime.tm_min = 0;

            cachedTime.tm_sec =
                (now / 1000) % 60;

            cachedTime.tm_wday = 0;

            cachedTime.tm_mday = 1;

            secondStartedAt =
                now - (now % 1000);
        }

        // Smooth second-hand movement
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
    // SMALL LOOP DELAY
    // -------------------------------------------------

    delay(2);
}
