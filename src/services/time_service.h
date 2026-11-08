// time_service.h

#pragma once
#include <Arduino.h>
#include <time.h>

/** @brief Restores the last valid Unix time and UTC offset from NVS. */
bool restoreTimeFromNvs();
/** @brief Saves a valid current Unix timestamp to NVS. */
bool saveTimeCheckpoint();
/** @brief Periodically checkpoints time without writing flash every second. */
void serviceTimePersistence();
/** @brief Configures NTP using saved timezone data and IP fallback if needed. */
void beginTimeService();
/** @brief Applies the coordinate-derived timezone returned by the weather API. */
void applyTimezoneFromWeather(const String &timezoneName, long utcOffset);
/** @brief Reads the current local time using the configured system clock. */
bool readLocalTime(struct tm &timeinfo);
