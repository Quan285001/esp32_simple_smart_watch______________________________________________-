// Button input manager implementation.

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
static bool pendingSinglePress = false;
static uint32_t pendingSingleAtMs = 0;
static constexpr uint32_t DOUBLE_PRESS_MS = 280;
static uint32_t lastButtonActivityMs = 0;

uint32_t getLastButtonActivityMs() {
    return lastButtonActivityMs;
}

/**
 * @brief Configures the BOOT button input and initializes the debouncing state.

 */
void beginButtonManager() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    stableState = digitalRead(BUTTON_PIN);
    lastRawState = stableState;
    lastChangeMs = millis();
    lastButtonActivityMs = lastChangeMs;
}

/**
 * @brief Samples the BOOT button and dispatches short, double, and long-press events.

 */
void updateButtonManager() {
    const uint32_t now = millis();
    const bool raw = digitalRead(BUTTON_PIN);

    if (raw != lastRawState) {
        lastButtonActivityMs = now;
        lastRawState = raw;
        lastChangeMs = now;
    }

    if (raw != stableState && (now - lastChangeMs) >= BUTTON_DEBOUNCE_MS) {
        stableState = raw;

        if (stableState == LOW) {
            pressedAtMs = now;
            longPressHandled = false;
        } else if (!longPressHandled && (now - pressedAtMs) < BUTTON_HOLD_MS) {
            if (pendingSinglePress && (now - pendingSingleAtMs) <= DOUBLE_PRESS_MS) {
                pendingSinglePress = false;
                onButtonDoublePress();
            } else {
                // If the previous click timed out, deliver it before queuing this one.
                if (pendingSinglePress) onButtonShortPress();
                pendingSinglePress = true;
                pendingSingleAtMs = now;
            }
        }
    }

    if (stableState == LOW && !longPressHandled &&
        (now - pressedAtMs) >= BUTTON_HOLD_MS) {
        longPressHandled = true;
        pendingSinglePress = false;
        onButtonLongPress();
    }

    if (pendingSinglePress && (now - pendingSingleAtMs) > DOUBLE_PRESS_MS) {
        pendingSinglePress = false;
        onButtonShortPress();
    }
}
