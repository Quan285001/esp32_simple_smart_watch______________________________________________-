#include "weather_coordinator.h"

#include <WiFi.h>

#include "config.h"
#include "../app/app_state.h"
#include "ble_service.h"
#include "weather_service.h"

namespace WeatherCoordinator {
namespace {

constexpr uint32_t RETRY_INTERVAL_MS = 15000UL;
constexpr uint32_t MANUAL_COOLDOWN_MS = 10000UL;

volatile bool fetchRunning = false;
volatile bool faceInvalidationPending = false;
volatile bool locationRefreshPending = false;
volatile int8_t notificationPending = -1;

uint32_t lastSuccessfulFetchMs = 0;
uint32_t lastAttemptMs = 0;
uint32_t lastRetryMs = 0;
uint32_t lastManualRequestMs = 0;
uint32_t lastLocationRetryMs = 0;

void weatherFetchTask(void* parameter) {
    (void)parameter;

    Serial.println("[WEATHER] Background fetch started.");

    const bool success = fetchHourlyWeather();
    if (success) {
        lastSuccessfulFetchMs = millis();
        weatherDataValid = true;
        faceInvalidationPending = true;
    }

    // BLE host operations stay on the application-loop thread.
    notificationPending = success ? 1 : 0;
    Serial.println(success
        ? "[WEATHER] Weather stage finished."
        : "[WEATHER] Weather stage failed.");

    // Keep TLS requests sequential to limit peak ESP32-C3 RAM usage.
    if (success && wifiEnabled && WiFi.status() == WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        Serial.println("[WEATHER] Starting deferred lunar stage.");
        fetchLunarDetails();
    }

    fetchRunning = false;
    vTaskDelete(nullptr);
}

bool scheduleFetch(
    bool wifiIsEnabled,
    bool wifiIsConnected,
    bool wifiScanBusy,
    bool bootNetworkBusy,
    bool transferBusy
) {
    if (!wifiIsEnabled) {
        Serial.println("[WEATHER] Fetch skipped: Wi-Fi is OFF.");
        return false;
    }

    if (!wifiIsConnected || fetchRunning || wifiScanBusy ||
        bootNetworkBusy || transferBusy) {
        return false;
    }

    fetchRunning = true;

    const BaseType_t created = xTaskCreate(
        weatherFetchTask,
        "weatherFetch",
        8192,
        nullptr,
        1,
        nullptr
    );

    if (created != pdPASS) {
        fetchRunning = false;
        Serial.println("[WEATHER] Could not create background task.");
        return false;
    }

    lastAttemptMs = millis();
    Serial.println("[WEATHER] Fetch scheduled.");
    return true;
}

} // namespace

void begin() {
    fetchRunning = false;
    faceInvalidationPending = false;
    locationRefreshPending = false;
    notificationPending = -1;

    lastSuccessfulFetchMs = 0;
    lastAttemptMs = 0;
    lastRetryMs = 0;
    lastManualRequestMs = 0;
    lastLocationRetryMs = 0;
}

bool isFetchRunning() {
    return fetchRunning;
}

bool canStartTransfer(bool wifiScanBusy, bool bootNetworkBusy) {
    return !fetchRunning && !wifiScanBusy && !bootNetworkBusy;
}

int32_t refreshCountdownSeconds(bool wifiIsEnabled, bool wifiIsConnected) {
    if (!wifiIsEnabled) return -1;
    if (!wifiIsConnected) return -2;
    if (fetchRunning) return -3;

    const uint32_t elapsed = millis() - lastSuccessfulFetchMs;
    if (elapsed >= WEATHER_REFRESH_MS) return 0;

    const uint32_t remainingMs = WEATHER_REFRESH_MS - elapsed;
    return static_cast<int32_t>((remainingMs + 999UL) / 1000UL);
}

uint8_t requestManual(
    bool wifiIsEnabled,
    bool wifiIsConnected,
    bool wifiScanBusy,
    bool bootNetworkBusy,
    bool transferBusy
) {
    if (!wifiIsEnabled || !wifiIsConnected) return 2;

    // Preserve the BLE API's existing result codes: a boot-stage conflict
    // is reported as unavailable unless a fetch/scan/transfer is already busy.
    if (fetchRunning || wifiScanBusy || transferBusy) return 1;

    const uint32_t now = millis();
    if (lastManualRequestMs != 0 &&
        now - lastManualRequestMs < MANUAL_COOLDOWN_MS) {
        return 3;
    }

    if (!scheduleFetch(
            wifiIsEnabled, wifiIsConnected, wifiScanBusy,
            bootNetworkBusy, transferBusy)) {
        return fetchRunning ? 1 : 2;
    }

    lastManualRequestMs = now;
    return 0;
}

uint8_t requestForLocation(
    bool wifiIsEnabled,
    bool wifiIsConnected,
    bool wifiScanBusy,
    bool bootNetworkBusy,
    bool transferBusy
) {
    locationRefreshPending = true;
    lastLocationRetryMs = 0;

    if (!wifiIsEnabled || !wifiIsConnected) return 2;
    if (fetchRunning || wifiScanBusy || transferBusy) return 1;

    if (scheduleFetch(
            wifiIsEnabled, wifiIsConnected, wifiScanBusy,
            bootNetworkBusy, transferBusy)) {
        locationRefreshPending = false;
        return 0;
    }

    return 2;
}

void service(
    uint32_t nowMs,
    bool wifiIsEnabled,
    bool wifiIsConnected,
    bool hasWeatherData,
    bool wifiScanBusy,
    bool bootNetworkBusy,
    bool transferBusy
) {
    // A GPS update received during another request is retried once resources
    // are available. Keep it pending while Wi-Fi is unavailable.
    if (locationRefreshPending && wifiIsEnabled && wifiIsConnected &&
        !fetchRunning && !wifiScanBusy && !bootNetworkBusy && !transferBusy &&
        (lastLocationRetryMs == 0 ||
         nowMs - lastLocationRetryMs >= RETRY_INTERVAL_MS)) {
        lastLocationRetryMs = nowMs;

        if (scheduleFetch(
                wifiIsEnabled, wifiIsConnected, wifiScanBusy,
                bootNetworkBusy, transferBusy)) {
            locationRefreshPending = false;
            return;
        }
    }

    if (!wifiIsEnabled || !wifiIsConnected || bootNetworkBusy ||
        fetchRunning || wifiScanBusy || transferBusy) {
        return;
    }

    if (!hasWeatherData) {
        if (lastRetryMs == 0 ||
            nowMs - lastRetryMs >= RETRY_INTERVAL_MS) {
            if (scheduleFetch(
                    wifiIsEnabled, wifiIsConnected, wifiScanBusy,
                    bootNetworkBusy, transferBusy)) {
                lastRetryMs = nowMs;
                Serial.println("[WEATHER] Retrying weather fetch...");
            }
        }
        return;
    }

    if (nowMs - lastSuccessfulFetchMs >= WEATHER_REFRESH_MS &&
        (lastAttemptMs == 0 ||
         nowMs - lastAttemptMs >= RETRY_INTERVAL_MS)) {
        scheduleFetch(
            wifiIsEnabled, wifiIsConnected, wifiScanBusy,
            bootNetworkBusy, transferBusy);
    }
}

void serviceNotification() {
    const int8_t result = notificationPending;
    if (result < 0) return;

    notificationPending = -1;
    sendBLENotification(result ? "WEATHER_OK" : "WEATHER_FAILED");
}

bool takeFaceInvalidation() {
    const bool pending = faceInvalidationPending;
    faceInvalidationPending = false;
    return pending;
}

void onWifiConnected(uint32_t nowMs) {
    lastSuccessfulFetchMs = nowMs - WEATHER_REFRESH_MS;
    lastAttemptMs = 0;
    lastRetryMs = 0;
}

void onWifiEnabled() {
    lastSuccessfulFetchMs = 0;
    lastAttemptMs = 0;
    lastRetryMs = 0;
}

void beginBootFetch() {
    fetchRunning = true;
}

void finishBootFetch(bool success) {
    if (success) {
        lastSuccessfulFetchMs = millis();
        weatherDataValid = true;
        faceInvalidationPending = true;
    }
    fetchRunning = false;
}

} // namespace WeatherCoordinator
