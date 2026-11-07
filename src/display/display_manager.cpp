#include "display_manager.h"

TFT_eSPI tft = TFT_eSPI();

namespace {
bool displayInitialized = false;
uint8_t activeRotation = 0;
}

bool beginDisplay(uint8_t rotation) {
    if (displayInitialized) {
        if (rotation != activeRotation) {
            tft.setRotation(rotation);
            activeRotation = rotation;
        }
        return true;
    }

    tft.init();
    tft.setRotation(rotation);
    tft.fillScreen(TFT_BLACK);
    activeRotation = rotation;
    displayInitialized = true;
    Serial.println("[DISPLAY] GC9A01 initialized.");
    return true;
}

bool isDisplayInitialized() {
    return displayInitialized;
}

void blankDisplay() {
    if (displayInitialized) tft.fillScreen(TFT_BLACK);
}

void clearDisplay(uint16_t color) {
    if (displayInitialized) tft.fillScreen(color);
}
