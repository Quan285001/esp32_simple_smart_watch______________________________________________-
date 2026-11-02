//weather_service.h

#pragma once

#include "config.h"

WeatherData getWeatherSnapshot();
void fetchHourlyWeather();

void setWeatherCoordinates(float latitude, float longitude);
