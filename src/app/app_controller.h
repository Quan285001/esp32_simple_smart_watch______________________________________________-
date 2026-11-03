
#pragma once

void drawCurrentScreen(bool force = false);

void onButtonShortPress();
void onButtonDoublePress();
void onButtonLongPress();

// Menu navigation.
void enterSubMenu();
void goBackMenu();
void selectMenuItem();

// System synchronization.
void syncTime();
void syncWeather();
void showDeviceInfo();
