
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>

#include "app_controller.h"
#include "app_state.h"
#include "../services/weather_service.h"

extern TFT_eSPI tft;

extern void drawAnalogClockFace(
    struct tm timeinfo,
    int millisValue,
    int heartRate,
    int steps
);

extern void drawWeatherFaceData(const WeatherData &weather);
extern void advanceWeatherForecast(const WeatherData &weather);

extern void drawMenuScreen(
    int selectedIndex,
    bool wifiStatus,
    bool wifiConnected
);

void drawCurrentScreen(bool force) {
    static ScreenState previous = static_cast<ScreenState>(255);

    if (!force && currentScreen == SCREEN_CLOCK &&
        previous == SCREEN_CLOCK) {
        return;
    }

    previous = currentScreen;

    if (currentScreen == SCREEN_CLOCK) {
        struct tm timeinfo = {};

        if (!getLocalTime(&timeinfo, 10)) {
            timeinfo.tm_hour = 12;
            timeinfo.tm_min = 0;
            timeinfo.tm_sec = (millis() / 1000) % 60;
            timeinfo.tm_wday = 0;
            timeinfo.tm_mday = 1;
        }

        tft.fillScreen(TFT_BLACK);
        drawAnalogClockFace(timeinfo, 0, 0, 0);

    } else if (currentScreen == SCREEN_WEATHER) {
        drawWeatherFaceData(currentWeather);

    } else if (currentScreen == SCREEN_MENU) {
        bool wifiStatus = WiFi.getMode() != WIFI_OFF;
        bool wifiConnected = WiFi.status() == WL_CONNECTED;

        drawMenuScreen(menuIndex, wifiStatus, wifiConnected);
    }
}

void onButtonShortPress() {
    if (currentScreen == SCREEN_MENU) {
        menuIndex = (menuIndex + 1) % 4;

    } else if (currentScreen == SCREEN_WEATHER) {
        advanceWeatherForecast(currentWeather);
        return;

    } else {
        currentScreen = SCREEN_WEATHER;
    }

    drawCurrentScreen(true);
}

void onButtonLongPress() {
    if (currentScreen != SCREEN_MENU) {
        screenBeforeMenu = currentScreen;
        currentScreen = SCREEN_MENU;
        menuIndex = 0;

    } else {
        switch (menuIndex) {
            case 0:
                currentScreen = SCREEN_CLOCK;
                break;

            case 1:
                currentScreen = SCREEN_WEATHER;
                break;

            case 2:
                // Wi-Fi Mode: xử lý bật/tắt tại menu
                break;

            case 3:
            default:
                currentScreen = screenBeforeMenu;
                break;
        }
    }

    drawCurrentScreen(true);
}
