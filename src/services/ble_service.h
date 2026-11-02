#pragma once

#include <Arduino.h>

void beginBLE();
bool readBLECommand(String &command);
void sendBLENotification(const String &message);
bool isBLEConnected();
