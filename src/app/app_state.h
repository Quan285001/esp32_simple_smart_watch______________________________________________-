
#pragma once

#include <Arduino.h>
#include "../config.h"

// Các cấp menu
enum MenuLevel : uint8_t {
    MENU_MAIN,
    MENU_CONNECTIVITY,
    MENU_SYSTEM
};

// Trạng thái menu
extern MenuLevel currentMenuLevel;
extern uint8_t menuIndex;

// Số mục của từng menu
constexpr uint8_t MAIN_MENU_COUNT = 4;
constexpr uint8_t CONNECTIVITY_MENU_COUNT = 3;
constexpr uint8_t SYSTEM_MENU_COUNT = 5;

// Trạng thái màn hình
extern ScreenState currentScreen;
extern ScreenState screenBeforeMenu;

extern uint8_t forecastIndex;
extern bool weatherDataValid;
extern bool wifiEnabled;
