// face_clock.cpp
//
// Built-in analog face renderer:
// - A persistent 160x160 static background cache.
// - A 64x64 RGB565 working tile, reused for every partial update.
// - Dirty tiles are derived from old/new hand bounds and changed text fields.
// - Every dirty tile is restored from the cache, then all dynamic layers are
//   recomposed in the original z-order before that tile is sent to the GC9A01.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>
#include <time.h>
#include <WiFi.h>
#include <string.h>
#include "icons.h"
#include "../services/ble_service.h"

extern TFT_eSPI tft;

namespace {
constexpr int16_t FACE_SIZE = 160;
constexpr int16_t FACE_ORIGIN_X = 40;
constexpr int16_t FACE_ORIGIN_Y = 40;
constexpr int16_t FACE_CENTER = 80;
constexpr int16_t TILE_SIZE = 64;
constexpr int8_t TILE_COLUMNS = (FACE_SIZE + TILE_SIZE - 1) / TILE_SIZE;
constexpr int8_t TILE_ROWS = TILE_COLUMNS;

constexpr uint16_t COLOR_BG = 0x0000;
constexpr uint16_t COLOR_INNER_BG = 0x18C3;
constexpr uint16_t COLOR_HAND = 0xFFFF;
constexpr uint16_t COLOR_SEC_HAND = 0xFBE0;
constexpr uint16_t COLOR_TEXT_MAIN = 0xFFFF;
constexpr uint16_t COLOR_TEXT_ACCENT = 0xFBE0;

TFT_eSprite backgroundCache(&tft);
TFT_eSprite workingTile(&tft);
bool backgroundCacheReady = false;
bool workingTileReady = false;
bool rendererReady = false;
bool fullRefreshPending = true;

struct HandGeometry {
    int16_t hourX;
    int16_t hourY;
    int16_t minuteX;
    int16_t minuteY;
    int16_t secondX;
    int16_t secondY;
    int16_t secondBackX;
    int16_t secondBackY;
};

HandGeometry previousHands{};
bool previousHandsValid = false;

char previousTimeText[9] = "";
char previousDayText[4] = "";
int previousDate = -1;
char previousConnectionLabel[12] = "";
int previousIconHour = -1;

void drawSmartWeatherIconSpr(TFT_eSPI& target, int currentHour, int x, int y) {
    const bool isNight = (currentHour < 6 || currentHour >= 18);
    if (isNight) {
        drawMiniCrescentMoon(target, x, y, COLOR_TEXT_ACCENT, COLOR_INNER_BG);
    } else {
        drawMiniSun(target, x, y, COLOR_TEXT_ACCENT);
    }
}

void drawStaticBackground() {
    backgroundCache.fillSprite(COLOR_BG);
    backgroundCache.fillCircle(FACE_CENTER, FACE_CENTER, 78, COLOR_INNER_BG);
    backgroundCache.setTextDatum(MC_DATUM);
    backgroundCache.setTextColor(COLOR_TEXT_ACCENT, COLOR_INNER_BG);
    backgroundCache.drawString("UTC+7", FACE_CENTER - 40, FACE_CENTER - 10, 1);
}

void markDirtyRect(bool dirty[TILE_ROWS][TILE_COLUMNS],
                   int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    const int left = (x < 0) ? 0 : x;
    const int top = (y < 0) ? 0 : y;
    const int right = (x + w > FACE_SIZE) ? FACE_SIZE : x + w;
    const int bottom = (y + h > FACE_SIZE) ? FACE_SIZE : y + h;
    if (left >= right || top >= bottom) return;

    const int firstColumn = left / TILE_SIZE;
    const int lastColumn = (right - 1) / TILE_SIZE;
    const int firstRow = top / TILE_SIZE;
    const int lastRow = (bottom - 1) / TILE_SIZE;
    for (int row = firstRow; row <= lastRow; ++row) {
        for (int column = firstColumn; column <= lastColumn; ++column) {
            dirty[row][column] = true;
        }
    }
}

void markHandDirty(bool dirty[TILE_ROWS][TILE_COLUMNS],
                   int x0, int y0, int x1, int y1, int padding) {
    const int left = (x0 < x1 ? x0 : x1) - padding;
    const int top = (y0 < y1 ? y0 : y1) - padding;
    const int right = (x0 > x1 ? x0 : x1) + padding + 1;
    const int bottom = (y0 > y1 ? y0 : y1) + padding + 1;
    markDirtyRect(dirty, left, top, right - left, bottom - top);
}

void markHandsDirty(bool dirty[TILE_ROWS][TILE_COLUMNS],
                    const HandGeometry& hands) {
    markHandDirty(dirty, FACE_CENTER, FACE_CENTER,
                  hands.hourX, hands.hourY, 2);
    markHandDirty(dirty, FACE_CENTER, FACE_CENTER,
                  hands.minuteX, hands.minuteY, 2);
    markHandDirty(dirty, hands.secondBackX, hands.secondBackY,
                  hands.secondX, hands.secondY, 2);
    markDirtyRect(dirty, FACE_CENTER - 6, FACE_CENTER - 6, 12, 12);
}

void drawHands(TFT_eSprite& target, int offsetX, int offsetY,
               const HandGeometry& hands) {
    const int cx = FACE_CENTER - offsetX;
    const int cy = FACE_CENTER - offsetY;
    const int hx = hands.hourX - offsetX;
    const int hy = hands.hourY - offsetY;
    const int mx = hands.minuteX - offsetX;
    const int my = hands.minuteY - offsetY;
    const int sx = hands.secondX - offsetX;
    const int sy = hands.secondY - offsetY;
    const int sbx = hands.secondBackX - offsetX;
    const int sby = hands.secondBackY - offsetY;

    // Preserve the original layer order: hour, minute, second, centre cap.
    for (int offset = -1; offset <= 1; ++offset) {
        target.drawLine(cx + offset, cy, hx + offset, hy, COLOR_HAND);
        target.drawLine(cx, cy + offset, hx, hy + offset, COLOR_HAND);
    }
    for (int offset = -1; offset <= 1; ++offset) {
        target.drawLine(cx + offset, cy, mx + offset, my, COLOR_HAND);
        target.drawLine(cx, cy + offset, mx, my + offset, COLOR_HAND);
    }
    target.drawLine(sbx, sby, sx, sy, COLOR_SEC_HAND);
    target.drawLine(sbx + 1, sby, sx + 1, sy, COLOR_SEC_HAND);
    target.fillCircle(cx, cy, 5, COLOR_SEC_HAND);
    target.fillCircle(cx, cy, 2, COLOR_TEXT_MAIN);
}

void drawDynamicLayers(TFT_eSprite& target, int tileX, int tileY,
                       const tm& timeinfo, const char* timeText,
                       const char* dayText, int date,
                       const char* connectionLabel,
                       const HandGeometry& hands) {
    const int cx = FACE_CENTER - tileX;
    const int cy = FACE_CENTER - tileY;

    target.setTextDatum(MC_DATUM);
    target.setTextColor(COLOR_TEXT_MAIN, COLOR_INNER_BG);
    target.drawString(timeText, cx - 40, cy + 6, 1);

    target.setTextColor(COLOR_TEXT_ACCENT, COLOR_INNER_BG);
    target.drawString(dayText, cx + 40, cy - 10, 1);
    target.setTextColor(COLOR_TEXT_MAIN, COLOR_INNER_BG);
    target.drawNumber(date, cx + 40, cy + 6, 2);

    drawSmartWeatherIconSpr(target, timeinfo.tm_hour, cx, cy + 38);

    target.setTextColor(COLOR_TEXT_ACCENT, COLOR_INNER_BG);
    target.drawString(connectionLabel, cx, cy + 50, 1);

    drawHands(target, tileX, tileY, hands);
}

void copyBackgroundTile(int tileX, int tileY, int tileWidth, int tileHeight) {
    const uint16_t* source =
        static_cast<const uint16_t*>(backgroundCache.getPointer());
    uint16_t* destination =
        static_cast<uint16_t*>(workingTile.getPointer());
    if (source == nullptr || destination == nullptr) return;

    for (int row = 0; row < tileHeight; ++row) {
        const uint16_t* sourceRow =
            source + (tileY + row) * FACE_SIZE + tileX;
        uint16_t* destinationRow = destination + row * TILE_SIZE;
        memcpy(destinationRow, sourceRow,
               static_cast<size_t>(tileWidth) * sizeof(uint16_t));
    }
}

void drawFallbackDirect(const tm& timeinfo, const char* timeText,
                        const char* dayText, int date,
                        const char* connectionLabel,
                        const HandGeometry& hands) {
    // Low-memory fallback: render directly if either sprite allocation fails.
    tft.fillRect(FACE_ORIGIN_X, FACE_ORIGIN_Y, FACE_SIZE, FACE_SIZE, COLOR_BG);
    tft.fillCircle(FACE_ORIGIN_X + FACE_CENTER, FACE_ORIGIN_Y + FACE_CENTER,
                   78, COLOR_INNER_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COLOR_TEXT_ACCENT, COLOR_INNER_BG);
    tft.drawString("UTC+7", FACE_ORIGIN_X + FACE_CENTER - 40,
                   FACE_ORIGIN_Y + FACE_CENTER - 10, 1);
    tft.setTextColor(COLOR_TEXT_MAIN, COLOR_INNER_BG);
    tft.drawString(timeText, FACE_ORIGIN_X + FACE_CENTER - 40,
                   FACE_ORIGIN_Y + FACE_CENTER + 6, 1);
    tft.setTextColor(COLOR_TEXT_ACCENT, COLOR_INNER_BG);
    tft.drawString(dayText, FACE_ORIGIN_X + FACE_CENTER + 40,
                   FACE_ORIGIN_Y + FACE_CENTER - 10, 1);
    tft.setTextColor(COLOR_TEXT_MAIN, COLOR_INNER_BG);
    tft.drawNumber(date, FACE_ORIGIN_X + FACE_CENTER + 40,
                   FACE_ORIGIN_Y + FACE_CENTER + 6, 2);
    drawSmartWeatherIconSpr(tft, timeinfo.tm_hour,
                            FACE_ORIGIN_X + FACE_CENTER,
                            FACE_ORIGIN_Y + FACE_CENTER + 38);
    tft.setTextColor(COLOR_TEXT_ACCENT, COLOR_INNER_BG);
    tft.drawString(connectionLabel, FACE_ORIGIN_X + FACE_CENTER,
                   FACE_ORIGIN_Y + FACE_CENTER + 50, 1);

    const int cx = FACE_ORIGIN_X + FACE_CENTER;
    const int cy = FACE_ORIGIN_Y + FACE_CENTER;
    const int hx = FACE_ORIGIN_X + hands.hourX;
    const int hy = FACE_ORIGIN_Y + hands.hourY;
    const int mx = FACE_ORIGIN_X + hands.minuteX;
    const int my = FACE_ORIGIN_Y + hands.minuteY;
    const int sx = FACE_ORIGIN_X + hands.secondX;
    const int sy = FACE_ORIGIN_Y + hands.secondY;
    const int sbx = FACE_ORIGIN_X + hands.secondBackX;
    const int sby = FACE_ORIGIN_Y + hands.secondBackY;
    for (int offset = -1; offset <= 1; ++offset) {
        tft.drawLine(cx + offset, cy, hx + offset, hy, COLOR_HAND);
        tft.drawLine(cx, cy + offset, hx, hy + offset, COLOR_HAND);
    }
    for (int offset = -1; offset <= 1; ++offset) {
        tft.drawLine(cx + offset, cy, mx + offset, my, COLOR_HAND);
        tft.drawLine(cx, cy + offset, mx, my + offset, COLOR_HAND);
    }
    tft.drawLine(sbx, sby, sx, sy, COLOR_SEC_HAND);
    tft.drawLine(sbx + 1, sby, sx + 1, sy, COLOR_SEC_HAND);
    tft.fillCircle(cx, cy, 5, COLOR_SEC_HAND);
    tft.fillCircle(cx, cy, 2, COLOR_TEXT_MAIN);
}

HandGeometry calculateHands(const tm& timeinfo, int millisValue) {
    const float exactSeconds = timeinfo.tm_sec + (millisValue / 1000.0f);
    const float exactMinutes = timeinfo.tm_min + (exactSeconds / 60.0f);
    const float exactHours = (timeinfo.tm_hour % 12) + (exactMinutes / 60.0f);

    const float angleHour = (exactHours * 30.0f - 90.0f) * (PI / 180.0f);
    const float angleMinute = (exactMinutes * 6.0f - 90.0f) * (PI / 180.0f);
    const float angleSecond = (exactSeconds * 6.0f - 90.0f) * (PI / 180.0f);

    HandGeometry hands{};
    hands.hourX = FACE_CENTER + static_cast<int>(40 * cosf(angleHour));
    hands.hourY = FACE_CENTER + static_cast<int>(40 * sinf(angleHour));
    hands.minuteX = FACE_CENTER + static_cast<int>(68 * cosf(angleMinute));
    hands.minuteY = FACE_CENTER + static_cast<int>(68 * sinf(angleMinute));
    hands.secondX = FACE_CENTER + static_cast<int>(90 * cosf(angleSecond));
    hands.secondY = FACE_CENTER + static_cast<int>(90 * sinf(angleSecond));
    hands.secondBackX = FACE_CENTER - static_cast<int>(18 * cosf(angleSecond));
    hands.secondBackY = FACE_CENTER - static_cast<int>(18 * sinf(angleSecond));
    return hands;
}

bool sameHands(const HandGeometry& a, const HandGeometry& b) {
    return a.hourX == b.hourX && a.hourY == b.hourY &&
           a.minuteX == b.minuteX && a.minuteY == b.minuteY &&
           a.secondX == b.secondX && a.secondY == b.secondY &&
           a.secondBackX == b.secondBackX && a.secondBackY == b.secondBackY;
}
} // namespace

/**
 * @brief Renders the built-in analog clock using cached background and
 *        bounded partial updates instead of clearing and pushing a full sprite.
 */
void drawAnalogClockFace(struct tm timeinfo, int millisValue,
                         int heartRate, int steps) {
    (void)heartRate;
    (void)steps;

    char timeText[9];
    snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    static const char* days[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
    };
    const int wday = (timeinfo.tm_wday >= 0 && timeinfo.tm_wday < 7)
        ? timeinfo.tm_wday : 0;
    const int date = timeinfo.tm_mday > 0 ? timeinfo.tm_mday : 1;

    const bool bluetoothConnected = isBLEConnected();
    const bool wifiConnected = WiFi.status() == WL_CONNECTED;
    const char* connectionLabel = "OFFLINE";
    if (bluetoothConnected && wifiConnected) connectionLabel = "BT + Wi-Fi";
    else if (bluetoothConnected) connectionLabel = "Bluetooth";
    else if (wifiConnected) connectionLabel = "Wi-Fi";

    const HandGeometry hands = calculateHands(timeinfo, millisValue);

    if (!backgroundCacheReady) {
        backgroundCache.setColorDepth(16);
        backgroundCacheReady = backgroundCache.createSprite(
            FACE_SIZE, FACE_SIZE) != nullptr;
        if (backgroundCacheReady) drawStaticBackground();
    }
    if (!workingTileReady) {
        workingTile.setColorDepth(16);
        workingTileReady = workingTile.createSprite(
            TILE_SIZE, TILE_SIZE) != nullptr;
    }
    rendererReady = backgroundCacheReady && workingTileReady;

    if (!rendererReady) {
        drawFallbackDirect(timeinfo, timeText, days[wday], date,
                           connectionLabel, hands);
        previousHands = hands;
        previousHandsValid = true;
        strncpy(previousTimeText, timeText, sizeof(previousTimeText) - 1);
        previousTimeText[sizeof(previousTimeText) - 1] = '\0';
        strncpy(previousDayText, days[wday], sizeof(previousDayText) - 1);
        previousDayText[sizeof(previousDayText) - 1] = '\0';
        previousDate = date;
        strncpy(previousConnectionLabel, connectionLabel,
                sizeof(previousConnectionLabel) - 1);
        previousConnectionLabel[sizeof(previousConnectionLabel) - 1] = '\0';
        previousIconHour = timeinfo.tm_hour;
        fullRefreshPending = true;
        return;
    }

    bool dirty[TILE_ROWS][TILE_COLUMNS] = {};
    if (fullRefreshPending || !previousHandsValid) {
        for (int row = 0; row < TILE_ROWS; ++row) {
            for (int column = 0; column < TILE_COLUMNS; ++column) {
                dirty[row][column] = true;
            }
        }
    } else {
        if (!sameHands(previousHands, hands)) {
            markHandsDirty(dirty, previousHands);
            markHandsDirty(dirty, hands);
        }
        if (strcmp(previousTimeText, timeText) != 0) {
            markDirtyRect(dirty, 34, 78, 56, 18);
        }
        if (strcmp(previousDayText, days[wday]) != 0 || previousDate != date) {
            markDirtyRect(dirty, 104, 64, 52, 30);
        }
        if (strcmp(previousConnectionLabel, connectionLabel) != 0) {
            markDirtyRect(dirty, 40, 122, 80, 18);
        }
        if (previousIconHour < 0 ||
            ((previousIconHour < 6 || previousIconHour >= 18) !=
             (timeinfo.tm_hour < 6 || timeinfo.tm_hour >= 18))) {
            markDirtyRect(dirty, 68, 108, 24, 24);
        }
    }

    for (int row = 0; row < TILE_ROWS; ++row) {
        for (int column = 0; column < TILE_COLUMNS; ++column) {
            if (!dirty[row][column]) continue;

            const int tileX = column * TILE_SIZE;
            const int tileY = row * TILE_SIZE;
            const int tileWidth =
                (FACE_SIZE - tileX < TILE_SIZE) ? FACE_SIZE - tileX : TILE_SIZE;
            const int tileHeight =
                (FACE_SIZE - tileY < TILE_SIZE) ? FACE_SIZE - tileY : TILE_SIZE;

            copyBackgroundTile(tileX, tileY, tileWidth, tileHeight);
            drawDynamicLayers(workingTile, tileX, tileY, timeinfo,
                              timeText, days[wday], date,
                              connectionLabel, hands);
            workingTile.pushSprite(
                FACE_ORIGIN_X + tileX, FACE_ORIGIN_Y + tileY,
                0, 0, tileWidth, tileHeight);
        }
    }

    previousHands = hands;
    previousHandsValid = true;
    strncpy(previousTimeText, timeText, sizeof(previousTimeText) - 1);
    previousTimeText[sizeof(previousTimeText) - 1] = '\0';
    strncpy(previousDayText, days[wday], sizeof(previousDayText) - 1);
    previousDayText[sizeof(previousDayText) - 1] = '\0';
    previousDate = date;
    strncpy(previousConnectionLabel, connectionLabel,
            sizeof(previousConnectionLabel) - 1);
    previousConnectionLabel[sizeof(previousConnectionLabel) - 1] = '\0';
    previousIconHour = timeinfo.tm_hour;
    fullRefreshPending = false;
}
