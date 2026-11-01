
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

// =====================================================
// WMO WEATHER CODE CLASSIFICATION
// =====================================================

enum WeatherType : uint8_t {
    WEATHER_CLEAR,
    WEATHER_PARTLY_CLOUDY,
    WEATHER_CLOUDY,
    WEATHER_FOG,
    WEATHER_DRIZZLE,
    WEATHER_FREEZING_DRIZZLE,
    WEATHER_RAIN,
    WEATHER_FREEZING_RAIN,
    WEATHER_SNOW,
    WEATHER_SNOW_GRAINS,
    WEATHER_RAIN_SHOWERS,
    WEATHER_SNOW_SHOWERS,
    WEATHER_THUNDERSTORM,
    WEATHER_THUNDERSTORM_HAIL,
    WEATHER_UNKNOWN
};

inline WeatherType getWeatherType(int code) {
    switch (code) {
        case 0:
            return WEATHER_CLEAR;

        case 1:
        case 2:
            return WEATHER_PARTLY_CLOUDY;

        case 3:
            return WEATHER_CLOUDY;

        case 45:
        case 48:
            return WEATHER_FOG;

        case 51:
        case 53:
        case 55:
            return WEATHER_DRIZZLE;

        case 56:
        case 57:
            return WEATHER_FREEZING_DRIZZLE;

        case 61:
        case 63:
        case 65:
            return WEATHER_RAIN;

        case 66:
        case 67:
            return WEATHER_FREEZING_RAIN;

        case 71:
        case 73:
        case 75:
            return WEATHER_SNOW;

        case 77:
            return WEATHER_SNOW_GRAINS;

        case 80:
        case 81:
        case 82:
            return WEATHER_RAIN_SHOWERS;

        case 85:
        case 86:
            return WEATHER_SNOW_SHOWERS;

        case 95:
        case 96:
        case 97:
        case 99:
            return WEATHER_THUNDERSTORM_HAIL;

        default:
            return WEATHER_UNKNOWN;
    }
}

// =====================================================
// BASIC ICONS
// =====================================================

inline void drawMiniSun(TFT_eSPI &tft, int x, int y, uint16_t color) {
    tft.fillCircle(x, y, 5, color);
    tft.drawFastVLine(x, y - 10, 4, color);
    tft.drawFastVLine(x, y + 7, 4, color);
    tft.drawFastHLine(x - 10, y, 4, color);
    tft.drawFastHLine(x + 7, y, 4, color);
    tft.drawLine(x - 7, y - 7, x - 10, y - 10, color);
    tft.drawLine(x + 7, y - 7, x + 10, y - 10, color);
    tft.drawLine(x - 7, y + 7, x - 10, y + 10, color);
    tft.drawLine(x + 7, y + 7, x + 10, y + 10, color);
}

inline void drawMiniCloud(TFT_eSPI &tft, int x, int y, uint16_t color) {
    tft.fillCircle(x - 6, y + 2, 5, color);
    tft.fillCircle(x + 5, y, 7, color);
    tft.fillCircle(x, y - 4, 6, color);
    tft.fillRect(x - 8, y, 16, 8, color);
}

inline void drawMiniRain(TFT_eSPI &tft, int x, int y, uint16_t color) {
    drawMiniCloud(tft, x, y - 3, color);
    tft.drawLine(x - 6, y + 6, x - 8, y + 11, 0x05FF);
    tft.drawLine(x, y + 6, x - 2, y + 11, 0x05FF);
    tft.drawLine(x + 6, y + 6, x + 4, y + 11, 0x05FF);
}

inline void drawMiniThunder(TFT_eSPI &tft, int x, int y, uint16_t color) {
    drawMiniCloud(tft, x, y - 4, color);
    tft.drawLine(x, y + 3, x - 4, y + 9, 0xFDE0);
    tft.drawLine(x - 4, y + 9, x + 2, y + 9, 0xFDE0);
    tft.drawLine(x + 2, y + 9, x - 2, y + 15, 0xFDE0);
}

inline void drawMiniSunCloud(TFT_eSPI &tft, int x, int y, uint16_t color) {
    drawMiniSun(tft, x - 5, y - 5, 0xFDE0);
    drawMiniCloud(tft, x + 2, y + 2, color);
}

inline void drawMiniCrescentMoon(TFT_eSPI &tft, int x, int y, uint16_t color) {
    tft.fillCircle(x, y, 6, color);
    tft.fillCircle(x + 3, y - 2, 5, 0x0000);
}

inline void drawMiniMoon(TFT_eSPI &tft, int x, int y, uint16_t color) {
    drawMiniCrescentMoon(tft, x, y, color);
}

inline void drawMiniMoonCloud(TFT_eSPI &tft, int x, int y, uint16_t color) {
    drawMiniCrescentMoon(tft, x - 5, y - 5, 0xFCE0);
    drawMiniCloud(tft, x + 2, y + 2, color);
}

// =====================================================
// ADDITIONAL WEATHER ICONS
// =====================================================

inline void drawMiniFog(TFT_eSPI &tft, int x, int y, uint16_t color) {
    drawMiniCloud(tft, x, y - 4, color);
    tft.drawFastHLine(x - 10, y + 7, 20, color);
    tft.drawFastHLine(x - 7, y + 11, 14, color);
}

inline void drawMiniSnow(TFT_eSPI &tft, int x, int y, uint16_t color) {
    for (int i = -1; i <= 1; ++i) {
        int px = x + i * 7;
        tft.drawFastVLine(px, y - 5, 10, color);
        tft.drawFastHLine(px - 4, y, 8, color);
        tft.drawLine(px - 3, y - 3, px + 3, y + 3, color);
        tft.drawLine(px - 3, y + 3, px + 3, y - 3, color);
    }
}

inline void drawMiniFreezingRain(TFT_eSPI &tft, int x, int y, uint16_t color) {
    drawMiniCloud(tft, x, y - 4, color);
    tft.drawLine(x - 6, y + 5, x - 8, y + 10, 0x05FF);
    tft.drawLine(x + 5, y + 5, x + 3, y + 10, 0x05FF);
    tft.drawFastVLine(x, y + 6, 5, 0xFFFF);
    tft.drawFastHLine(x - 2, y + 8, 5, 0xFFFF);
}

inline void drawMiniSnowShowers(TFT_eSPI &tft, int x, int y, uint16_t color) {
    drawMiniCloud(tft, x, y - 4, color);
    drawMiniSnow(tft, x, y + 10, 0xFFFF);
}

inline void drawMiniUnknown(TFT_eSPI &tft, int x, int y, uint16_t color) {
    tft.drawCircle(x, y, 7, color);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(color, TFT_BLACK);
    tft.drawString("?", x, y, 1);
}

// =====================================================
// UNIVERSAL MINI WEATHER ICON
// =====================================================

inline void drawWeatherMiniIcon(
    TFT_eSPI &tft,
    int x,
    int y,
    int code,
    bool isNight
) {
    switch (getWeatherType(code)) {
        case WEATHER_CLEAR:
            if (isNight) drawMiniCrescentMoon(tft, x, y, 0xFCE0);
            else drawMiniSun(tft, x, y, 0xFDE0);
            break;

        case WEATHER_PARTLY_CLOUDY:
            if (isNight) drawMiniMoonCloud(tft, x, y, TFT_WHITE);
            else drawMiniSunCloud(tft, x, y, TFT_WHITE);
            break;

        case WEATHER_CLOUDY:
            drawMiniCloud(tft, x, y, 0xCE79);
            break;

        case WEATHER_FOG:
            drawMiniFog(tft, x, y, 0x7BEF);
            break;

        case WEATHER_DRIZZLE:
        case WEATHER_RAIN:
        case WEATHER_RAIN_SHOWERS:
            drawMiniRain(tft, x, y, TFT_WHITE);
            break;

        case WEATHER_FREEZING_DRIZZLE:
        case WEATHER_FREEZING_RAIN:
            drawMiniFreezingRain(tft, x, y, TFT_WHITE);
            break;

        case WEATHER_SNOW:
        case WEATHER_SNOW_GRAINS:
            drawMiniSnow(tft, x, y, TFT_WHITE);
            break;

        case WEATHER_SNOW_SHOWERS:
            drawMiniSnowShowers(tft, x, y, TFT_WHITE);
            break;

        case WEATHER_THUNDERSTORM:
        case WEATHER_THUNDERSTORM_HAIL:
            drawMiniThunder(tft, x, y, 0xCE79);
            break;

        default:
            drawMiniUnknown(tft, x, y, TFT_WHITE);
            break;
    }
}

// =====================================================
// UNIVERSAL LARGE WEATHER ICON
// =====================================================

inline void drawWeatherIcon(
    TFT_eSPI &tft,
    int x,
    int y,
    int code,
    bool isNight
) {
    switch (getWeatherType(code)) {
        case WEATHER_CLEAR:
            if (isNight) {
                drawMiniCrescentMoon(tft, x, y, 0xFCE0);
            } else {
                tft.fillCircle(x, y, 14, 0xFDE0);
                for (int a = 0; a < 8; ++a) {
                    float angle = a * 0.785398f;
                    int x1 = x + (int)(18 * cosf(angle));
                    int y1 = y + (int)(18 * sinf(angle));
                    int x2 = x + (int)(24 * cosf(angle));
                    int y2 = y + (int)(24 * sinf(angle));
                    tft.drawLine(x1, y1, x2, y2, 0xFDE0);
                }
            }
            break;

        case WEATHER_PARTLY_CLOUDY:
            if (isNight) drawMiniMoonCloud(tft, x, y, TFT_WHITE);
            else drawMiniSunCloud(tft, x, y, TFT_WHITE);
            break;

        case WEATHER_CLOUDY:
            drawMiniCloud(tft, x, y, 0xCE79);
            break;

        case WEATHER_FOG:
            drawMiniFog(tft, x, y, 0x7BEF);
            break;

        case WEATHER_DRIZZLE:
        case WEATHER_RAIN:
        case WEATHER_RAIN_SHOWERS:
            drawMiniRain(tft, x, y, TFT_WHITE);
            break;

        case WEATHER_FREEZING_DRIZZLE:
        case WEATHER_FREEZING_RAIN:
            drawMiniFreezingRain(tft, x, y, TFT_WHITE);
            break;

        case WEATHER_SNOW:
        case WEATHER_SNOW_GRAINS:
            drawMiniSnow(tft, x, y, TFT_WHITE);
            break;

        case WEATHER_SNOW_SHOWERS:
            drawMiniSnowShowers(tft, x, y, TFT_WHITE);
            break;

        case WEATHER_THUNDERSTORM:
        case WEATHER_THUNDERSTORM_HAIL:
            drawMiniThunder(tft, x, y, 0xCE79);
            break;

        default:
            drawMiniUnknown(tft, x, y, TFT_WHITE);
            break;
    }
}
