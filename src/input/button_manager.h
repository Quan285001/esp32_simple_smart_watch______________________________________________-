#pragma once

#include <Arduino.h>

/** @brief Configures the BOOT button input and initializes input state. */
void beginButtonManager();
/** @brief Polls and debounces the button, then dispatches press events. */
void updateButtonManager();
/** @brief Returns the timestamp of the most recent physical button transition. */
uint32_t getLastButtonActivityMs();
