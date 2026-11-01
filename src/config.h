//config.h

#pragma once

#include <Arduino.h>
#include "secrets.h"

// =====================================================
// HARDWARE
// =====================================================

constexpr uint8_t BUTTON_PIN = 9;

constexpr uint32_t BUTTON_DEBOUNCE_MS = 35;
constexpr uint32_t BUTTON_HOLD_MS = 1000;

// =====================================================
// TIME CONFIGURATION
// =====================================================

constexpr char NTP_SERVER[] = "pool.ntp.org";

constexpr long GMT_OFFSET_SEC = 7 * 3600;
constexpr int DAYLIGHT_OFFSET_SEC = 0;

// =====================================================
// WEATHER CONFIGURATION
// =====================================================

constexpr char WEATHER_LATITUDE[] = "10.8231";
constexpr char WEATHER_LONGITUDE[] = "106.6297";

constexpr uint32_t WEATHER_REFRESH_MS =
    30UL * 60UL * 1000UL;

// =====================================================
// SCREEN STATES
// =====================================================

enum ScreenState : uint8_t {
    SCREEN_CLOCK,
    SCREEN_WEATHER,
    SCREEN_MENU
};

// =====================================================
// WEATHER DATA
// =====================================================

struct HourlyForecast {
    int hour;
    int temp;
    int weatherCode;
};

struct WeatherData {
    int currentTemp;
    int maxTemp;
    int minTemp;
    String conditionText;
    HourlyForecast hourly[24];

    float windSpeed10m;
    float surfacePressure;
    float rain;
    float sunshineDuration;
    float daylightDuration;
    String moonPhase;
    String moonrise;
    String moonset;
    String sunrise;
    String sunset;
    bool detailsValid;
    bool valid;
};
