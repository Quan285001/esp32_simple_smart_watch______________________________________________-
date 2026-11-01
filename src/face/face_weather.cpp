
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
static bool weatherDetailsAnimating = false;
static bool weatherDetailsOpening = false;
static uint32_t weatherDetailsAnimationAt = 0;
static int weatherDetailsScroll = 0;
static WeatherData detailWeather;

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
static const int cardCenterX[5] = {22, 66, 110, 154, 198};

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

// Vẽ 5 thẻ forecast vào sprite rồi đẩy lên màn hình.
static void drawForecastRowToSprite(const WeatherData& weather, int startIndex, int slideOffset) {
    forecastSprite.fillSprite(TFT_BLACK);

    for (int i = 0; i < 5; i++) {
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

    for (int i = 0; i < 5; i++) {
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

static void drawWeatherDetails() {
    tft.fillScreen(TFT_BLACK);
    tft.fillRoundRect(20, 20, 200, 200, 15, 0x1082);
    tft.drawRoundRect(20, 20, 200, 200, 15, 0x7BEF);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, 0x1082);
    tft.drawString("WEATHER DETAILS", 32, 28, 2);

    drawTemperatureWithDegree(detailWeather.currentTemp, 34, 51, 4, TFT_WHITE, 0x1082);
    tft.setTextColor(0xFDE0, 0x1082);
    tft.drawString(detailWeather.moonPhase.length() ? detailWeather.moonPhase : "Moon: N/A", 34, 82, 2);

    const char* labels[] = {
        "Moonrise", "Moonset", "Sunrise", "Sunset",
        "Sunshine", "Daylight", "Wind 10m",
        "Pressure", "Rain"
    };
    String values[] = {
        detailWeather.moonrise, detailWeather.moonset,
        detailWeather.sunrise, detailWeather.sunset,
        String(detailWeather.sunshineDuration / 3600.0f, 1) + " h",
        String(detailWeather.daylightDuration / 3600.0f, 1) + " h",
        String(detailWeather.windSpeed10m, 1) + " km/h",
        String(detailWeather.surfacePressure, 0) + " hPa",
        String(detailWeather.rain, 1) + " mm"
    };

    tft.setTextColor(0xFFFF, 0x1082);
    for (int row = 0; row < 4; ++row) {
        int item = weatherDetailsScroll + row;
        if (item >= 9) break;
        int y = 111 + row * 17;
        tft.setTextColor(0x7BEF, 0x1082);
        tft.drawString(labels[item], 32, y, 1);
        tft.setTextColor(TFT_WHITE, 0x1082);
        tft.drawRightString(values[item].length() ? values[item] : "N/A", 207, y, 1);
    }

    tft.setTextColor(0x7BEF, 0x1082);
    tft.drawCentreString("Press: scroll  Double: close", 120, 204, 1);
    tft.setTextColor(0x7BEF, 0x1082);
    tft.drawString("Powered by SunriseSunset.io", 31, 188, 1);
}

void toggleWeatherDetails(const WeatherData& weather) {
    if (!weather.valid) return;
    if (weatherDetailsAnimating) return;
    weatherDetailsOpening = !weatherDetailsOpen;
    detailWeather = weather;
    if (weatherDetailsOpening) weatherDetailsScroll = 0;
    weatherDetailsAnimating = true;
    weatherDetailsAnimationAt = millis();
}

bool scrollWeatherDetails(const WeatherData& weather) {
    if (!weatherDetailsOpen) return false;
    weatherDetailsScroll = (weatherDetailsScroll + 1) % 6;
    detailWeather = weather;
    drawWeatherDetails();
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

    if (weatherDetailsAnimating) {
        const uint32_t duration = 260;
        float p = clamp01((float)(millis() - weatherDetailsAnimationAt) / duration);
        float eased = easeOutCubic(p);
        int size = weatherDetailsOpening ? (int)(200.0f * eased) : (int)(200.0f * (1.0f - eased));
        if (size < 2) size = 2;
        int x = 120 - size / 2;
        int y = 120 - size / 2;
        tft.fillScreen(TFT_BLACK);
        tft.fillRoundRect(x, y, size, size, 14, 0x1082);
        tft.drawRoundRect(x, y, size, size, 14, 0x7BEF);
        if (p >= 1.0f) {
            weatherDetailsAnimating = false;
            weatherDetailsOpen = weatherDetailsOpening;
            if (weatherDetailsOpen) drawWeatherDetails();
            else drawWeatherFaceData(detailWeather);
        }
        return;
    }

    if (weatherDetailsOpen) return;
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

    // Phase 1: the selected card rises toward the viewer.
    constexpr float liftPhaseEnd = 0.42f;
    if (progress < liftPhaseEnd) {
        float liftProgress = easeOutCubic(progress / liftPhaseEnd);
        drawForecastRow(animationWeather, animationOldIndex);
        int lift = (int)(24.0f * liftProgress);
        tft.fillRect(0, CARD_Y, 45, CARD_H, TFT_BLACK);
        drawForecastCard(tft, animationWeather.hourly[animationOldIndex],
                         cardCenterX[0], CARD_Y - lift, true);
        return;
    }

    // Phase 2: the next forecast row slides in after the selected card lifts.
    float slideProgress = easeInOutCubic((progress - liftPhaseEnd) / (1.0f - liftPhaseEnd));
    int slideOffset = WEATHER_SLIDE_DISTANCE - (int)(slideProgress * WEATHER_SLIDE_DISTANCE);

    if (initForecastSprite()) {
        drawForecastRowToSprite(animationWeather, animationNewIndex, slideOffset);
    } else {
        tft.fillRect(10, 120, 220, 90, TFT_BLACK);
        for (int i = 0; i < 5; i++) {
            int dataIndex = (animationNewIndex + i) % 24;
            int x = cardCenterX[i] + slideOffset;
            drawForecastCard(tft, animationWeather.hourly[dataIndex], x, CARD_Y, i == 0);
        }
    }
}
