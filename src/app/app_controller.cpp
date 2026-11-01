
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>
#include <esp_system.h>

#include "app_controller.h"
#include "app_state.h"

#include "../config.h"
#include "../services/time_service.h"
#include "../services/weather_service.h"

extern TFT_eSPI tft;

extern void toggleWiFi();

extern void drawAnalogClockFace(
    struct tm timeinfo,
    int millisValue,
    int heartRate,
    int steps
);

extern void drawWeatherFaceData(const WeatherData &weather);
extern void advanceWeatherForecast(const WeatherData &weather);
extern void advanceWeatherForecast(const WeatherData& w);

// Renderer mới: có thêm MenuLevel
extern void drawMenuScreen(
    int selectedIndex,
    MenuLevel level,
    bool wifiStatus,
    bool wifiConnected
);

extern void resetMenuScreen();

// =====================================================
// MENU HELPERS
// =====================================================

static uint8_t getMenuCount() {
    switch (currentMenuLevel) {
        case MENU_MAIN:
            return MAIN_MENU_COUNT;

        case MENU_CONNECTIVITY:
            return CONNECTIVITY_MENU_COUNT;

        case MENU_SYSTEM:
            return SYSTEM_MENU_COUNT;

        default:
            return MAIN_MENU_COUNT;
    }
}

static void resetMenuSelection() {
    menuIndex = 0;
    resetMenuScreen();
}

// =====================================================
// DRAW CURRENT SCREEN
// =====================================================

void drawCurrentScreen(bool force) {
    static ScreenState previous =
        static_cast<ScreenState>(255);

    if (
        !force &&
        currentScreen == SCREEN_CLOCK &&
        previous == SCREEN_CLOCK
    ) {
        return;
    }

    previous = currentScreen;

    switch (currentScreen) {

        case SCREEN_CLOCK: {
            struct tm timeinfo = {};

            if (!readLocalTime(timeinfo)) {
                timeinfo.tm_hour = 12;
                timeinfo.tm_min = 0;
                timeinfo.tm_sec = (millis() / 1000) % 60;
                timeinfo.tm_wday = 0;
                timeinfo.tm_mday = 1;
            }

            tft.fillScreen(TFT_BLACK);

            drawAnalogClockFace(
                timeinfo,
                0,
                0,
                0
            );

            break;
        }

        case SCREEN_WEATHER: {
            WeatherData snapshot = getWeatherSnapshot();

            drawWeatherFaceData(snapshot);

            break;
        }

        case SCREEN_MENU: {
            const bool connected =
                wifiEnabled &&
                WiFi.status() == WL_CONNECTED;

            drawMenuScreen(
                menuIndex,
                currentMenuLevel,
                wifiEnabled,
                connected
            );

            break;
        }

        default:
            break;
    }
}

// =====================================================
// ENTER / EXIT SUBMENU
// =====================================================

void enterSubMenu() {
    if (currentMenuLevel == MENU_MAIN) {

        if (menuIndex == 2) {
            currentMenuLevel = MENU_CONNECTIVITY;

        } else if (menuIndex == 3) {
            currentMenuLevel = MENU_SYSTEM;

        } else {
            return;
        }

        resetMenuSelection();
    }
}

void goBackMenu() {
    if (currentMenuLevel == MENU_MAIN) {
        currentScreen = screenBeforeMenu;
        currentMenuLevel = MENU_MAIN;

    } else {
        currentMenuLevel = MENU_MAIN;
        resetMenuSelection();
    }

    resetMenuScreen();
}

// =====================================================
// SYSTEM FUNCTIONS
// =====================================================

void syncTime() {
    Serial.println("[TIME] Starting NTP synchronization.");

    beginTimeService();

    struct tm timeinfo = {};

    if (readLocalTime(timeinfo)) {
        Serial.println("[TIME] Time synchronized.");
    } else {
        Serial.println("[TIME] Waiting for NTP response.");
    }
}

void syncWeather() {
    Serial.println("[WEATHER] Manual update requested.");

    if (!wifiEnabled || WiFi.status() != WL_CONNECTED) {
        Serial.println("[WEATHER] Wi-Fi is unavailable.");
        return;
    }

    // Tạm thời thực hiện trực tiếp.
    // Sẽ chuyển sang tác vụ nền khi tích hợp renderer hoàn chỉnh.
    fetchHourlyWeather();

    Serial.println("[WEATHER] Manual update finished.");
}

void showDeviceInfo() {
    Serial.println("========== DEVICE INFO ==========");
    Serial.printf("Chip: ESP32-C3\n");
    Serial.printf("Chip revision: %d\n", ESP.getChipRevision());
    Serial.printf("CPU frequency: %u MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Sketch size: %u bytes\n", ESP.getSketchSize());
    Serial.println("=================================");
}

// =====================================================
// SELECT MENU ITEM
// =====================================================

void selectMenuItem() {

    if (currentMenuLevel == MENU_MAIN) {

        switch (menuIndex) {

            case 0:
                currentScreen = SCREEN_CLOCK;
                currentMenuLevel = MENU_MAIN;
                break;

            case 1:
                currentScreen = SCREEN_WEATHER;
                currentMenuLevel = MENU_MAIN;
                break;

            case 2:
            case 3:
                enterSubMenu();
                return;

            default:
                break;
        }
    }

    else if (currentMenuLevel == MENU_CONNECTIVITY) {

        switch (menuIndex) {

            case 0:
                // Bluetooth placeholder
                Serial.println(
                    "[BT] Bluetooth interface is not implemented yet."
                );
                break;

            case 1:
                toggleWiFi();
                break;

            case 2:
                goBackMenu();
                return;

            default:
                break;
        }
    }

    else if (currentMenuLevel == MENU_SYSTEM) {

        switch (menuIndex) {

            case 0:
                syncTime();
                break;

            case 1:
                syncWeather();
                break;

            case 2:
                showDeviceInfo();
                break;

            case 3:
                Serial.println("[SYSTEM] Restarting...");
                delay(100);
                ESP.restart();
                break;

            case 4:
                goBackMenu();
                return;

            default:
                break;
        }
    }

    resetMenuScreen();
    drawCurrentScreen(true);
}

// =====================================================
// BUTTON: SHORT PRESS
// =====================================================

void onButtonShortPress() {

    if (currentScreen == SCREEN_MENU) {

        const uint8_t count = getMenuCount();

        if (count > 0) {
            menuIndex = (menuIndex + 1) % count;
        }

        return;
    }

    if (currentScreen == SCREEN_WEATHER) {

        WeatherData snapshot = getWeatherSnapshot();

        advanceWeatherForecast(snapshot);

        return;
    }

    currentScreen = SCREEN_WEATHER;

    drawCurrentScreen(true);
}

// =====================================================
// BUTTON: LONG PRESS
// =====================================================

void onButtonLongPress() {

    // Đang ở màn hình chính: mở menu
    if (currentScreen != SCREEN_MENU) {

        screenBeforeMenu = currentScreen;

        currentScreen = SCREEN_MENU;

        currentMenuLevel = MENU_MAIN;

        resetMenuSelection();

        drawCurrentScreen(true);

        return;
    }

    // Đang ở menu: chọn mục
    selectMenuItem();
}
