#pragma once

#include <Arduino.h>

namespace WifiService {

bool configureCredentials(const char* ssid, const char* password);
struct ConnectionSnapshot {
    bool connected;
    bool justConnected;
    bool justDisconnected;
};
ConnectionSnapshot serviceConnectionMonitor(uint32_t nowMs, bool enabled, bool bootNetworkBusy);
void resetConnectionMonitor(uint32_t nowMs);
bool isConnected();
uint8_t requestScan(bool weatherBusy, bool bootNetworkBusy, bool transferBusy);
bool isScanBusy();
void serviceScan();


} // namespace WifiService
