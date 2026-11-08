#pragma once
#include <Arduino.h>
namespace WeatherCoordinator {
void begin(); bool isFetchRunning(); bool canStartTransfer(bool,bool);
int32_t refreshCountdownSeconds(bool,bool);
uint8_t requestManual(bool,bool,bool,bool,bool);
uint8_t requestForLocation(bool,bool,bool,bool,bool);
void service(uint32_t,bool,bool,bool,bool,bool,bool);
void serviceNotification(); bool takeFaceInvalidation(); void onWifiConnected(uint32_t);
void onWifiEnabled(); void beginBootFetch(); void finishBootFetch(bool);
}
