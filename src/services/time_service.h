// time_service.h

#pragma once
#include <Arduino.h>
#include <time.h>

/** @brief Configures NTP using saved timezone data and IP fallback if needed. */
void beginTimeService();
/** @brief Applies the coordinate-derived timezone returned by the weather API. */
void applyTimezoneFromWeather(const String &timezoneName, long utcOffset);
/** @brief Reads the current local time using the configured system clock. */
bool readLocalTime(struct tm &timeinfo);
