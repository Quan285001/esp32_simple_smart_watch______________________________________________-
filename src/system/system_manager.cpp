#include "system_manager.h"

#include <WiFi.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "../config.h"
#include "../app/app_state.h"
#include "../app/app_controller.h"
#include "../input/button_manager.h"
#include "../services/ble_service.h"
#include "../display/display_manager.h"


namespace SystemManager {
namespace {
volatile bool pendingSleep = false;
volatile PowerState currentPowerState = PowerState::Active;
uint8_t autoSleepTimeoutSeconds = 15;
uint32_t lastUserActivityMs = 0;
uint32_t lastButtonActivitySeenMs = 0;
bool previousBleConnected = false;
}

void enterLightSleep() {
    SystemManager::beginSleep();
    while (digitalRead(BUTTON_PIN) == LOW) delay(10);

    // The main loop defers sleep until background radio work has completed.
    // Preserve cached weather/time in RAM and NVS; shut down radios only.
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    wifiEnabled = false;

    shutdownBLEForSleep();

    // Keep the GC9A01 controller awake and simply paint the panel black.
    // This isolates ESP32-C3 Light Sleep from TFT sleep-state restoration:
    // the first BOOT wake should be followed by a normal redraw.
    clearDisplay(TFT_BLACK);

    // GPIO9 is the BOOT button. Keep its pull-up enabled and wake on LOW.
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    gpio_wakeup_enable(static_cast<gpio_num_t>(BUTTON_PIN), GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    Serial.println("[POWER] Light sleep: Wi-Fi OFF, BLE deinitialized, TFT panel blanked.");
    SystemManager::markSleeping();
    esp_light_sleep_start();
    gpio_wakeup_disable(static_cast<gpio_num_t>(BUTTON_PIN));
    SystemManager::markWaking();
    SystemManager::markRestoringServices();

    const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
    Serial.printf("[POWER] Wake cause: %d\n", static_cast<int>(wakeCause));

    // Wait for the wake press to be released before resetting button state.
    while (digitalRead(BUTTON_PIN) == LOW) {
        delay(10);
    }
    delay(BUTTON_DEBOUNCE_MS);

    // The TFT was intentionally kept awake (black screen) during MCU sleep,
    // so no panel reset or sleep-out sequence is needed. Redraw immediately.

    // Radios remain OFF until explicitly enabled from the menu.
    beginButtonManager();
    drawCurrentScreen(true);
    SystemManager::markActive();
    Serial.println("[POWER] Wake complete; radios remain OFF.");
}


void begin() {
    pendingSleep = false;
    currentPowerState = PowerState::Active;
    autoSleepTimeoutSeconds = 15;
    lastUserActivityMs = 0;
    lastButtonActivitySeenMs = 0;
    previousBleConnected = false;
}

bool setAutoSleepTimeoutSeconds(uint8_t seconds) {
    if (!(seconds == 0 || seconds == 10 || seconds == 12 ||
          seconds == 15 || seconds == 18 || seconds == 20)) {
        Serial.printf("[POWER] Rejected invalid Auto Sleep value: %u seconds.\\n", seconds);
        return false;
    }
    autoSleepTimeoutSeconds = seconds;
    lastUserActivityMs = millis();
    Serial.printf("[POWER] Auto Sleep timeout applied: %u seconds.\\n", autoSleepTimeoutSeconds);
    return true;
}

uint8_t getAutoSleepTimeoutSeconds() {
    return autoSleepTimeoutSeconds;
}

void resetAutoSleepActivity(uint32_t nowMs, uint32_t buttonActivityMs, bool bleConnected) {
    lastUserActivityMs = nowMs;
    lastButtonActivitySeenMs = buttonActivityMs;
    previousBleConnected = bleConnected;
}

void serviceAutoSleep(uint32_t nowMs, uint32_t buttonActivityMs, bool bleConnected,
                      bool backgroundWorkActive, bool onClockScreen) {
    if (buttonActivityMs != lastButtonActivitySeenMs) {
        lastButtonActivitySeenMs = buttonActivityMs;
        lastUserActivityMs = nowMs;
    }
    if (bleConnected != previousBleConnected) {
        previousBleConnected = bleConnected;
        lastUserActivityMs = nowMs;
    }
    if (!pendingSleep && !bleConnected && !backgroundWorkActive &&
        lastUserActivityMs != 0 && onClockScreen && autoSleepTimeoutSeconds > 0 &&
        (nowMs - lastUserActivityMs) >= static_cast<uint32_t>(autoSleepTimeoutSeconds) * 1000UL) {
        Serial.printf("[POWER] Auto Light Sleep: %u seconds without user activity.\\n",
                      autoSleepTimeoutSeconds);
        requestSleep();
    }
}

void requestSleep() {
    if (currentPowerState == PowerState::Active) {
        pendingSleep = true;
        currentPowerState = PowerState::PreparingSleep;
    }
}

bool sleepRequested() {
    return pendingSleep;
}

void beginSleep() {
    pendingSleep = false;
    currentPowerState = PowerState::PreparingSleep;
}

void markSleeping() {
    currentPowerState = PowerState::Sleeping;
}

void markWaking() {
    currentPowerState = PowerState::Waking;
}

void markRestoringServices() {
    currentPowerState = PowerState::RestoringServices;
}

void markActive() {
    pendingSleep = false;
    currentPowerState = PowerState::Active;
}

PowerState powerState() {
    return currentPowerState;
}

const char* powerStateName(PowerState state) {
    switch (state) {
        case PowerState::Active: return "ACTIVE";
        case PowerState::PreparingSleep: return "PREPARING_SLEEP";
        case PowerState::Sleeping: return "SLEEPING";
        case PowerState::Waking: return "WAKING";
        case PowerState::RestoringServices: return "RESTORING_SERVICES";
        default: return "UNKNOWN";
    }
}

} // namespace SystemManager
