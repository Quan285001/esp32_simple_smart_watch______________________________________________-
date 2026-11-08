//weather_service.h

#pragma once

#include "config.h"

/** @brief Returns a thread-safe copy of the current weather snapshot. */
WeatherData getWeatherSnapshot();
/** @brief Fetches and publishes current conditions and hourly forecast data. */
bool fetchHourlyWeather();
/** @brief Restores the previously persisted weather snapshot. */
bool loadWeatherCache();

/** @brief Validates and updates the coordinates used for weather requests. */
void setWeatherCoordinates(float latitude, float longitude);
/** @brief Fetches optional sunrise, sunset, and lunar information. */
bool fetchLunarDetails();
