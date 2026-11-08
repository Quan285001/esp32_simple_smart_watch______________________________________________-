#pragma once

#include <Arduino.h>

namespace WifiService {

bool configureCredentials(const char* ssid, const char* password);
bool isConnected();
uint8_t requestScan(bool weatherBusy, bool bootNetworkBusy, bool transferBusy);
bool isScanBusy();
void serviceScan();


} // namespace WifiService
