#pragma once

#include <Arduino.h>

void beginBLE();
void toggleBLE();
void shutdownBLEForSleep();
bool isBLEEnabled();
bool readBLECommand(String &command);
void sendBLENotification(const String &message);
bool isBLEConnected();
