#pragma once

#include <Arduino.h>

void beginBLE();
void toggleBLE();
bool isBLEEnabled();
bool readBLECommand(String &command);
void sendBLENotification(const String &message);
bool isBLEConnected();
