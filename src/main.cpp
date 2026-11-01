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
bool weatherDataValid = false;

extern void drawAnalogClockFace(struct tm timeinfo, int millisValue, int heartRate, int steps);
extern void showBootTerminal();

static uint32_t lastWeatherFetchMs = 0;
static uint32_t lastClockDrawMs = 0;
static bool initialWeatherAttempted = false;

void setup() {
    Serial.begin(115200);
    delay(250);
    Serial.println("\n[BOOT] ESP32-C3 Smart Watch");

    beginButtonManager();
    Serial.println("[BOOT] Button ready");

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    Serial.println("[BOOT] GC9A01 ready");

    showBootTerminal();
    drawCurrentScreen(true);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    beginTimeService();
    Serial.println("[WIFI] Connecting in background");
}

void loop() {
    updateButtonManager();

    if (WiFi.status() == WL_CONNECTED && !initialWeatherAttempted) {
        initialWeatherAttempted = true;
        fetchHourlyWeather();
        lastWeatherFetchMs = millis();
        if (currentScreen == SCREEN_WEATHER) drawCurrentScreen(true);
    }

    if (initialWeatherAttempted && WiFi.status() == WL_CONNECTED &&
        millis() - lastWeatherFetchMs >= WEATHER_REFRESH_MS) {
        fetchHourlyWeather();
        lastWeatherFetchMs = millis();
        if (currentScreen == SCREEN_WEATHER) drawCurrentScreen(true);
    }

    if (currentScreen == SCREEN_CLOCK && millis() - lastClockDrawMs >= 40) {
        static int cachedSecond = -1;
        static struct tm cachedTime = {};
        static uint32_t secondStartedAt = 0;
        struct tm now = {};

        if (getLocalTime(&now, 0)) {
            if (now.tm_sec != cachedSecond) {
                cachedSecond = now.tm_sec;
                cachedTime = now;
                secondStartedAt = millis();
            }
        } else {
            cachedTime.tm_hour = 12;
            cachedTime.tm_min = 0;
            cachedTime.tm_sec = (millis() / 1000) % 60;
            cachedTime.tm_wday = 0;
            cachedTime.tm_mday = 1;
            secondStartedAt = millis() - (millis() % 1000);
        }

        uint32_t elapsed = millis() - secondStartedAt;
        if (elapsed > 999) elapsed = 999;
        drawAnalogClockFace(cachedTime, static_cast<int>(elapsed), 0, 0);
        lastClockDrawMs = millis();
    }

    delay(5);
}
