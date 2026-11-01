#pragma once
#include <Arduino.h>

// Hardware: ESP32-C3 Super Mini + GC9A01 240x240
constexpr uint8_t BUTTON_PIN = 9;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 35;
constexpr uint32_t BUTTON_HOLD_MS = 1000;

// Wi-Fi credentials: điền mạng Wi-Fi của ông tại đây.
// Không đưa mật khẩu thật lên GitHub công khai.
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

constexpr char NTP_SERVER[] = "pool.ntp.org";
constexpr long GMT_OFFSET_SEC = 7 * 3600;
constexpr int DAYLIGHT_OFFSET_SEC = 0;

// Open-Meteo: TP. Hồ Chí Minh
constexpr char WEATHER_LATITUDE[] = "10.8231";
constexpr char WEATHER_LONGITUDE[] = "106.6297";
constexpr uint32_t WEATHER_REFRESH_MS = 30UL * 60UL * 1000UL;

enum ScreenState : uint8_t {
    SCREEN_CLOCK,
    SCREEN_WEATHER,
    SCREEN_MENU
};

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
    bool valid;
};
