
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "icons.h"
#include "config.h"
#include <math.h>

extern TFT_eSPI tft;


extern int32_t getWeatherRefreshCountdownSeconds();

// Show the next automatic refresh countdown at the top of the face.
static int32_t lastCountdownDisplaySecond = INT32_MIN;

static void drawWeatherUpdateCountdown(bool force = false) {
    const int32_t seconds = getWeatherRefreshCountdownSeconds();
    if (!force && seconds == lastCountdownDisplaySecond) return;
    lastCountdownDisplaySecond = seconds;

    char label[32];
    if (seconds == -1) {
        snprintf(label, sizeof(label), "Wi-Fi OFF");
    } else if (seconds == -2) {
        snprintf(label, sizeof(label), "Waiting for Wi-Fi");
    } else if (seconds == -3) {
        snprintf(label, sizeof(label), "Updating weather...");
    } else {
        const uint32_t minutes = static_cast<uint32_t>(seconds) / 60U;
        const uint32_t remainder = static_cast<uint32_t>(seconds) % 60U;
        snprintf(label, sizeof(label), "Next update %02lu:%02lu",
                 static_cast<unsigned long>(minutes),
                 static_cast<unsigned long>(remainder));
    }

    tft.fillRect(0, 11, 240, 18, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString(label, 120, 20, 1);
}

// =====================================================
// 1. WEATHER FACE STATE
// =====================================================

// Currently selected forecast hour.
static int selectedForecastIndex = 0;
static bool forecastSelectionInitialized = false;
static bool weatherDetailsOpen = false;
static int weatherDetailsScroll = 0;
static int weatherDetailsScrollFrom = 0;
static int weatherDetailsScrollTo = 0;
static bool weatherDetailsScrollAnimating = false;
static uint32_t weatherDetailsScrollAt = 0;
static WeatherData detailWeather;

// Forward declaration: detail-view close returns to the main weather face.

void drawWeatherFaceData(const WeatherData& weather);

static constexpr int DETAIL_PANEL_X = 20, DETAIL_PANEL_Y = 20;
static constexpr int DETAIL_PANEL_W = 200, DETAIL_PANEL_H = 200;

// Animation state.
static bool weatherAnimating = false;
static uint32_t animationStartedAt = 0;

// Animation duration and slide distance.
static constexpr uint32_t WEATHER_ANIMATION_MS = 400;
static constexpr int WEATHER_SLIDE_DISTANCE = 47;

// Weather panel dimensions.
static constexpr int PANEL_X = 15;
static constexpr int PANEL_Y = 30;
static constexpr int PANEL_W = 210;
static constexpr int PANEL_H = 88;

// Forecast row position and dimensions.
static constexpr int CARD_Y = 124;
static constexpr int CARD_W = 42;
static constexpr int CARD_H = 78;

// Center positions of the four forecast cards.
static const int cardCenterX[4] = {37, 92, 148, 203};

// Data retained while the animation is running.
static WeatherData animationWeather;
static int animationOldIndex = 0;
static int animationNewIndex = 0;

// =====================================================
// 2. FORECAST SPRITE
// =====================================================

// The sprite contains only the forecast row, not the entire screen.
// Dimensions: 220 x 87 pixels, RGB565, approximately 38 KB of RAM.
static TFT_eSprite forecastSprite(&tft);

static bool forecastSpriteReady = false;
static bool forecastSpriteAttempted = false;

// Initialize the sprite only once.
static bool initForecastSprite() {
    if (forecastSpriteReady) return true;
    if (forecastSpriteAttempted) return false;

    forecastSpriteAttempted = true;

    forecastSprite.setColorDepth(16);

    if (forecastSprite.createSprite(220, 87) == nullptr) {
        Serial.println("[WEATHER] Sprite allocation failed!");
        return false;
    }

    forecastSpriteReady = true;
    Serial.println("[WEATHER] Forecast sprite ready.");

    return true;
}

// =====================================================
// 3. ANIMATION HELPERS
// =====================================================

// Clamp a value to the range from 0 to 1.

void drawWeatherFaceData(const WeatherData& weather) {
    renderWeatherFaceData(weather, true);
}

// =====================================================
// 10. START FORECAST ANIMATION
// =====================================================

// Được gọi khi người dùng nhấn nút chuyển giờ dự báo.
/**
 * @brief Implements the advance weather forecast operation.
 * @param weather Input or state used by this operation.
 */
void advanceWeatherForecast(const WeatherData& weather) {
    if (!weather.valid || weatherAnimating) return;

    if (!forecastSelectionInitialized) {
        selectedForecastIndex = 0;
        forecastSelectionInitialized = true;
    }

    animationWeather = weather;
    animationOldIndex = selectedForecastIndex;
    animationNewIndex = (selectedForecastIndex + 1) % 24;

    animationStartedAt = millis();
    weatherAnimating = true;
}

// =====================================================
// 11. ANIMATION UPDATE
// =====================================================

// Cập nhật animation forecast.
/**
 * @brief Implements the update weather face animation operation.
 * @param weather Input or state used by this operation.
 */
void updateWeatherFaceAnimation(const WeatherData& weather) {
    (void)weather;

    if (weatherDetailsOpen) {
        if (weatherDetailsScrollAnimating) {
            const uint32_t duration = 260;
            float p = clamp01((float)(millis() - weatherDetailsScrollAt) / duration);
            float eased = easeOutCubic(p);
            // Animate only the list contents; do not clear/redraw the full display.
            drawWeatherDetailsRows((int)(14.0f * eased));
            if (p >= 1.0f) {
                weatherDetailsScroll = weatherDetailsScrollTo;
                weatherDetailsScrollAnimating = false;
                // Only refresh the list viewport; keep the header and panel stable.
                drawWeatherDetailsRows(0);
            }
        }
        return;
    }

    drawWeatherUpdateCountdown();
    if (!weatherAnimating) return;

    // Tính tiến trình animation.
    uint32_t elapsed = millis() - animationStartedAt;
    float progress = clamp01((float)elapsed / WEATHER_ANIMATION_MS);

    // Khi animation kết thúc, cập nhật panel và forecast.
    if (progress >= 1.0f) {
        selectedForecastIndex = animationNewIndex;
        weatherAnimating = false;
        drawWeatherFaceData(animationWeather);
        return;
    }

    // Phase 1: selected card gets a brief lift/highlight without scaling.
    constexpr float popPhaseEnd = 0.38f;
    if (progress < popPhaseEnd) {
        float popProgress = easeOutCubic(progress / popPhaseEnd);
        drawForecastRow(animationWeather, animationOldIndex);

        const HourlyForecast& selected =
            animationWeather.hourly[animationOldIndex];

        const int centerX = cardCenterX[0];
        const int w = CARD_W;
        const int h = CARD_H;
        const int x = centerX - (int)(47.0f * popProgress);
        const int y = CARD_Y;
        const uint16_t bg = 0x2184;

        // Redraw the selected card at its normal size with a small upward lift.
        tft.fillRoundRect(x, y, w, h, 11, bg);
        tft.drawRoundRect(x, y, w, h, 11, 0xFDE0);

        char temperature[8];
        snprintf(temperature, sizeof(temperature), "%d", selected.temp);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, bg);
        tft.drawString(temperature, centerX, y + 18, 1);
        const int degreeX = centerX + tft.textWidth(temperature) / 2 + 2;
        tft.drawCircle(degreeX, y + 15, 2, TFT_WHITE);
        tft.drawString("C", degreeX + 7, y + 18, 1);

        const bool isNight = selected.hour < 6 || selected.hour >= 18;
        drawWeatherMiniIcon(tft, centerX, y + h / 2 + 1,
                            selected.weatherCode, isNight);

        char timeLabel[8];
        snprintf(timeLabel, sizeof(timeLabel), "%02d:00", selected.hour);
        tft.setTextColor(TFT_WHITE, bg);
        tft.drawString(timeLabel, centerX, y + h - 11, 1);
        return;
    }

    // Phase 2: the next row enters horizontally from the right.
    // The selected card is no longer moved upward; it exits with the row transition.
    float slideProgress = easeInOutCubic(
        (progress - popPhaseEnd) / (1.0f - popPhaseEnd)
    );
    int slideOffset = WEATHER_SLIDE_DISTANCE -
        (int)(slideProgress * WEATHER_SLIDE_DISTANCE);

    if (initForecastSprite()) {
        drawForecastRowToSprite(animationWeather, animationNewIndex, slideOffset);
    } else {
        tft.fillRect(10, 120, 220, 90, TFT_BLACK);
        for (int i = 0; i < 4; i++) {
            int dataIndex = (animationNewIndex + i) % 24;
            int x = cardCenterX[i] + slideOffset;
            drawForecastCard(tft, animationWeather.hourly[dataIndex], x, CARD_Y, i == 0);
        }
    }
}
