
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "icons.h"
#include "config.h"
#include <math.h>

extern TFT_eSPI tft;

// =====================================================
// 1. WEATHER FACE STATE
// =====================================================

// Giờ dự báo đang được chọn.
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

// Trạng thái animation.
static bool weatherAnimating = false;
static uint32_t animationStartedAt = 0;

// Thời gian và khoảng cách trượt của animation.
static constexpr uint32_t WEATHER_ANIMATION_MS = 400;
static constexpr int WEATHER_SLIDE_DISTANCE = 47;

// Kích thước panel thời tiết.
static constexpr int PANEL_X = 15;
static constexpr int PANEL_Y = 30;
static constexpr int PANEL_W = 210;
static constexpr int PANEL_H = 88;

// Vị trí và kích thước hàng forecast.
static constexpr int CARD_Y = 124;
static constexpr int CARD_W = 42;
static constexpr int CARD_H = 78;

// Vị trí tâm của 4 thẻ dự báo.
static const int cardCenterX[4] = {37, 92, 148, 203};

// Dữ liệu được lưu lại trong lúc animation.
static WeatherData animationWeather;
static int animationOldIndex = 0;
static int animationNewIndex = 0;

// =====================================================
// 2. FORECAST SPRITE
// =====================================================

// Sprite chỉ chứa hàng forecast, không chứa toàn bộ màn hình.
// 220 x 87 pixel, RGB565, khoảng 38 KB RAM.
static TFT_eSprite forecastSprite(&tft);

static bool forecastSpriteReady = false;
static bool forecastSpriteAttempted = false;

// Khởi tạo sprite một lần duy nhất.
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

// Giới hạn giá trị trong khoảng 0 đến 1.
static float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

// Chuyển động nhanh lúc đầu và chậm dần khi kết thúc.
static float easeOutCubic(float t) {
    t = clamp01(t);
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

// Chuyển động chậm lúc đầu và cuối, nhanh ở giữa.
static float easeInOutCubic(float t) {
    t = clamp01(t);

    if (t < 0.5f) return 4.0f * t * t * t;

    float v = -2.0f * t + 2.0f;
    return 1.0f - (v * v * v) / 2.0f;
}

// =====================================================
// 4. TEMPERATURE DRAWING
// =====================================================

// Vẽ nhiệt độ kèm ký hiệu độ C.
static void drawTemperatureWithDegree(int temp, int x, int y, int font, uint16_t color, uint16_t background) {
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%d", temp);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(color, background);
    tft.drawString(buffer, x, y, font);

    int textWidth = tft.textWidth(buffer, font);
    int degreeX = x + textWidth + 3;
    int degreeY = y + (font >= 4 ? 5 : 3);
    int radius = font >= 4 ? 3 : 2;

    tft.drawCircle(degreeX + radius, degreeY, radius, color);

    int cX = degreeX + radius * 2 + 4;
    tft.drawString("C", cX, y, font);
}

// =====================================================
// 5. MAIN WEATHER PANEL
// =====================================================

// Vẽ nền panel phía trên.
static void drawMainPanelBackground() {
    tft.fillRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 16, 0x1082);
    tft.drawRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 16, 0x3186);
}

// Vẽ nội dung thời tiết của một giờ cụ thể.
static void drawMainWeatherContent(const WeatherData& weather, int index, int offsetY) {
    index = (index + 24) % 24;

    const HourlyForecast& forecast = weather.hourly[index];
    const bool isNight = forecast.hour < 6 || forecast.hour >= 18;
    const uint16_t panelColor = 0x1082;

    // Icon thời tiết.
    drawWeatherIcon(tft, 52, 74 + offsetY, forecast.weatherCode, isNight);

    // Nhiệt độ hiện tại hoặc nhiệt độ dự báo.
    int temperature = index == 0 ? weather.currentTemp : forecast.temp;
    drawTemperatureWithDegree(temperature, 90, 60 + offsetY, 4, TFT_WHITE, panelColor);

    // Nhiệt độ cao nhất.
    drawTemperatureWithDegree(weather.maxTemp, 175, 63 + offsetY, 2, 0xFDE0, panelColor);

    // Nhiệt độ thấp nhất.
    drawTemperatureWithDegree(weather.minTemp, 175, 82 + offsetY, 2, 0x7BEF, panelColor);

    // Giờ dự báo.
    char timeLabel[8];
    snprintf(timeLabel, sizeof(timeLabel), "%02d:00", forecast.hour);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(0xFDE0, panelColor);
    tft.drawString(timeLabel, 110, 98 + offsetY, 1);
}

// =====================================================
// 6. FORECAST CARD
// =====================================================

// Hàm này có thể vẽ lên TFT chính hoặc TFT Sprite.
static void drawForecastCard(TFT_eSPI& display, const HourlyForecast& forecast, int centerX, int cardY, bool selected) {
    const int boxX = centerX - CARD_W / 2;

    const uint16_t background = selected ? 0x2184 : 0x1082;
    const uint16_t border = selected ? 0xFDE0 : 0x3186;

    // Nền và viền thẻ.
    display.fillRoundRect(boxX, cardY, CARD_W, CARD_H, 10, background);
    display.drawRoundRect(boxX, cardY, CARD_W, CARD_H, 10, border);

    // Nhiệt độ.
    char temperature[8];
    snprintf(temperature, sizeof(temperature), "%d", forecast.temp);

    display.setTextDatum(MC_DATUM);
    display.setTextColor(TFT_WHITE, background);
    display.drawString(temperature, centerX, cardY + 17, 1);

    // Ký hiệu độ.
    int degreeX = centerX + display.textWidth(temperature) / 2 + 2;
    display.drawCircle(degreeX, cardY + 14, 2, TFT_WHITE);

    // Icon thời tiết.
    const bool isNight = forecast.hour < 6 || forecast.hour >= 18;
    drawWeatherMiniIcon(display, centerX, cardY + 42, forecast.weatherCode, isNight);

    // Thời gian.
    char timeLabel[8];
    snprintf(timeLabel, sizeof(timeLabel), "%02d:00", forecast.hour);

    display.setTextDatum(MC_DATUM);
    display.setTextColor(selected ? TFT_WHITE : 0x7BEF, background);
    display.drawString(timeLabel, centerX, cardY + 66, 1);
}

// =====================================================
// 7. FORECAST ROW RENDERING
// =====================================================

// Vẽ 4 thẻ forecast vào sprite rồi đẩy lên màn hình.
static void drawForecastRowToSprite(const WeatherData& weather, int startIndex, int slideOffset) {
    forecastSprite.fillSprite(TFT_BLACK);

    for (int i = 0; i < 4; i++) {
        int dataIndex = (startIndex + i) % 24;
        int centerX = cardCenterX[i] - 10 + slideOffset;

        // CARD_Y - 120 = 4, chuyển tọa độ sang hệ tọa độ sprite.
        drawForecastCard(forecastSprite, weather.hourly[dataIndex], centerX, 4, i == 0);
    }

    // Đẩy toàn bộ hàng forecast lên TFT trong một lần.
    forecastSprite.pushSprite(10, 120);
}

// Vẽ forecast tĩnh khi không có animation.
static void drawForecastRow(const WeatherData& weather, int startIndex) {
    if (initForecastSprite()) {
        drawForecastRowToSprite(weather, startIndex, 0);
        return;
    }

    // Phương án dự phòng nếu không đủ RAM tạo sprite.
    tft.fillRect(10, 120, 220, 90, TFT_BLACK);

    for (int i = 0; i < 4; i++) {
        int dataIndex = (startIndex + i) % 24;
        drawForecastCard(tft, weather.hourly[dataIndex], cardCenterX[i], CARD_Y, i == 0);
    }
}

// =====================================================
// 8. FOOTER
// =====================================================

// Footer không thay đổi trong animation nên chỉ vẽ một lần.
static void drawWeatherFooter() {
    tft.fillRect(0, 207, 240, 30, TFT_BLACK);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString("[Press: Next hour]", 120, 221, 1);
}

// =====================================================
// 8A. WEATHER DETAIL CARD
// =====================================================

static void drawWeatherDetailsRows(int scrollOffset = 0) {
    const char* labels[] = {"Moonrise", "Moonset", "Sunrise", "Sunset", "Sunshine", "Daylight", "Wind 10m", "Pressure", "Rain"};
    String values[] = {
        detailWeather.moonrise, detailWeather.moonset, detailWeather.sunrise, detailWeather.sunset,
        String(detailWeather.sunshineDuration / 3600.0f, 1) + " h",
        String(detailWeather.daylightDuration / 3600.0f, 1) + " h",
        String(detailWeather.windSpeed10m, 1) + " km/h",
        String(detailWeather.surfacePressure, 0) + " hPa",
        String(detailWeather.rain, 1) + " mm"
    };

    // Clear only the clipped list viewport; keep the panel and fixed header intact.
    tft.setViewport(28, 108, 184, 74, true);
    tft.fillRect(0, 0, 184, 74, 0x1082);

    for (int row = 0; row < 5; ++row) {
        int item = weatherDetailsScroll + row;
        if (item >= 9) break;
        int y = 3 + row * 14 - scrollOffset;
        tft.setTextColor(0x7BEF, 0x1082);
        tft.drawString(labels[item], 4, y, 1);
        tft.setTextColor(TFT_WHITE, 0x1082);
        tft.drawRightString(values[item], 179, y, 1);
    }

    tft.resetViewport();
}

static void drawWeatherDetailsContent(int scrollOffset = 0) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, 0x1082);
    tft.drawString("WEATHER DETAILS", 32, 28, 2);
    drawTemperatureWithDegree(detailWeather.currentTemp, 34, 51, 4, TFT_WHITE, 0x1082);
    tft.setTextColor(0xFDE0, 0x1082);
    tft.drawString(detailWeather.moonPhase.length() ? detailWeather.moonPhase : "Moon: N/A", 34, 82, 2);

    drawWeatherDetailsRows(scrollOffset);

    tft.setTextColor(0x7BEF, 0x1082);
    tft.drawCentreString("Press: scroll  Double: close", 120, 204, 1);
    tft.drawString("Powered by SunriseSunset.io", 31, 188, 1);
}

static void drawWeatherDetails() {
    tft.fillScreen(TFT_BLACK);
    tft.fillRoundRect(DETAIL_PANEL_X, DETAIL_PANEL_Y, DETAIL_PANEL_W, DETAIL_PANEL_H, 15, 0x1082);
    tft.drawRoundRect(DETAIL_PANEL_X, DETAIL_PANEL_Y, DETAIL_PANEL_W, DETAIL_PANEL_H, 15, 0x7BEF);
    drawWeatherDetailsContent();
}

void toggleWeatherDetails(const WeatherData& weather) {
    if (!weather.valid || !weather.detailsValid) return;

    detailWeather = weather;
    weatherDetailsOpen = !weatherDetailsOpen;

    if (weatherDetailsOpen) {
        weatherDetailsScroll = 0;
        weatherDetailsScrollAnimating = false;

        // Run the opening transition here, directly from the double-press
        // handler. This guarantees frames are rendered even if the main-loop
        // animation scheduler is delayed by button/event processing.
        constexpr int frames = 18;
        for (int frame = 0; frame <= frames; ++frame) {
            const float progress = (float)frame / frames;
            const float eased = easeOutCubic(progress);

            const int x = 16 - (int)(4.0f * eased);
            const int y = 124 - (int)(104.0f * eased);
            const int w = 42 + (int)(158.0f * eased);
            const int h = 78 + (int)(122.0f * eased);
            const int radius = 5 + (int)(10.0f * eased);

            tft.fillRoundRect(x, y, w, h, radius, 0x1082);
            tft.drawRoundRect(x, y, w, h, radius, 0x7BEF);

            delay(16);
        }

        // Clear the previous weather face after the transition, then render
        // the detail panel on a clean black background.
        tft.fillScreen(TFT_BLACK);
        tft.fillRoundRect(DETAIL_PANEL_X, DETAIL_PANEL_Y, DETAIL_PANEL_W, DETAIL_PANEL_H, 15, 0x1082);
        tft.drawRoundRect(DETAIL_PANEL_X, DETAIL_PANEL_Y, DETAIL_PANEL_W, DETAIL_PANEL_H, 15, 0x7BEF);
        drawWeatherDetailsContent();
    } else {
        weatherDetailsScrollAnimating = false;

        // Restore the weather face as the background, then collapse the panel
        // toward the first forecast card. Redrawing the background each frame
        // prevents remnants of the larger panel from being left on screen.
        constexpr int frames = 18;
        for (int frame = 0; frame <= frames; ++frame) {
            const float progress = (float)frame / frames;
            const float eased = easeInOutCubic(progress);

            const int x = 16 - (int)(4.0f * eased);
            const int y = 124 - (int)(104.0f * eased);
            const int w = 42 + (int)(158.0f * eased);
            const int h = 78 + (int)(122.0f * eased);
            const int radius = 5 + (int)(10.0f * eased);

            // Reverse the opening geometry: start full-size, end at the card.
            const float reverse = 1.0f - eased;
            const int closeX = 16 - (int)(4.0f * reverse);
            const int closeY = 124 - (int)(104.0f * reverse);
            const int closeW = 42 + (int)(158.0f * reverse);
            const int closeH = 78 + (int)(122.0f * reverse);
            const int closeRadius = 5 + (int)(10.0f * reverse);
            (void)x; (void)y; (void)w; (void)h; (void)radius;

            drawWeatherFaceData(weather);
            tft.fillRoundRect(closeX, closeY, closeW, closeH, closeRadius, 0x1082);
            tft.drawRoundRect(closeX, closeY, closeW, closeH, closeRadius, 0x7BEF);
            delay(16);
        }

        drawWeatherFaceData(weather);
    }
}

bool scrollWeatherDetails(const WeatherData& weather) {
    if (!weatherDetailsOpen) return false;
    if (weatherDetailsScrollAnimating) return true;

    detailWeather = weather;

    // Nine rows are shown five at a time. After the final page (Rain),
    // the next press returns to the first page (Moonrise).
    if (weatherDetailsScroll >= 4) {
        weatherDetailsScroll = 0;
        drawWeatherDetails();
        return true;
    }

    weatherDetailsScrollFrom = weatherDetailsScroll;
    weatherDetailsScrollTo = weatherDetailsScroll + 1;
    weatherDetailsScrollAt = millis();
    weatherDetailsScrollAnimating = true;
    return true;
}

// =====================================================
// 9. WEATHER SCREEN INITIALIZATION
// =====================================================

// Vẽ toàn bộ giao diện khi mở màn hình Weather.
void drawWeatherFaceData(const WeatherData& weather) {
    weatherAnimating = false;
    if (weatherDetailsOpen) { detailWeather = weather; drawWeatherDetails(); return; }
    tft.fillScreen(TFT_BLACK);

    if (!weather.valid) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("Weather unavailable", 120, 115, 2);

        tft.setTextColor(0x7BEF, TFT_BLACK);
        tft.drawString("Waiting for connection", 120, 140, 1);
        return;
    }

    if (!forecastSelectionInitialized) {
        selectedForecastIndex = 0;
        forecastSelectionInitialized = true;
    }

    selectedForecastIndex = (selectedForecastIndex + 24) % 24;

    drawMainPanelBackground();
    drawMainWeatherContent(weather, selectedForecastIndex, 0);
    drawForecastRow(weather, selectedForecastIndex);
    drawWeatherFooter();
}

// =====================================================
// 10. START FORECAST ANIMATION
// =====================================================

// Được gọi khi người dùng nhấn nút chuyển giờ dự báo.
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
