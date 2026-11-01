//button_manager.cpp

#include <Arduino.h>
#include "button_manager.h"
#include "../config.h"
#include "../app/app_state.h"
#include "../app/app_controller.h"

static bool stableState = HIGH;
static bool lastRawState = HIGH;
static uint32_t lastChangeMs = 0;
static uint32_t pressedAtMs = 0;
static bool longPressHandled = false;

void beginButtonManager() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    stableState = digitalRead(BUTTON_PIN);
    lastRawState = stableState;
}

void updateButtonManager() {
    const bool raw = digitalRead(BUTTON_PIN);
    const uint32_t now = millis();
    if (raw != lastRawState) {
        lastRawState = raw;
        lastChangeMs = now;
    }
    if ((now - lastChangeMs) < BUTTON_DEBOUNCE_MS || raw == stableState) {
        if (stableState == LOW && !longPressHandled && (now - pressedAtMs) >= BUTTON_HOLD_MS) {
            longPressHandled = true;
            onButtonLongPress();
        }
        return;
    }

    stableState = raw;
    if (stableState == LOW) {
        pressedAtMs = now;
        longPressHandled = false;
    } else {
        if (!longPressHandled && (now - pressedAtMs) < BUTTON_HOLD_MS) onButtonShortPress();
    }
}
