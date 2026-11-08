
#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "app/app_state.h"
#include "app/app_controller.h"
#include "app/boot_coordinator.h"
#include "app/app_runtime.h"
#include "input/button_manager.h"
#include "services/time_service.h"
#include "services/weather_service.h"
#include "services/weather_coordinator.h"
#include "services/wifi_service.h"
#include "services/ble_service.h"
#include "system/diagnostics.h"
#include "display/display_manager.h"
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


// Prevent a new bulk face transfer from overlapping TLS/weather/Wi-Fi scan work.
bool canStartWfsTransfer() {
    return WeatherCoordinator::canStartTransfer(
        WifiService::isScanBusy(),
        BootCoordinator::isBusy());
}

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
        WifiService::isScanBusy(), BootCoordinator::isBusy(),
        TransferManager::isBusy());
}

uint8_t requestWeatherFetchForLocation() {
    return WeatherCoordinator::requestForLocation(
        wifiEnabled, WiFi.status() == WL_CONNECTED,
        WifiService::isScanBusy(), BootCoordinator::isBusy(),
        TransferManager::isBusy());
}


// =====================================================
// ASYNCHRONOUS WI-FI SCAN
// =====================================================

// Return: 0=started, 1=already busy, 2=network work has priority, 3=start failed.
uint8_t requestWifiScan() {
    return WifiService::requestScan(
        WeatherCoordinator::isFetchRunning(),
        BootCoordinator::isBusy(),
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
    if (BootCoordinator::isBusy() || WeatherCoordinator::isFetchRunning()) {
        Serial.println("[WIFI] Toggle ignored while startup/weather network work is active.");
        return;
    }
    if (WifiService::isScanBusy()) {
        Serial.println("[WIFI] Toggle ignored while scan is active.");
        return;
    }

    if (wifiEnabled) {
        WifiService::disable();
        return;
    }

    if (!WifiService::enableSavedCredentials()) return;
    beginTimeService();
    WeatherCoordinator::onWifiEnabled();
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
    BootCoordinator::restorePersistentState();
    bootTerminalLog("[BOOT] Starting startup services", TFT_GREEN);

    BootCoordinator::begin();
}

// =====================================================
// MAIN LOOP
// =====================================================

/**
 * @brief Implements the loop operation.
 */
