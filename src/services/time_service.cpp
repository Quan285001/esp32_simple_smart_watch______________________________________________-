//time_service.cpp

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "time_service.h"
#include "../config.h"

/**
 * @brief Configures the system clock to synchronize with the configured NTP server.

 */
void beginTimeService() {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
}

/**
 * @brief Reads the current local time using the configured system clock.
 * @param timeinfo Input or state used by this operation.
 */
bool readLocalTime(struct tm &timeinfo) {
    return getLocalTime(&timeinfo, 10);
}
