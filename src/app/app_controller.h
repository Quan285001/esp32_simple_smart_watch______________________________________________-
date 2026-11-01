
#pragma once

void drawCurrentScreen(bool force = false);

void onButtonShortPress();
void onButtonLongPress();

// Điều hướng menu
void enterSubMenu();
void goBackMenu();
void selectMenuItem();

// Đồng bộ hệ thống
void syncTime();
void syncWeather();
void showDeviceInfo();
