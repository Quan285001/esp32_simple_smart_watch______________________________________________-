#pragma once

#include <Arduino.h>

// Hardware: ESP32-C3 Super Mini + GC9A01 240x240
constexpr uint8_t BUTTON_PIN = 9;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 35;
constexpr uint32_t BUTTON_HOLD_MS = 1000;
constexpr char NTP_SERVER[] = "pool.ntp.org";
constexpr long GMT_OFFSET_SEC = 7L * 3600L;
constexpr int DAYLIGHT_OFFSET_SEC = 0;
constexpr uint32_t WEATHER_REFRESH_MS = 30UL * 60UL * 1000UL;
constexpr uint32_t WEATHER_CACHE_MAX_AGE_SEC = 6UL * 60UL * 60UL;
constexpr char WEATHER_DEFAULT_LAT[] = "10.8231";
constexpr char WEATHER_DEFAULT_LON[] = "106.6297";

enum ScreenState : uint8_t { SCREEN_CLOCK, SCREEN_WEATHER, SCREEN_MENU };
enum MenuLevel : uint8_t { MENU_MAIN, MENU_CONNECTIVITY, MENU_SYSTEM };
constexpr uint8_t MAIN_MENU_COUNT = 4;
constexpr uint8_t CONNECTIVITY_MENU_COUNT = 4;
constexpr uint8_t SYSTEM_MENU_COUNT = 5;

struct HourlyForecast {
    int hour = 0;
    int temp = 0;
    int weatherCode = 0;
    int precipitationProbability = 0;
    float precipitation = 0.0f;
    float windSpeed = 0.0f;
    float windGusts = 0.0f;
    float uvIndex = 0.0f;
    int cloudCover = 0;
};

struct WeatherData {
    int currentTemp = 0;
    int apparentTemp = 0;
    int maxTemp = 0;
    int minTemp = 0;
    int weatherCode = 0;
    String conditionText = "Waiting for weather";
    HourlyForecast hourly[24];

    float humidity = 0.0f;
    float windSpeed10m = 0.0f;
    float windDirection10m = 0.0f;
    float windGusts10m = 0.0f;
    float uvIndex = 0.0f;
    int cloudCover = 0;
    float surfacePressure = 0.0f;
    float rain = 0.0f;
    float sunshineDuration = 0.0f;
    float daylightDuration = 0.0f;
    String moonPhase = "N/A";
    String moonrise = "N/A";
    String moonset = "N/A";
    String sunrise = "N/A";
    String sunset = "N/A";
    String locationName = "Ho Chi Minh City";
    uint32_t lastUpdatedEpoch = 0;
    bool detailsValid = false;
    bool valid = false;
    bool stale = false;
};
