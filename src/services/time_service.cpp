#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "time_service.h"
#include "../config.h"

void beginTimeService() {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
}

bool readLocalTime(struct tm &timeinfo) {
    return getLocalTime(&timeinfo, 10);
}
