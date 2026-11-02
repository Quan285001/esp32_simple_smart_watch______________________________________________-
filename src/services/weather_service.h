//weather_service.h

#pragma once

#include "config.h"

WeatherData getWeatherSnapshot();
void fetchHourlyWeather();
\nvoid setWeatherCoordinates(float latitude, float longitude);\n