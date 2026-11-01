
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "icons.h"
#include "config.h"
#include <time.h>
#include <math.h>

extern TFT_eSPI tft;

static int selectedForecastIndex = 0;
static bool forecastSelectionInitialized = false;

static bool weatherAnimating = false;
static uint32_t animationStartedAt = 0;

static constexpr uint32_t WEATHER_ANIMATION_MS = 280;
static constexpr int WEATHER_SLIDE_DISTANCE = 47;

static WeatherData animationWeather;
static int animationOldIndex = 0;
static int animationNewIndex = 0;

static const int colX[4] = {47, 94, 141, 188};
static const int boxWidth = 42;
static const int boxHeight = 78;
static const int boxY = 124;

// =====================================================
// COLOR / ANIMATION HELPERS
// =====================================================

static float easeOutCubic(float t) {
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

static uint16_t blendColor(
    uint16_t from,
    uint16_t to,
    float amount
) {
    int r1 = (from >> 11) & 0x1F;
    int g1 = (from >> 5) & 0x3F;
    int b1 = from & 0x1F;

    int r2 = (to >> 11) & 0x1F;
    int g2 = (to >> 5) & 0x3F;
    int b2 = to & 0x1F;

    int r = r1 + (int)((r2 - r1) * amount);
    int g = g1 + (int)((g2 - g1) * amount);
    int b = b1 + (int)((b2 - b1) * amount);

    return (r << 11) | (g << 5) | b;
}

// =====================================================
// TEMPERATURE
// =====================================================

static void drawTemperatureWithDegree(
    int temp,
    int x,
    int y,
    int font,
    uint16_t color,
    uint16_t background
) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", temp);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(color, background);
    tft.drawString(buf, x, y, font);

    int textW = tft.textWidth(buf, font);

    int degreeX = x + textW + 3;
    int degreeY = y + ((font >= 4) ? 5 : 3);
    int radius = (font >= 4) ? 3 : 2;

    tft.drawCircle(
        degreeX + radius,
        degreeY,
        radius,
        color
    );

    int cX = degreeX + radius * 2 + 4;
    tft.drawString("C", cX, y, font);
}

// =====================================================
// MAIN WEATHER PANEL
// =====================================================

static void drawMainWeatherContent(
    const WeatherData& w,
    int index,
    int offsetY,
    uint16_t textColor
) {
    const HourlyForecast& f = w.hourly[index];

    const bool isNight =
        (f.hour < 6 || f.hour >= 18);

    const int yOffset = offsetY;

    drawWeatherIcon(
        tft,
        52,
        74 + yOffset,
        f.weatherCode,
        isNight
    );

    drawTemperatureWithDegree(
        index == 0 ? w.currentTemp : f.temp,
        90,
        60 + yOffset,
        4,
        textColor,
        0x1082
    );

    drawTemperatureWithDegree(
        w.maxTemp,
        175,
        63 + yOffset,
        2,
        textColor,
        0x1082
    );

    drawTemperatureWithDegree(
        w.minTemp,
        175,
        82 + yOffset,
        2,
        0x7BEF,
        0x1082
    );

    char label[16];
    snprintf(label, sizeof(label), "%02d:00", f.hour);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(0xFDE0, 0x1082);
    tft.drawString(label, 91, 98 + yOffset, 1);
}

static void drawMainWeatherPanel(
    const WeatherData& w,
    int index
) {
    tft.fillRoundRect(15, 30, 210, 88, 16, 0x1082);
    tft.drawRoundRect(15, 30, 210, 88, 16, 0x3186);

    drawMainWeatherContent(
        w,
        index,
        0,
        TFT_WHITE
    );
}

// =====================================================
// FORECAST CARD
// =====================================================

static void drawForecastCard(
    const HourlyForecast& f,
    int centerX,
    bool selected
) {
    const int boxX = centerX - boxWidth / 2;

    const uint16_t bgColor =
        selected ? 0x2184 : 0x1082;

    const uint16_t borderColor =
        selected ? 0xFDE0 : 0x3186;

    tft.fillRoundRect(
        boxX,
        boxY,
        boxWidth,
        boxHeight,
        10,
        bgColor
    );

    tft.drawRoundRect(
        boxX,
        boxY,
        boxWidth,
        boxHeight,
        10,
        borderColor
    );

    char tempBuf[8];
    snprintf(tempBuf, sizeof(tempBuf), "%d", f.temp);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, bgColor);
    tft.drawString(tempBuf, centerX, boxY + 17, 1);

    int degreeX =
        centerX + tft.textWidth(tempBuf) / 2 + 2;

    tft.drawCircle(
        degreeX,
        boxY + 14,
        2,
        TFT_WHITE
    );

    bool isNight =
        (f.hour < 6 || f.hour >= 18);

    drawWeatherMiniIcon(
        tft,
        centerX,
        boxY + 42,
        f.weatherCode,
        isNight
    );

    char timeLabel[6];
    snprintf(timeLabel, sizeof(timeLabel), "%02d:00", f.hour);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(
        selected ? TFT_WHITE : 0x7BEF,
        bgColor
    );

    tft.drawString(
        timeLabel,
        centerX,
        boxY + 66,
        1
    );
}

// =====================================================
// STATIC WEATHER SCREEN
// =====================================================

static void drawForecastRow(
    const WeatherData& w,
    int startIndex
) {
    tft.fillRect(10, 120, 220, 90, TFT_BLACK);

    for (int p = 0; p < 4; p++) {
        int dataIndex = (startIndex + p) % 24;

        drawForecastCard(
            w.hourly[dataIndex],
            colX[p],
            p == 0
        );
    }
}

void drawWeatherFaceData(const WeatherData& w) {
    if (!w.valid) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("Weather unavailable", 120, 115, 2);
        return;
    }

    if (!forecastSelectionInitialized) {
        selectedForecastIndex = 0;
        forecastSelectionInitialized = true;
    }

    weatherAnimating = false;

    tft.fillScreen(TFT_BLACK);

    drawMainWeatherPanel(
        w,
        selectedForecastIndex
    );

    drawForecastRow(
        w,
        selectedForecastIndex
    );

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString(
        "[Press: Next hour]",
        120,
        216,
        1
    );
}

// =====================================================
// START ANIMATION
// =====================================================

void advanceWeatherForecast(const WeatherData& w) {
    if (!w.valid) return;

    if (weatherAnimating) return;

    if (!forecastSelectionInitialized) {
        selectedForecastIndex = 0;
        forecastSelectionInitialized = true;
    }

    animationWeather = w;

    animationOldIndex = selectedForecastIndex;

    animationNewIndex =
        (selectedForecastIndex + 1) % 24;

    animationStartedAt = millis();

    weatherAnimating = true;
}

// =====================================================
// ANIMATION FRAME
// Call this repeatedly from main loop.
// =====================================================

void updateWeatherFaceAnimation(const WeatherData& w) {
    if (!weatherAnimating) return;

    uint32_t elapsed = millis() - animationStartedAt;

    float progress =
        (float)elapsed / WEATHER_ANIMATION_MS;

    if (progress >= 1.0f) {
        selectedForecastIndex = animationNewIndex;
        weatherAnimating = false;

        drawWeatherFaceData(animationWeather);
        return;
    }

    float eased = easeOutCubic(progress);

    // ---------------------------------------------
    // FLOAT OUT / FLOAT IN - MAIN WEATHER PANEL
    // ---------------------------------------------

    tft.fillRect(15, 30, 210, 88, 0x1082);
    tft.drawRoundRect(15, 30, 210, 88, 16, 0x3186);

    // Cũ bay lên.
    int oldOffset = -(int)(eased * 28);

    // Mới bay từ dưới lên.
    int newOffset =
        28 - (int)(eased * 28);

    // Tạo hiệu ứng mờ giả lập bằng cách chuyển màu.
    uint16_t oldColor =
        blendColor(TFT_WHITE, 0x7BEF, eased);

    uint16_t newColor =
        blendColor(0x7BEF, TFT_WHITE, eased);

    if (progress < 0.55f) {
        drawMainWeatherContent(
            animationWeather,
            animationOldIndex,
            oldOffset,
            oldColor
        );
    }

    if (progress > 0.35f) {
        drawMainWeatherContent(
            animationWeather,
            animationNewIndex,
            newOffset,
            newColor
        );
    }

    // ---------------------------------------------
    // SLIDE FORECAST ROW - RIGHT TO LEFT
    // ---------------------------------------------

    tft.fillRect(10, 120, 220, 90, TFT_BLACK);

    for (int p = 0; p < 4; p++) {
        int dataIndex =
            (animationNewIndex + p) % 24;

        int x =
            colX[p] +
            WEATHER_SLIDE_DISTANCE -
            (int)(eased * WEATHER_SLIDE_DISTANCE);

        drawForecastCard(
            animationWeather.hourly[dataIndex],
            x,
            p == 0
        );
    }

    // ---------------------------------------------
    // FOOTER
    // ---------------------------------------------

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString(
        "[Press: Next hour]",
        120,
        216,
        1
    );
}
