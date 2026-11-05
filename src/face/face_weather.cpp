
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "icons.h"
#include "config.h"
#include <math.h>

extern TFT_eSPI tft;

extern int32_t getWeatherRefreshCountdownSeconds();

// Show the next automatic refresh countdown at the top of the face.
static int32_t lastCountdownDisplaySecond = INT32_MIN;

/**
 * @brief Displays the next automatic weather refresh countdown or the current Wi-Fi/update status.
 * @param force Input or state used by this operation.
 */
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
static constexpr int WEATHER_SLIDE_DISTANCE = 55;

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
static const int cardCenterX[4] = {37, 92, 147, 202};

// Data retained while the animation is running.
static WeatherData animationWeather;
static int animationOldIndex = 0;
static int animationNewIndex = 0;
static int animationLastRenderedShift = 0;

// =====================================================
// 2. DIRECT FORECAST RENDERING
// =====================================================

// =====================================================
// 3. ANIMATION HELPERS
// =====================================================

// Clamp a value to the range from 0 to 1.
/**
 * @brief Clamps an animation progress value to the inclusive range from zero to one.
 * @param value Input or state used by this operation.
 */
static float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

// Starts quickly and decelerates toward the end.
/**
 * @brief Calculates a cubic ease-out value for smooth deceleration.
 * @param t Input or state used by this operation.
 */
static float easeOutCubic(float t) {
    t = clamp01(t);
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

// Starts and ends slowly, with faster movement in the middle.
/**
 * @brief Calculates a cubic ease-in/ease-out value for smooth transitions.
 * @param t Input or state used by this operation.
 */
static float easeInOutCubic(float t) {
    t = clamp01(t);

    if (t < 0.5f) return 4.0f * t * t * t;

    float v = -2.0f * t + 2.0f;
    return 1.0f - (v * v * v) / 2.0f;
}

// =====================================================
// 4. TEMPERATURE DRAWING
// =====================================================

// Draw a temperature value with the Celsius symbol.
/**
 * @brief Draws a temperature value followed by the Celsius degree symbol.
 * @param temp Input or state used by this operation.
 * @param x Input or state used by this operation.
 * @param y Input or state used by this operation.
 * @param font Input or state used by this operation.
 * @param color Input or state used by this operation.
 * @param background Input or state used by this operation.
 */
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

// Draw the background of the upper weather panel.
/**
 * @brief Draws the background and border of the main weather panel.

 */
static void drawMainPanelBackground() {
    tft.fillRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 16, 0x1082);
    tft.drawRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 16, 0x3186);
}

// Draw weather information for a specific forecast hour.
/**
 * @brief Renders the current or selected hourly weather information in the main panel.
 * @param weather Input or state used by this operation.
 * @param index Input or state used by this operation.
 * @param offsetY Input or state used by this operation.
 */
static void drawMainWeatherContent(const WeatherData& weather, int index, int offsetY) {
    index = (index + 24) % 24;

    const HourlyForecast& forecast = weather.hourly[index];
    const bool isNight = forecast.hour < 6 || forecast.hour >= 18;
    const uint16_t panelColor = 0x1082;

    // Weather icon.
    drawWeatherIcon(tft, 52, 74 + offsetY, forecast.weatherCode, isNight);

    // Current or forecast temperature.
    int temperature = index == 0 ? weather.currentTemp : forecast.temp;
    drawTemperatureWithDegree(temperature, 90, 60 + offsetY, 4, TFT_WHITE, panelColor);

    // Daily maximum temperature.
    drawTemperatureWithDegree(weather.maxTemp, 175, 63 + offsetY, 2, 0xFDE0, panelColor);

    // Daily minimum temperature.
    drawTemperatureWithDegree(weather.minTemp, 175, 82 + offsetY, 2, 0x7BEF, panelColor);

    // Forecast hour.
    char timeLabel[8];
    snprintf(timeLabel, sizeof(timeLabel), "%02d:00", forecast.hour);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(0xFDE0, panelColor);
    tft.drawString(timeLabel, 110, 98 + offsetY, 1);
}

// =====================================================
// 6. FORECAST CARD
// =====================================================

// This function can render to either the main TFT or a TFT sprite.
/**
 * @brief Draws one hourly forecast card on the supplied display or sprite.
 * @param display Input or state used by this operation.
 * @param forecast Input or state used by this operation.
 * @param centerX Input or state used by this operation.
 * @param cardY Input or state used by this operation.
 * @param selected Input or state used by this operation.
 */
static void drawForecastCard(TFT_eSPI& display, const HourlyForecast& forecast, int centerX, int cardY, bool selected) {
    const int boxX = centerX - CARD_W / 2;

    const uint16_t background = selected ? 0x2184 : 0x1082;
    const uint16_t border = selected ? 0xFDE0 : 0x3186;

    // Card background and border..
    display.fillRoundRect(boxX, cardY, CARD_W, CARD_H, 10, background);
    display.drawRoundRect(boxX, cardY, CARD_W, CARD_H, 10, border);

    // Temperature value.
    char temperature[8];
    snprintf(temperature, sizeof(temperature), "%d", forecast.temp);

    display.setTextDatum(MC_DATUM);
    display.setTextColor(TFT_WHITE, background);
    display.drawString(temperature, centerX - 5, cardY + 17, 1);

    // Degree symbol..
    int degreeX = centerX + display.textWidth(temperature) / 2 + 2;
    display.drawCircle(degreeX - 5, cardY + 14, 2, TFT_WHITE);
    display.setTextColor(TFT_WHITE, background);
    display.drawString("C", degreeX + 2, cardY + 17, 1);

    // Weather icon.
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

// Render forecast cards directly to the TFT without allocating a full-row sprite.
/**
 * @brief Renders four forecast cards directly to the TFT with an optional horizontal offset.
 * @param weather Input or state used by this operation.
 * @param startIndex Input or state used by this operation.
 * @param slideOffset Input or state used by this operation.
 */
static void drawForecastRowDirect(const WeatherData& weather, int startIndex, int slideOffset = 0) {
    // Keep all card drawing inside the forecast strip. The viewport uses
    // local coordinates, which also clips cards as they enter/leave the row.
    tft.setViewport(10, 120, 220, 90, true);
    tft.fillRect(0, 0, 220, 90, TFT_BLACK);

    for (int i = 0; i < 4; ++i) {
        const int dataIndex = (startIndex + i) % 24;
        const int centerX = cardCenterX[i] - 10 + slideOffset;
        drawForecastCard(tft, weather.hourly[dataIndex], centerX, CARD_Y - 120, i == 0);
    }

    tft.resetViewport();
}

// Render the static forecast row when no animation is active.
static void drawForecastRow(const WeatherData& weather, int startIndex) {
    drawForecastRowDirect(weather, startIndex);
}

// =====================================================
// 8. FOOTER
// =====================================================

// The footer does not change during animation, so draw it only once.
/**
 * @brief Draws the static footer and button hint on the Weather screen.

 */
static void drawWeatherFooter() {
    tft.fillRect(0, 207, 240, 30, TFT_BLACK);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString("[Press: Next hour]", 120, 221, 1);
}

// =====================================================
// 8A. WEATHER DETAIL CARD
// =====================================================

/**
 * @brief Renders the scrollable rows in the weather details panel.
 * @param scrollOffset Input or state used by this operation.
 */
static void drawWeatherDetailsRows(int scrollOffset = 0) {
    const char* labels[] = {"Moonrise", "Moonset", "Sunrise", "Sunset", "Sunshine", "Daylight", "Wind 10m", "Pressure", "Rain"};

    // Use fixed buffers instead of constructing nine temporary Arduino Strings
    // on every scroll-animation frame. This avoids repeated heap allocation.
    char values[9][24];
    snprintf(values[0], sizeof(values[0]), "%s", detailWeather.moonrise);
    snprintf(values[1], sizeof(values[1]), "%s", detailWeather.moonset);
    snprintf(values[2], sizeof(values[2]), "%s", detailWeather.sunrise);
    snprintf(values[3], sizeof(values[3]), "%s", detailWeather.sunset);
    snprintf(values[4], sizeof(values[4]), "%.1f h", detailWeather.sunshineDuration / 3600.0f);
    snprintf(values[5], sizeof(values[5]), "%.1f h", detailWeather.daylightDuration / 3600.0f);
    snprintf(values[6], sizeof(values[6]), "%.1f km/h", detailWeather.windSpeed10m);
    snprintf(values[7], sizeof(values[7]), "%.0f hPa", detailWeather.surfacePressure);
    snprintf(values[8], sizeof(values[8]), "%.1f mm", detailWeather.rain);

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

/**
 * @brief Renders the weather details header, current temperature, lunar phase, and detail rows.
 * @param scrollOffset Input or state used by this operation.
 */
static void drawWeatherDetailsContent(int scrollOffset = 0) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, 0x1082);
    tft.drawString("WEATHER DETAILS", 32, 28, 2);
    drawTemperatureWithDegree(detailWeather.currentTemp, 34, 51, 4, TFT_WHITE, 0x1082);
    tft.setTextColor(0xFDE0, 0x1082);
    tft.drawString(detailWeather.moonPhase[0] != '\0' ? detailWeather.moonPhase : "Moon: N/A", 34, 82, 2);

    drawWeatherDetailsRows(scrollOffset);

    tft.setTextColor(0x7BEF, 0x1082);
    tft.drawCentreString("Press: scroll  Double: close", 120, 204, 1);
    tft.drawString("Powered by SunriseSunset.io", 31, 188, 1);
}

/**
 * @brief Clears the display and renders the complete weather details panel.

 */
static void drawWeatherDetails() {
    tft.fillScreen(TFT_BLACK);
    tft.fillRoundRect(DETAIL_PANEL_X, DETAIL_PANEL_Y, DETAIL_PANEL_W, DETAIL_PANEL_H, 15, 0x1082);
    tft.drawRoundRect(DETAIL_PANEL_X, DETAIL_PANEL_Y, DETAIL_PANEL_W, DETAIL_PANEL_H, 15, 0x7BEF);
    drawWeatherDetailsContent();
}

/**
 * @brief Opens or closes the detailed weather panel, including its opening transition.
 * @param weather Input or state used by this operation.
 */
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
        // Animate the panel from the first forecast card to the full detail panel.
        // Keep the normal weather face visible behind the expanding panel.
        weatherDetailsOpen = false;
        constexpr int frames = 18;
        for (int frame = 0; frame <= frames; ++frame) {
            const float progress = (float)frame / frames;
            const float eased = easeOutCubic(progress);

            const int x = 12 + (int)(4.0f * eased);
            const int y = 124 - (int)(104.0f * eased);
            const int w = 42 + (int)(158.0f * eased);
            const int h = 78 + (int)(122.0f * eased);
            const int radius = 5 + (int)(10.0f * eased);

            // Draw only the expanding panel. Do not redraw the weather face
            // here because drawWeatherFaceData() clears the whole screen.
            tft.fillRoundRect(x, y, w, h, radius, 0x1082);
            tft.drawRoundRect(x, y, w, h, radius, 0x7BEF);
            delay(16);
        }

        weatherDetailsOpen = true;

        // Clear the previous weather face after the transition, then render
        // the detail panel on a clean black background.
        tft.fillScreen(TFT_BLACK);
        tft.fillRoundRect(DETAIL_PANEL_X, DETAIL_PANEL_Y, DETAIL_PANEL_W, DETAIL_PANEL_H, 15, 0x1082);
        tft.drawRoundRect(DETAIL_PANEL_X, DETAIL_PANEL_Y, DETAIL_PANEL_W, DETAIL_PANEL_H, 15, 0x7BEF);
        drawWeatherDetailsContent();
    } else {
        // Closing is intentionally immediate. Avoid the per-frame full-face
        // redraw that caused ghost frames and SPI flicker on ESP32-C3.
        weatherDetailsScrollAnimating = false;
        drawWeatherFaceData(weather);
    }
}

/**
 * @brief Advances the weather details list and starts its scroll animation.
 * @param weather Input or state used by this operation.
 */
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

// Render the complete interface when the Weather screen is opened.
/**
 * @brief Renders the main Weather screen and its forecast cards.
 * @param weather Input or state used by this operation.
 * @param clearScreen Input or state used by this operation.
 */
static void renderWeatherFaceData(const WeatherData& weather, bool clearScreen) {
    weatherAnimating = false;
    if (weatherDetailsOpen) { detailWeather = weather; drawWeatherDetails(); return; }
    if (clearScreen) tft.fillScreen(TFT_BLACK);
    drawWeatherUpdateCountdown(true);

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

// Public renderer: normal screen transitions clear the display first.
/**
 * @brief Initializes and renders the Weather screen from a weather snapshot.
 * @param weather Input or state used by this operation.
 */
void drawWeatherFaceData(const WeatherData& weather) {
    renderWeatherFaceData(weather, true);
}

// =====================================================
// 10. START FORECAST ANIMATION
// =====================================================

// Called when the user presses the button to advance to the next forecast hour.
/**
 * @brief Starts the transition to the next hourly forecast card.
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
    animationLastRenderedShift = 0;
    weatherAnimating = true;
}

// =====================================================
// 11. ANIMATION UPDATE
// =====================================================

// Updates the forecast animation.
/**
 * @brief Advances forecast-card and detail-list animations and refreshes the countdown.
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

    // Calculate the animation progress.
    uint32_t elapsed = millis() - animationStartedAt;
    float progress = clamp01((float)elapsed / WEATHER_ANIMATION_MS);

    // Commit the selected forecast and redraw the panel when the animation completes.
    if (progress >= 1.0f) {
        selectedForecastIndex = animationNewIndex;
        weatherAnimating = false;
        drawWeatherFaceData(animationWeather);
        return;
    }

    const float slideProgress = easeInOutCubic(progress);
    const int shift = (int)(slideProgress * WEATHER_SLIDE_DISTANCE);
    const int delta = shift - animationLastRenderedShift;

    // Skip duplicate pixel positions. The display does not need another
    // redraw when the eased animation has not advanced by a whole pixel.
    if (delta <= 0) return;

    // Move the five-card strip without blanking the entire viewport.
    // As each card moves left, only its newly exposed right-hand edge is
    // erased; the card is then redrawn at its new position. This avoids the
    // full-width black flash produced by clearing all 220x90 pixels per frame.
    tft.startWrite();
    tft.setViewport(10, 120, 220, 90, true);

    const int cardY = CARD_Y - 120;
    const int eraseWidth = (delta < CARD_W) ? delta : CARD_W;

    for (int i = 0; i < 5; ++i) {
        const int dataIndex = (animationOldIndex + i) % 24;
        const int oldCenterX = cardCenterX[0] - 10 +
                               i * WEATHER_SLIDE_DISTANCE -
                               animationLastRenderedShift;
        const int newCenterX = oldCenterX - delta;

        // Clear only the part of the previous card that is no longer covered
        // by its new position. The viewport clips cards at both screen edges.
        const int eraseX = (delta < CARD_W)
                               ? oldCenterX + CARD_W / 2 - delta
                               : oldCenterX - CARD_W / 2;
        tft.fillRect(eraseX, cardY, eraseWidth, CARD_H, TFT_BLACK);

        drawForecastCard(tft, animationWeather.hourly[dataIndex],
                         newCenterX, cardY, i == 0);
    }

    animationLastRenderedShift = shift;
    tft.resetViewport();
    tft.endWrite();
}
