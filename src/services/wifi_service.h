#pragma once

#include <Arduino.h>

namespace WifiService {

bool configureCredentials(const char* ssid, const char* password);
bool isConnected();

} // namespace WifiService
