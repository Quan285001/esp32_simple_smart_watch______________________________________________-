#pragma once
#include <Arduino.h>
#include "../config.h"

extern ScreenState currentScreen;
extern ScreenState screenBeforeMenu;
extern uint8_t menuIndex;
extern uint8_t forecastIndex;
extern bool weatherDataValid;
extern bool wifiEnabled;