#include "boot_coordinator.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../config.h"
#include "app_state.h"
#include "../services/time_service.h"
#include "../services/weather_service.h"
#include "../services/weather_coordinator.h"
#include "../storage/storage_manager.h"
#include "../storage/face_storage.h"
#include "../watchface/face_manager.h"
#include "../system/system_manager.h"
#include "../system/diagnostics.h"

extern void bootTerminalLog(const char* msg, uint16_t color);

namespace BootCoordinator {
namespace {

enum class State : uint8_t {
    Idle,
    NetworkRunning,
    BlePending,
    Complete
};

portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
State state = State::Idle;

void setState(State next) {
    portENTER_CRITICAL(&stateMux);
    state = next;
    portEXIT_CRITICAL(&stateMux);
}

State getState() {
    portENTER_CRITICAL(&stateMux);
    const State current = state;
    portEXIT_CRITICAL(&stateMux);
    return current;
}

void networkStartupTask(void* parameter) {
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
        WeatherCoordinator::beginBootFetch();
        bootTerminalLog("[WEATHER] Fetching forecast", TFT_YELLOW);
        const bool weatherUpdated = fetchHourlyWeather();
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
        // Keep the coordinator busy until both sequential TLS stages are complete.
        WeatherCoordinator::finishBootFetch(weatherUpdated);
    } else {
        bootTerminalLog("[INFO] Wi-Fi unavailable; using saved data", TFT_YELLOW);
    }

    // BLE is initialized by loop() after this task exits, so its allocation
    // cannot overlap either HTTPS/TLS request.
    bootTerminalLog("[OK] Network startup stage complete", TFT_GREEN);
    SystemDiagnostics::logMemory("bootNetwork task exit");
    setState(State::BlePending);
    vTaskDelete(nullptr);
}

} // namespace

void restorePersistentState() {
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
        SystemManager::setAutoSleepTimeoutSeconds(savedAutoSleep);
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
}

bool begin() {
    if (getState() != State::Idle) return false;

    wifiEnabled = true;
    setState(State::NetworkRunning);
    const BaseType_t created = xTaskCreate(
        networkStartupTask,
        "bootNetwork",
        8192,
        nullptr,
        1,
        nullptr
    );

    if (created != pdPASS) {
        setState(State::BlePending);
        bootTerminalLog("[WARN] Network task unavailable; skipping Wi-Fi", TFT_YELLOW);
        WiFi.disconnect(false, false);
        WiFi.mode(WIFI_OFF);
        wifiEnabled = false;
        Serial.println("[BOOT] Could not create network task; staying offline.");
        return false;
    }
    return true;
}

bool isBusy() {
    const State current = getState();
    return current == State::NetworkRunning || current == State::BlePending;
}

bool takeBleInitRequest() {
    portENTER_CRITICAL(&stateMux);
    const bool pending = state == State::BlePending;
    if (pending) state = State::Complete;
    portEXIT_CRITICAL(&stateMux);
    if (pending) {
        // Let the network worker execute its self-delete and give the idle
        // task a tick to reclaim its stack before BLE allocates its buffers.
        vTaskDelay(1);
    }
    return pending;
}

} // namespace BootCoordinator
