#pragma once

#include <Arduino.h>

namespace WeatherCoordinator {

void begin();
bool isFetchRunning();
bool canStartTransfer(bool wifiScanBusy, bool bootNetworkBusy);

int32_t refreshCountdownSeconds(bool wifiEnabled, bool wifiConnected);

uint8_t requestManual(
    bool wifiEnabled,
    bool wifiConnected,
    bool wifiScanBusy,
    bool bootNetworkBusy,
    bool transferBusy
);

uint8_t requestForLocation(
    bool wifiEnabled,
    bool wifiConnected,
    bool wifiScanBusy,
    bool bootNetworkBusy,
    bool transferBusy
);

void service(
    uint32_t nowMs,
    bool wifiEnabled,
    bool wifiConnected,
    bool hasWeatherData,
    bool wifiScanBusy,
    bool bootNetworkBusy,
    bool transferBusy
);

void serviceNotification();
bool takeFaceInvalidation();
void onWifiConnected(uint32_t nowMs);
void onWifiEnabled();
void beginBootFetch();
void finishBootFetch(bool success);

} // namespace WeatherCoordinator
