#pragma once

#include <Arduino.h>

namespace SystemManager {

enum class PowerState : uint8_t {
    Active,
    PreparingSleep,
    Sleeping,
    Waking,
    RestoringServices
};

void begin();
bool setAutoSleepTimeoutSeconds(uint8_t seconds);
uint8_t getAutoSleepTimeoutSeconds();
void resetAutoSleepActivity(uint32_t nowMs, uint32_t buttonActivityMs, bool bleConnected);
void serviceAutoSleep(uint32_t nowMs, uint32_t buttonActivityMs, bool bleConnected,
                      bool backgroundWorkActive, bool onClockScreen);
void requestSleep();
void enterLightSleep();
bool sleepRequested();
void beginSleep();
void markSleeping();
void markWaking();
void markRestoringServices();
void markActive();
PowerState powerState();
const char* powerStateName(PowerState state);

} // namespace SystemManager
