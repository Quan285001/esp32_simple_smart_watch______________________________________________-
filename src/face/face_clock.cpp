//face_clock.cpp

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>
#include <time.h>
#include <WiFi.h>
#include "../services/ble_service.h"
#include "icons.h"

extern TFT_eSPI tft;

static TFT_eSprite sprFace = TFT_eSprite(&tft);
static bool sprFaceCreated = false;

#define COLOR_BG        0x0000
#define COLOR_INNER_BG  0x18C3
#define COLOR_HAND      0xFFFF
#define COLOR_SEC_HAND  0xFBE0
#define COLOR_TEXT_MAIN 0xFFFF
#define COLOR_TEXT_ACCENT 0xFBE0

/**
 * @brief Draws a sun or crescent-moon icon according to the current hour.
 * @param currentHour Input or state used by this operation.
 * @param x Input or state used by this operation.
 * @param y Input or state used by this operation.
 */
void drawSmartWeatherIconSpr(int currentHour, int x, int y) {
    bool isNight = (currentHour < 6 || currentHour >= 18);
    if (isNight) {
        drawMiniCrescentMoon(sprFace, x, y, COLOR_TEXT_ACCENT);
    } else {
        drawMiniSun(sprFace, x, y, COLOR_TEXT_ACCENT);
    }
}

/**
 * @brief Renders the analog clock face and its animated clock hands.
 * @param timeinfo Input or state used by this operation.
 * @param millisValue Input or state used by this operation.
 * @param heartRate Input or state used by this operation.
 * @param steps Input or state used by this operation.
 */
/**
 * @brief Renders the analog clock face and its clock hands.
 * @param timeinfo Input or state used by this operation.
 * @param millisValue Input or state used by this operation.
 * @param heartRate Input or state used by this operation.
 * @param steps Input or state used by this operation.
 */
/**
 * @brief Renders the analog clock face, including the date, weather indicator, and animated hands.
 * @param timeinfo Local calendar time used to position the clock hands and render the date.
 * @param millisValue Milliseconds elapsed within the current second for smooth second-hand motion.
 * @param heartRate Reserved value; currently unused by this clock face.
 * @param steps Reserved value; currently unused by this clock face.
 */
void drawAnalogClockFace(struct tm timeinfo, int millisValue, int heartRate, int steps) {
    (void)heartRate;
    (void)steps;

    // Initialize the sprite safely.
    if (!sprFaceCreated) {
        sprFace.setColorDepth(16);
        if (sprFace.createSprite(160, 160)) {
            sprFaceCreated = true;
        } else {
            return; // Exit safely if there is not enough RAM.
        }
    }

    sprFace.fillSprite(COLOR_BG);

    int cx = 80;
    int cy = 80;

    sprFace.fillCircle(cx, cy, 78, COLOR_INNER_BG);

    sprFace.setTextDatum(MC_DATUM);
    sprFace.setTextColor(COLOR_TEXT_ACCENT, COLOR_INNER_BG);
    sprFace.drawString("UTC+7", cx - 40, cy - 10, 1);
    // Show the full local time in 24-hour format, including seconds.
    char localTimeText[9];
    snprintf(localTimeText, sizeof(localTimeText), "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    sprFace.setTextColor(COLOR_TEXT_MAIN, COLOR_INNER_BG);
    sprFace.drawString(localTimeText, cx - 40, cy + 6, 1);

    const char* days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    int wday = (timeinfo.tm_wday >= 0 && timeinfo.tm_wday < 7) ? timeinfo.tm_wday : 0;
    sprFace.setTextColor(COLOR_TEXT_ACCENT, COLOR_INNER_BG);
    sprFace.drawString(days[wday], cx + 40, cy - 10, 1);
    sprFace.setTextColor(COLOR_TEXT_MAIN, COLOR_INNER_BG);
    sprFace.drawNumber(timeinfo.tm_mday > 0 ? timeinfo.tm_mday : 1, cx + 40, cy + 6, 2);

    drawSmartWeatherIconSpr(timeinfo.tm_hour, cx, cy + 38);

    // Show active connections only; BLE advertising alone is not connected.
    const bool bluetoothConnected = isBLEConnected();
    const bool wifiConnected = WiFi.status() == WL_CONNECTED;
    const char* connectionLabel = "OFFLINE";
    if (bluetoothConnected && wifiConnected) {
        connectionLabel = "BT + Wi-Fi";
    } else if (bluetoothConnected) {
        connectionLabel = "Bluetooth";
    } else if (wifiConnected) {
        connectionLabel = "Wi-Fi";
    }
    sprFace.setTextColor(COLOR_TEXT_ACCENT, COLOR_INNER_BG);
    sprFace.drawString(connectionLabel, cx, cy + 50, 1);

    float exactSeconds = timeinfo.tm_sec + (millisValue / 1000.0f);
    float exactMinutes = timeinfo.tm_min + (exactSeconds / 60.0f);
    float exactHours   = (timeinfo.tm_hour % 12) + (exactMinutes / 60.0f);

    float angleHour = (exactHours * 30.0f - 90.0f) * (PI / 180.0f);
    float angleMin  = (exactMinutes * 6.0f - 90.0f) * (PI / 180.0f);
    float angleSec  = (exactSeconds * 6.0f - 90.0f) * (PI / 180.0f);

    int hx = cx + (int)(40 * cos(angleHour));
    int hy = cy + (int)(40 * sin(angleHour));
    for (int offset = -1; offset <= 1; offset++) {
        sprFace.drawLine(cx + offset, cy, hx + offset, hy, COLOR_HAND);
        sprFace.drawLine(cx, cy + offset, hx, hy + offset, COLOR_HAND);
    }

    int mx = cx + (int)(68 * cos(angleMin));
    int my = cy + (int)(68 * sin(angleMin));
    for (int offset = -1; offset <= 1; offset++) {
        sprFace.drawLine(cx + offset, cy, mx + offset, my, COLOR_HAND);
        sprFace.drawLine(cx, cy + offset, mx, my + offset, COLOR_HAND);
    }

    int sx = cx + (int)(90 * cos(angleSec));
    int sy = cy + (int)(90 * sin(angleSec));
    int sx_back = cx - (int)(18 * cos(angleSec));
    int sy_back = cy - (int)(18 * sin(angleSec));
    sprFace.drawLine(sx_back, sy_back, sx, sy, COLOR_SEC_HAND);
    sprFace.drawLine(sx_back + 1, sy_back, sx + 1, sy, COLOR_SEC_HAND);

    sprFace.fillCircle(cx, cy, 5, COLOR_SEC_HAND);
    sprFace.fillCircle(cx, cy, 2, COLOR_TEXT_MAIN);

    // Push the sprite to the main display at (40, 40).
    sprFace.pushSprite(40, 40);
}