#include "system_manager.h"

namespace SystemManager {
namespace {
volatile bool pendingSleep = false;
volatile PowerState currentPowerState = PowerState::Active;
}

void begin() {
    pendingSleep = false;
    currentPowerState = PowerState::Active;
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
        default: return "UNKNOWN";
    }
}

} // namespace SystemManager
