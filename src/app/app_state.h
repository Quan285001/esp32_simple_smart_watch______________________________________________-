
#pragma once

#include <Arduino.h>
#include "../config.h"

// Menu hierarchy.
enum MenuLevel : uint8_t {
    MENU_MAIN,
    MENU_CONNECTIVITY,
    MENU_SYSTEM
};

// Menu state.
extern MenuLevel currentMenuLevel;
extern uint8_t menuIndex;

// Number of entries in each menu.
constexpr uint8_t MAIN_MENU_COUNT = 4;
constexpr uint8_t CONNECTIVITY_MENU_COUNT = 3;
constexpr uint8_t SYSTEM_MENU_COUNT = 6;

// Screen state.
extern ScreenState currentScreen;
extern ScreenState screenBeforeMenu;

extern uint8_t forecastIndex;
extern volatile bool weatherDataValid;
extern bool wifiEnabled;
