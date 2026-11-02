#pragma once
#include "../config.h"

WeatherData getWeatherSnapshot();
void fetchHourlyWeather();
bool loadWeatherCache();
bool saveWeatherCache();
void setWeatherLocation(float latitude, float longitude);
