
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "icons.h"
#include "config.h"
#include <math.h>

extern TFT_eSPI tft;

// =====================================================
// WEATHER FACE STATE
// =====================================================

static int selectedForecastIndex = 0;
static bool forecastSelectionInitialized = false;

static bool weatherAnimating = false;
static uint32_t animationStartedAt = 0;

static constexpr uint32_t WEATHER_ANIMATION_MS = 420;
static constexpr int WEATHER_SLIDE_DISTANCE = 47;

static constexpr int PANEL_X = 15;
static constexpr int PANEL_Y = 30;
static constexpr int PANEL_W = 210;
static constexpr int PANEL_H = 88;

static constexpr int CARD_Y = 124;
static constexpr int CARD_W = 42;
static constexpr int CARD_H = 78;

static const int cardCenterX[4] = {47, 94, 141, 188};

static WeatherData animationWeather;
static int animationOldIndex = 0;
static int animationNewIndex = 0;

// =====================================================
// ANIMATION HELPERS
// =====================================================

static float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float easeOutCubic(float t) {
    t = clamp01(t);
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

static float easeInOutCubic(float t) {
    t = clamp01(t);

    if (t < 0.5f) {
        return 4.0f * t * t * t;
    }

    float v = -2.0f * t + 2.0f;
    return 1.0f - (v * v * v) / 2.0f;
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
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%d", temp);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(color, background);
    tft.drawString(buffer, x, y, font);

    int textWidth = tft.textWidth(buffer, font);

    int degreeX = x + textWidth + 3;
    int degreeY = y + (font >= 4 ? 5 : 3);
    int radius = font >= 4 ? 3 : 2;

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
// MAIN WEATHER CONTENT
// =====================================================

static void drawMainWeatherContent(
    const WeatherData& weather,
    int index,
    int offsetY
) {
    index = (index + 24) % 24;

    const HourlyForecast& forecast = weather.hourly[index];

    const bool isNight =
        forecast.hour < 6 || forecast.hour >= 18;

    const uint16_t panelColor = 0x1082;

    // Icon
    drawWeatherIcon(
        tft,
        52,
        74 + offsetY,
        forecast.weatherCode,
        isNight
    );

    // Main temperature
    int temperature =
        index == 0
            ? weather.currentTemp
            : forecast.temp;

    drawTemperatureWithDegree(
        temperature,
        90,
        60 + offsetY,
        4,
        TFT_WHITE,
        panelColor
    );

    // Maximum temperature
    drawTemperatureWithDegree(
        weather.maxTemp,
        175,
        63 + offsetY,
        2,
        0xFDE0,
        panelColor
    );

    // Minimum temperature
    drawTemperatureWithDegree(
        weather.minTemp,
        175,
        82 + offsetY,
        2,
        0x7BEF,
        panelColor
    );

    char timeLabel[8];

    snprintf(
        timeLabel,
        sizeof(timeLabel),
        "%02d:00",
        forecast.hour
    );

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(0xFDE0, panelColor);

    tft.drawString(
        timeLabel,
        91,
        98 + offsetY,
        1
    );
}

static void drawMainPanelBackground() {
    tft.fillRoundRect(
        PANEL_X,
        PANEL_Y,
        PANEL_W,
        PANEL_H,
        16,
        0x1082
    );

    tft.drawRoundRect(
        PANEL_X,
        PANEL_Y,
        PANEL_W,
        PANEL_H,
        16,
        0x3186
    );
}

// =====================================================
// FORECAST CARD
// =====================================================

static void drawForecastCard(
    const HourlyForecast& forecast,
    int centerX,
    bool selected
) {
    const int boxX = centerX - CARD_W / 2;

    const uint16_t background =
        selected ? 0x2184 : 0x1082;

    const uint16_t border =
        selected ? 0xFDE0 : 0x3186;

    tft.fillRoundRect(
        boxX,
        CARD_Y,
        CARD_W,
        CARD_H,
        10,
        background
    );

    tft.drawRoundRect(
        boxX,
        CARD_Y,
        CARD_W,
        CARD_H,
        10,
        border
    );

    char temperature[8];

    snprintf(
        temperature,
        sizeof(temperature),
        "%d",
        forecast.temp
    );

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, background);

    tft.drawString(
        temperature,
        centerX,
        CARD_Y + 17,
        1
    );

    int degreeX =
        centerX + tft.textWidth(temperature) / 2 + 2;

    tft.drawCircle(
        degreeX,
        CARD_Y + 14,
        2,
        TFT_WHITE
    );

    const bool isNight =
        forecast.hour < 6 || forecast.hour >= 18;

    drawWeatherMiniIcon(
        tft,
        centerX,
        CARD_Y + 42,
        forecast.weatherCode,
        isNight
    );

    char timeLabel[8];

    snprintf(
        timeLabel,
        sizeof(timeLabel),
        "%02d:00",
        forecast.hour
    );

    tft.setTextDatum(MC_DATUM);

    tft.setTextColor(
        selected ? TFT_WHITE : 0x7BEF,
        background
    );

    tft.drawString(
        timeLabel,
        centerX,
        CARD_Y + 66,
        1
    );
}

// =====================================================
// STATIC FORECAST ROW
// =====================================================

static void drawForecastRow(
    const WeatherData& weather,
    int startIndex
) {
    tft.fillRect(
        10,
        120,
        220,
        90,
        TFT_BLACK
    );

    for (int i = 0; i < 4; i++) {
        int dataIndex = (startIndex + i) % 24;

        drawForecastCard(
            weather.hourly[dataIndex],
            cardCenterX[i],
            i == 0
        );
    }
}

// =====================================================
// FOOTER
// =====================================================

static void drawWeatherFooter() {
    tft.fillRect(0, 207, 240, 30, TFT_BLACK);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x7BEF, TFT_BLACK);

    tft.drawString(
        "[Press: Next hour]",
        120,
        221,
        1
    );
}

// =====================================================
// WEATHER SCREEN
// =====================================================

void drawWeatherFaceData(const WeatherData& weather) {
    weatherAnimating = false;

    tft.fillScreen(TFT_BLACK);

    if (!weather.valid) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);

        tft.drawString(
            "Weather unavailable",
            120,
            115,
            2
        );

        tft.setTextColor(0x7BEF, TFT_BLACK);

        tft.drawString(
            "Waiting for connection",
            120,
            140,
            1
        );

        return;
    }

    if (!forecastSelectionInitialized) {
        selectedForecastIndex = 0;
        forecastSelectionInitialized = true;
    }

    selectedForecastIndex =
        (selectedForecastIndex + 24) % 24;

    drawMainPanelBackground();

    drawMainWeatherContent(
        weather,
        selectedForecastIndex,
        0
    );

    drawForecastRow(
        weather,
        selectedForecastIndex
    );

    drawWeatherFooter();
}

// =====================================================
// START FORECAST ANIMATION
// =====================================================

void advanceWeatherForecast(const WeatherData& weather) {
    if (!weather.valid || weatherAnimating) {
        return;
    }

    if (!forecastSelectionInitialized) {
        selectedForecastIndex = 0;
        forecastSelectionInitialized = true;
    }

    animationWeather = weather;

    animationOldIndex = selectedForecastIndex;

    animationNewIndex =
        (selectedForecastIndex + 1) % 24;

    animationStartedAt = millis();

    weatherAnimating = true;
}

// =====================================================
// ANIMATION UPDATE
// =====================================================

void updateWeatherFaceAnimation(const WeatherData& weather) {
    (void)weather;

    if (!weatherAnimating) {
        return;
    }

    uint32_t elapsed =
        millis() - animationStartedAt;

    float progress =
        clamp01((float)elapsed / WEATHER_ANIMATION_MS);

    if (progress >= 1.0f) {
        selectedForecastIndex = animationNewIndex;
        weatherAnimating = false;

        drawWeatherFaceData(animationWeather);
        return;
    }

    // -------------------------------------------------
    // PHASE 1: OLD CONTENT FLOAT OUT
    // PHASE 2: NEW CONTENT FLOAT IN
    // -------------------------------------------------

    const float splitPoint = 0.48f;

    drawMainPanelBackground();

    if (progress < splitPoint) {
        float phase =
            easeOutCubic(progress / splitPoint);

        int offsetY =
            -(int)(phase * 22.0f);

        drawMainWeatherContent(
            animationWeather,
            animationOldIndex,
            offsetY
        );
    } else {
        float phase =
            easeOutCubic(
                (progress - splitPoint) /
                (1.0f - splitPoint)
            );

        int offsetY =
            22 - (int)(phase * 22.0f);

        drawMainWeatherContent(
            animationWeather,
            animationNewIndex,
            offsetY
        );
    }

    // -------------------------------------------------
    // FORECAST CARDS SLIDE RIGHT TO LEFT
    // -------------------------------------------------

    tft.fillRect(
        10,
        120,
        220,
        90,
        TFT_BLACK
    );

    float slideProgress =
        easeInOutCubic(progress);

    int slideOffset =
        WEATHER_SLIDE_DISTANCE -
        (int)(slideProgress * WEATHER_SLIDE_DISTANCE);

    for (int i = 0; i < 4; i++) {
        int dataIndex =
            (animationNewIndex + i) % 24;

        int x =
            cardCenterX[i] + slideOffset;

        drawForecastCard(
            animationWeather.hourly[dataIndex],
            x,
            i == 0
        );
    }

    drawWeatherFooter();
}
