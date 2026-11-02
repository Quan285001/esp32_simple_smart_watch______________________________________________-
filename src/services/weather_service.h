//weather_service.h

#pragma once

#include "config.h"

WeatherData getWeatherSnapshot();
bool fetchHourlyWeather();
bool loadWeatherCache();

void setWeatherCoordinates(float latitude, float longitude);
