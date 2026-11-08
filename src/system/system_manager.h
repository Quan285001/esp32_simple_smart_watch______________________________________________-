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
