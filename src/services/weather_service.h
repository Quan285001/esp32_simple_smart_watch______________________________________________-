//weather_service.h

#pragma once

#include "config.h"

WeatherData getWeatherSnapshot();
bool fetchHourlyWeather();

void setWeatherCoordinates(float latitude, float longitude);
