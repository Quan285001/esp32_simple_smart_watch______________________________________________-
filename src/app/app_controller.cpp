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
#include "../services/ble_service.h"
#include "../watchface/face_manager.h"

extern TFT_eSPI tft;

extern void toggleWiFi();
extern void requestLightSleep();
extern uint8_t requestWeatherFetch();

extern void drawAnalogClockFace(
    struct tm timeinfo,
    int millisValue,
    int heartRate,
    int steps
);

extern void drawWeatherFaceData(const WeatherData &weather);
extern void advanceWeatherForecast(const WeatherData &weather);
extern void advanceWeatherForecast(const WeatherData& w);
extern void toggleWeatherDetails(const WeatherData& weather);
extern bool scrollWeatherDetails(const WeatherData& weather);

// Menu renderer supporting nested menu levels.
extern void drawMenuScreen(
    int selectedIndex,
    MenuLevel level,
    bool wifiStatus,
    bool wifiConnected
);

extern void resetMenuScreen();
extern void drawSystemInfoScreen();

// =====================================================
// MENU HELPERS
// =====================================================

/**
 * @brief Implements the get menu count operation.

 */
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

/**
 * @brief Resets the menu cursor and invalidates the menu renderer state.
 */
static void resetMenuSelection() {
    menuIndex = 0;
    resetMenuScreen();
}

// =====================================================
// DRAW CURRENT SCREEN
// =====================================================

/**
 * @brief Renders the active screen, optionally forcing a complete redraw.
 * @param force Input or state used by this operation.
 */
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

            if (FaceManager::hasActiveFace()) {
                const FaceRenderStatus status = FaceManager::renderActive(timeinfo, getWeatherSnapshot());
                if (status != FaceRenderStatus::Ok) {
                    Serial.printf("[FACE] Render rejected (%u); using built-in clock.\\n",
                                  static_cast<unsigned int>(status));
                    FaceManager::disableActive();
                    tft.fillScreen(TFT_BLACK);
                    drawAnalogClockFace(timeinfo, 0, 0, 0);
                }
            } else {
                tft.fillScreen(TFT_BLACK);
                drawAnalogClockFace(timeinfo, 0, 0, 0);
            }

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

        case SCREEN_INFO:
            drawSystemInfoScreen();
            break;

        default:
            break;
    }
}

// =====================================================
// ENTER / EXIT SUBMENU
// =====================================================

/**
 * @brief Enters the submenu associated with the selected main-menu item.

 */
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

/**
 * @brief Returns to the parent menu or the screen that was open before the menu.
 */
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

/**
 * @brief Starts NTP synchronization and reports whether local time is available.
 */
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

/**
 * @brief Performs a manual weather update when Wi-Fi is connected.
 */
void syncWeather() {
    Serial.println("[WEATHER] Manual update requested.");

    const uint8_t result = requestWeatherFetch();
    switch (result) {
        case 0:
            Serial.println("[WEATHER] Update scheduled in background.");
            break;
        case 1:
            Serial.println("[WEATHER] An update is already running.");
            break;
        case 2:
            Serial.println("[WEATHER] Wi-Fi is unavailable.");
            break;
        case 3:
            Serial.println("[WEATHER] Please wait before requesting another update.");
            break;
        default:
            Serial.println("[WEATHER] Could not schedule update.");
            break;
    }
}

/**
 * @brief Prints hardware and memory diagnostics to the serial console.
 */
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

/**
 * @brief Executes the action associated with the selected item in the active menu.
 */
void selectMenuItem() {
    bool traceBleMenuAction = false;

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

    /**
     * @brief Implements the if operation.
     * @param currentMenuLevel Input or state used by this operation.
     */
    else if (currentMenuLevel == MENU_CONNECTIVITY) {

        switch (menuIndex) {

            case 0:
                traceBleMenuAction = true;
                Serial.println("[MENU][TRACE] BLE menu action ENTER");
                Serial.println("[MENU][TRACE] toggleBLE call ENTER");
                toggleBLE();
                Serial.println("[MENU][TRACE] toggleBLE call EXIT");
                Serial.println(isBLEEnabled() ? "[BT] Bluetooth enabled." : "[BT] Bluetooth disabled.");
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

    /**
     * @brief Implements the if operation.
     * @param currentMenuLevel Input or state used by this operation.
     */
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
                currentScreen = SCREEN_INFO;
                drawCurrentScreen(true);
                return;

            case 3:
                requestLightSleep();
                break;

            case 4:
                Serial.println("[SYSTEM] Restarting...");
                delay(100);
                ESP.restart();
                break;

            case 5:
                goBackMenu();
                return;

            default:
                break;
        }
    }

    resetMenuScreen();
    if (traceBleMenuAction) {
        Serial.println("[MENU][TRACE] BLE menu redraw ENTER");
    }
    drawCurrentScreen(true);
    if (traceBleMenuAction) {
        Serial.println("[MENU][TRACE] BLE menu redraw EXIT");
    }
}

// =====================================================
// BUTTON: SHORT PRESS
// =====================================================

/**
 * @brief Handles a short button press according to the active screen.

 */
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

        if (scrollWeatherDetails(snapshot)) return;
        advanceWeatherForecast(snapshot);

        return;
    }

    if (currentScreen == SCREEN_INFO) {
        currentScreen = SCREEN_MENU;
        resetMenuScreen();
        drawCurrentScreen(true);
        return;
    }

    currentScreen = SCREEN_WEATHER;

    drawCurrentScreen(true);
}


// =====================================================
// BUTTON: DOUBLE PRESS
// =====================================================
/**
 * @brief Handles a double button press according to the active screen.
 */
void onButtonDoublePress() {
    if (currentScreen == SCREEN_WEATHER) {
        WeatherData snapshot = getWeatherSnapshot();
        toggleWeatherDetails(snapshot);
        return;
    }

    if (currentScreen == SCREEN_MENU) {
        selectMenuItem();
    }
}

// =====================================================
// BUTTON: LONG PRESS
// =====================================================

/**
 * @brief Opens the main menu or activates the selected menu item.
 */
void onButtonLongPress() {

    // Open the menu when a face is active.
    if (currentScreen != SCREEN_MENU) {

        screenBeforeMenu = currentScreen;

        currentScreen = SCREEN_MENU;

        currentMenuLevel = MENU_MAIN;

        resetMenuSelection();

        drawCurrentScreen(true);

        return;
    }

    // Activate the selected menu item.
    selectMenuItem();
}
