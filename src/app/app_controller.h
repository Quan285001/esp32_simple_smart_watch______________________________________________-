
#pragma once

/** @brief Renders the active screen; force requests a complete redraw. */
void drawCurrentScreen(bool force = false);

/** @brief Handles a short press of the physical button. */
void onButtonShortPress();
/** @brief Handles a double press of the physical button. */
void onButtonDoublePress();
/** @brief Handles a long press of the physical button. */
void onButtonLongPress();

// Menu navigation.
void enterSubMenu();
void goBackMenu();
void selectMenuItem();

// System synchronization.
void syncTime();
void syncWeather();
void showDeviceInfo();
