#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include <math.h>
#include <time.h>

extern TFT_eSPI tft;

namespace {
uint8_t weatherPage = 0;
constexpr uint16_t BG = TFT_BLACK;
constexpr uint16_t PANEL = 0x1082;
constexpr uint16_t MUTED = 0x9CF3;
constexpr uint16_t ACCENT = 0x5DDF;
constexpr uint16_t WHITE = TFT_WHITE;

void header(const char* title, const WeatherData& w) {
    tft.fillScreen(BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(WHITE, BG);
    tft.drawString(title, 120, 17, 2);
    tft.drawFastHLine(24, 32, 192, 0x3186);
    tft.setTextColor(MUTED, BG);
    tft.drawString(w.locationName, 120, 224, 1);
    if (w.stale) {
        tft.setTextColor(TFT_ORANGE, BG);
        tft.drawString("CACHED / OLD DATA", 120, 211, 1);
    } else if (w.lastUpdatedEpoch) {
        tft.setTextColor(MUTED, BG);
        time_t ts = (time_t)w.lastUpdatedEpoch;
        struct tm tmv;
        localtime_r(&ts, &tmv);
        char stamp[20];
        snprintf(stamp, sizeof(stamp), "Updated %02d:%02d", tmv.tm_hour, tmv.tm_min);
        tft.drawString(stamp, 120, 211, 1);
    }
}
void valueBox(int x, int y, int w, int h, const char* label, const String& value, const char* unit = "") {
    tft.fillRoundRect(x, y, w, h, 9, PANEL);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(MUTED, PANEL);
    tft.drawString(label, x + w/2, y + 11, 1);
    tft.setTextColor(WHITE, PANEL);
    tft.drawString(value + unit, x + w/2, y + h - 13, 2);
}
String windDirection(float deg) {
    const char* dirs[] = {"N","NE","E","SE","S","SW","W","NW"};
    int i = ((int)lroundf(deg / 45.0f)) % 8;
    return dirs[i];
}
void drawCurrent(const WeatherData& w) {
    header("CURRENT WEATHER", w);
    if (!w.valid) {
        tft.setTextColor(TFT_ORANGE, BG);
        tft.drawString("Waiting for weather data", 120, 105, 2);
        tft.setTextColor(MUTED, BG);
        tft.drawString("Connect Wi-Fi or sync from phone", 120, 130, 1);
        return;
    }
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(WHITE, BG);
    tft.drawString(String(w.currentTemp) + " C", 120, 75, 7);
    tft.setTextColor(ACCENT, BG);
    tft.drawString(w.conditionText, 120, 105, 2);
    tft.setTextColor(MUTED, BG);
    tft.drawString("Feels like " + String(w.apparentTemp) + " C", 120, 123, 1);
    valueBox(18, 139, 62, 53, "HUMIDITY", String(w.humidity,0), "%");
    valueBox(89, 139, 62, 53, "WIND", String(w.windSpeed10m,0), " km/h");
    valueBox(160, 139, 62, 53, "UV MAX", String(w.uvIndex,1));
    tft.setTextColor(MUTED, BG);
    tft.drawString("Low " + String(w.minTemp) + " C   /   High " + String(w.maxTemp) + " C", 120, 198, 1);
}
void drawRain(const WeatherData& w) {
    header("RAIN FORECAST", w);
    if (!w.valid) {
        tft.setTextColor(TFT_ORANGE, BG);
        tft.drawString("No forecast available", 120, 110, 2);
        return;
    }
    int maxPop = 1;
    for (int i=0;i<8;i++) maxPop = max(maxPop, w.hourly[i].precipitationProbability);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(WHITE, BG);
    tft.drawString("Next 8 hours", 120, 49, 2);
    for (int i=0;i<8;i++) {
        int x = 22 + i*28;
        int barH = (int)(82.0f * w.hourly[i].precipitationProbability / 100.0f);
        tft.fillRoundRect(x, 154-barH, 16, max(2,barH), 4, w.hourly[i].precipitationProbability >= 60 ? TFT_BLUE : ACCENT);
        tft.setTextColor(WHITE, BG);
        tft.drawString(String(w.hourly[i].precipitationProbability), x+8, 164, 1);
        tft.setTextColor(MUTED, BG);
        char hh[6]; snprintf(hh,sizeof(hh),"%02d",w.hourly[i].hour);
        tft.drawString(hh, x+8, 180, 1);
    }
    tft.setTextColor(WHITE, BG);
    tft.drawString("Probability (%)", 120, 196, 1);
    float total = 0;
    for (int i=0;i<8;i++) total += w.hourly[i].precipitation;
    tft.setTextColor(ACCENT, BG);
    tft.drawString("Rain amount: " + String(total,1) + " mm / 8h", 120, 207, 1);
}
void drawWindSun(const WeatherData& w) {
    header("WIND / SUN / SKY", w);
    if (!w.valid) {
        tft.setTextColor(TFT_ORANGE, BG);
        tft.drawString("No weather data", 120, 110, 2);
        return;
    }
    valueBox(18, 45, 88, 55, "WIND", String(w.windSpeed10m,1), " km/h");
    valueBox(114, 45, 108, 55, "GUST", String(w.windGusts10m,1), " km/h");
    valueBox(18, 108, 88, 55, "DIRECTION", windDirection(w.windDirection10m));
    valueBox(114, 108, 108, 55, "PRESSURE", String(w.surfacePressure,0), " hPa");
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(WHITE, BG);
    tft.drawString("UV " + String(w.uvIndex,1) + "   Cloud " + String(w.cloudCover) + "%", 120, 176, 1);
    tft.setTextColor(ACCENT, BG);
    tft.drawString("Sun " + w.sunrise + " - " + w.sunset, 120, 191, 1);
    tft.setTextColor(MUTED, BG);
    tft.drawString("Moon " + w.moonrise + " / " + w.moonset, 120, 203, 1);
    tft.drawString(w.moonPhase, 120, 214, 1);
}
}

void drawWeatherFaceData(const WeatherData& weather) {
    if (weatherPage == 0) drawCurrent(weather);
    else if (weatherPage == 1) drawRain(weather);
    else drawWindSun(weather);
}
void advanceWeatherForecast(const WeatherData& weather) {
    weatherPage = (weatherPage + 1) % 3;
    drawWeatherFaceData(weather);
}
void toggleWeatherDetails(const WeatherData& weather) {
    advanceWeatherForecast(weather);
}
bool scrollWeatherDetails(const WeatherData& weather) {
    (void)weather;
    return false;
}
void updateWeatherFaceAnimation(const WeatherData& weather) {
    (void)weather;
}
