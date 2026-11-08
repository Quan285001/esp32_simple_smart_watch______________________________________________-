#include "app_state.h"

// Single ownership point for shared application/UI state.
ScreenState currentScreen = SCREEN_CLOCK;
ScreenState screenBeforeMenu = SCREEN_CLOCK;

MenuLevel currentMenuLevel = MENU_MAIN;
uint8_t menuIndex = 0;
uint8_t forecastIndex = 0;

volatile bool weatherDataValid = false;
bool wifiEnabled = true;
