#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "display_canvas.h"

// Transitional handle for legacy UI modules. New Watch Face modules use the
// backend-neutral DisplayCanvas interface instead.
extern TFT_eSPI tft;

bool beginDisplay(uint8_t rotation = 0);
bool isDisplayInitialized();
void blankDisplay();
void clearDisplay(uint16_t color);
