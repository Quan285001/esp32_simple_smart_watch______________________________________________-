#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

// Transitional shared driver handle. Existing face modules use this symbol;
// ownership and initialization remain centralized in this module.
extern TFT_eSPI tft;

bool beginDisplay(uint8_t rotation = 0);
bool isDisplayInitialized();
void blankDisplay();
void clearDisplay(uint16_t color);
