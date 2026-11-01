#pragma once
#include "config.h"

extern WeatherData currentWeather;
void fetchHourlyWeather();

// Trả về bản sao nhất quán, an toàn khi tác vụ Wi-Fi đang cập nhật dữ liệu.
WeatherData getWeatherSnapshot();