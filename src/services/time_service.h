//time_service.h

#pragma once
#include <time.h>
/** @brief Configures NTP synchronization for the system clock. */
void beginTimeService();
/** @brief Reads the current local time into the supplied tm structure. */
bool readLocalTime(struct tm &timeinfo);
