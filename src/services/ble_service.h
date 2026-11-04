#pragma once

#include <Arduino.h>

/** @brief Initializes the BLE GATT server and starts advertising. */
void beginBLE();
/** @brief Toggles BLE advertising without deinitializing the stack. */
void toggleBLE();
/** @brief Processes deferred BLE events from the application loop. */
void serviceBLE();
/** @brief Releases Bluetooth resources before entering Light Sleep. */
void shutdownBLEForSleep();
/** @brief Returns whether BLE advertising is enabled. */
bool isBLEEnabled();
/** @brief Reads one pending command from the BLE queue without blocking. */
bool readBLECommand(String &command);
/** @brief Sends a notification to the connected BLE client. */
void sendBLENotification(const String &message);
/** @brief Returns whether a BLE client is connected. */
bool isBLEConnected();
