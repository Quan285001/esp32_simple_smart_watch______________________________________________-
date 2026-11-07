//face_clock.cpp

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>
#include <time.h>
#include <WiFi.h>
#include <Preferences.h>
#include "../services/ble_service.h"
#include "icons.h"

extern TFT_eSPI tft;

static TFT_eSprite sprFace = TFT_eSprite(&tft);
static bool sprFaceCreated = false;

// Face Studio v0.1 profile. Defaults preserve the original factory appearance.
static uint16_t faceBackgroundColor = 0x0000;
static uint16_t faceDialColor = 0x18C3;
static uint16_t faceHandColor = 0xFFFF;
static uint16_t faceAccentColor = 0xFBE0;
static uint8_t faceStudioFlags = 0x03; // bit 0: seconds, bit 1: date
static uint8_t faceStyleId = 0; // Stable Face Studio style ID; 0 = Classic analog

static void writeProfileColor(uint8_t* bytes, uint8_t offset, uint16_t color) {
    bytes[offset] = static_cast<uint8_t>(color & 0xFF);
    bytes[offset + 1] = static_cast<uint8_t>(color >> 8);
}

static uint16_t readProfileColor(const uint8_t* bytes, uint8_t offset) {
    return static_cast<uint16_t>(bytes[offset]) |
           (static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

void loadFaceStudioConfig() {
    Preferences prefs;
    if (!prefs.begin("face", true)) return;
    uint8_t profile[9] = {};
    if (prefs.getBytesLength("profile") == sizeof(profile) &&
        prefs.getBytes("profile", profile, sizeof(profile)) == sizeof(profile)) {
        faceBackgroundColor = readProfileColor(profile, 0);
        faceDialColor = readProfileColor(profile, 2);
        faceHandColor = readProfileColor(profile, 4);
        faceAccentColor = readProfileColor(profile, 6);
        faceStudioFlags = profile[8] & 0x03;
    }
    faceStyleId = prefs.getUChar("style", 0);
    prefs.end();
}

bool setFaceStyleConfig(uint8_t styleId) {
    // IDs are grouped by renderer family; unknown IDs are rejected.
    const bool valid = styleId <= 12 || (styleId >= 20 && styleId <= 27) ||
                       (styleId >= 40 && styleId <= 44) ||
                       (styleId >= 60 && styleId <= 62);
    if (!valid) return false;
    Preferences prefs;
    if (!prefs.begin("face", false)) return false;
    const size_t written = prefs.putUChar("style", styleId);
    const uint8_t verify = prefs.getUChar("style", 0xFF);
    prefs.end();
    if (written != sizeof(uint8_t) || verify != styleId) return false;
    faceStyleId = styleId;
    Serial.printf("[FACE] Style %u saved and applied.\\n", styleId);
    return true;
}

bool setFaceStudioConfig(uint16_t background, uint16_t dial, uint16_t hand,
                         uint16_t accent, uint8_t flags) {
    if (flags > 0x03) return false;

    // One bounded NVS blob makes profile replacement atomic at the key level.
    uint8_t profile[9] = {};
    writeProfileColor(profile, 0, background);
    writeProfileColor(profile, 2, dial);
    writeProfileColor(profile, 4, hand);
    writeProfileColor(profile, 6, accent);
    profile[8] = flags;

    Preferences prefs;
    if (!prefs.begin("face", false)) return false;
    const size_t written = prefs.putBytes("profile", profile, sizeof(profile));
    uint8_t verify[sizeof(profile)] = {};
    const size_t read = prefs.getBytes("profile", verify, sizeof(verify));
    bool verified = written == sizeof(profile) && read == sizeof(profile);
    for (uint8_t i = 0; i < sizeof(profile) && verified; ++i) {
        if (profile[i] != verify[i]) verified = false;
    }
    prefs.end();
    if (!verified) return false;

    faceBackgroundColor = background;
    faceDialColor = dial;
    faceHandColor = hand;
    faceAccentColor = accent;
    faceStudioFlags = flags;
    Serial.println("[FACE] Custom face profile saved and applied.");
    return true;
}

/**
 * @brief Draws a sun or crescent-moon icon according to the current hour.
 * @param currentHour Input or state used by this operation.
 * @param x Input or state used by this operation.
 * @param y Input or state used by this operation.
 */
void drawSmartWeatherIconSpr(int currentHour, int x, int y) {
    bool isNight = (currentHour < 6 || currentHour >= 18);
    if (isNight) {
        drawMiniCrescentMoon(sprFace, x, y, faceAccentColor, faceDialColor);
    } else {
        drawMiniSun(sprFace, x, y, faceAccentColor);
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

    sprFace.fillSprite(faceBackgroundColor);

    int cx = 80;
    int cy = 80;

    sprFace.fillCircle(cx, cy, 78, faceDialColor);

    // Analog families share the same timebase but use distinct dial treatments.
    if (faceStyleId == 2) {
        static const char* roman[] = {"XII","I","II","III","IV","V","VI","VII","VIII","IX","X","XI"};
        sprFace.setTextDatum(MC_DATUM);
        sprFace.setTextColor(faceHandColor, faceDialColor);
        for (int i = 0; i < 12; ++i) {
            const float a = i * PI / 6.0f - PI / 2.0f;
            sprFace.drawString(roman[i], cx + (int)(58*cos(a)), cy + (int)(58*sin(a)), 1);
        }
    } else if (faceStyleId == 5 || faceStyleId == 7 || faceStyleId == 11) {
        sprFace.drawCircle(cx, cy, 73, faceAccentColor);
        sprFace.drawCircle(cx, cy, 70, faceAccentColor);
    } else if (faceStyleId == 3 || faceStyleId == 4 || faceStyleId == 9 ||
               faceStyleId == 10 || faceStyleId == 12) {
        for (int i = 0; i < 60; ++i) {
            const float a = i * PI / 30.0f - PI / 2.0f;
            const bool major = (i % 5) == 0;
            const int outer = (faceStyleId == 4 ? 72 : 74);
            const int inner = outer - (major ? 9 : 4);
            sprFace.drawLine(cx + (int)(inner*cos(a)), cy + (int)(inner*sin(a)),
                             cx + (int)(outer*cos(a)), cy + (int)(outer*sin(a)),
                             major ? faceAccentColor : faceHandColor);
        }
    } else if (faceStyleId == 1 || faceStyleId == 6 || faceStyleId == 8) {
        for (int i = 0; i < 12; ++i) {
            const float a = i * PI / 6.0f - PI / 2.0f;
            const int r = (i % 3 == 0) ? 72 : 74;
            sprFace.fillCircle(cx + (int)(r*cos(a)), cy + (int)(r*sin(a)),
                               (i % 3 == 0) ? 2 : 1, faceAccentColor);
        }
    }

    sprFace.setTextDatum(MC_DATUM);
    sprFace.setTextColor(faceAccentColor, faceDialColor);
    sprFace.drawString("UTC+7", cx - 40, cy - 10, 1);
    // Show the full local time in 24-hour format, including seconds.
    char localTimeText[9];
    snprintf(localTimeText, sizeof(localTimeText), "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    sprFace.setTextColor(faceHandColor, faceDialColor);
    if (faceStudioFlags & 0x01) {
        sprFace.drawString(localTimeText, cx - 40, cy + 6, 1);
    } else {
        char shortTimeText[6];
        snprintf(shortTimeText, sizeof(shortTimeText), "%02d:%02d",
                 timeinfo.tm_hour, timeinfo.tm_min);
        sprFace.drawString(shortTimeText, cx - 40, cy + 6, 1);
    }

    if (faceStudioFlags & 0x02) {
        const char* days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
        int wday = (timeinfo.tm_wday >= 0 && timeinfo.tm_wday < 7) ? timeinfo.tm_wday : 0;
        sprFace.setTextColor(faceAccentColor, faceDialColor);
        sprFace.drawString(days[wday], cx + 40, cy - 10, 1);
        sprFace.setTextColor(faceHandColor, faceDialColor);
        sprFace.drawNumber(timeinfo.tm_mday > 0 ? timeinfo.tm_mday : 1, cx + 40, cy + 6, 2);
    }

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
    sprFace.setTextColor(faceAccentColor, faceDialColor);
    sprFace.drawString(connectionLabel, cx, cy + 50, 1);

    // Digital and typography renderers are exclusive: they return before any
    // analog hand-angle calculation or hand drawing can run.
    if ((faceStyleId >= 20 && faceStyleId <= 27) ||
        (faceStyleId >= 40 && faceStyleId <= 44)) {
        char timeText[9];
        snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d",
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        sprFace.setTextDatum(MC_DATUM);
        if (faceStyleId >= 40) {
            static const char* hours[] = {"TWELVE","ONE","TWO","THREE","FOUR","FIVE",
                "SIX","SEVEN","EIGHT","NINE","TEN","ELEVEN"};
            static const char* minutes[] = {"O'CLOCK","FIVE","TEN","FIFTEEN","TWENTY",
                "TWENTY FIVE","THIRTY","THIRTY FIVE","FORTY","FORTY FIVE","FIFTY","FIFTY FIVE"};
            const uint8_t h = static_cast<uint8_t>((timeinfo.tm_hour % 12 + 12) % 12);
            const uint8_t m = static_cast<uint8_t>(((timeinfo.tm_min + 2) / 5) % 12);
            sprFace.setTextColor(faceAccentColor, faceDialColor);
            sprFace.drawString(faceStyleId == 44 ? "THE TIME IS" : "IT IS", cx, cy - 29, 1);
            sprFace.setTextColor(faceHandColor, faceDialColor);
            sprFace.drawString(hours[h], cx, cy - 3, faceStyleId == 43 ? 4 : 2);
            sprFace.setTextColor(faceAccentColor, faceDialColor);
            sprFace.drawString(minutes[m], cx, cy + 25, faceStyleId == 43 ? 2 : 1);
        } else {
            const bool compact = faceStyleId == 21 || faceStyleId == 22;
            const bool seconds = (faceStudioFlags & 0x01) != 0;
            const bool stacked = faceStyleId == 23 || faceStyleId == 24 || faceStyleId == 25;
            const bool led = faceStyleId == 26 || faceStyleId == 27;
            sprFace.setTextColor(faceAccentColor, faceDialColor);
            sprFace.drawString(led ? "DIGITAL TIME" : "LOCAL TIME", cx, cy - 31, 1);
            sprFace.setTextColor(faceHandColor, faceDialColor);
            if (stacked) {
                char shortTime[6];
                snprintf(shortTime, sizeof(shortTime), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                sprFace.drawString(shortTime, cx, cy - 2, faceStyleId == 25 ? 7 : 4);
                if (seconds) {
                    char secText[3]; snprintf(secText, sizeof(secText), "%02d", timeinfo.tm_sec);
                    sprFace.setTextColor(faceAccentColor, faceDialColor);
                    sprFace.drawString(secText, cx, cy + 29, 2);
                }
            } else {
                char displayTime[9];
                if (seconds) snprintf(displayTime, sizeof(displayTime), "%s", timeText);
                else snprintf(displayTime, sizeof(displayTime), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                sprFace.drawString(displayTime, cx, cy + (compact ? 0 : 5), faceStyleId == 22 ? 4 : 2);
            }
        }
        if (faceStudioFlags & 0x02) {
            char dateText[20];
            static const char* months[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                "JUL","AUG","SEP","OCT","NOV","DEC"};
            const int month = (timeinfo.tm_mon >= 0 && timeinfo.tm_mon < 12)
                ? timeinfo.tm_mon : 0;
            snprintf(dateText, sizeof(dateText), "%02d %s", timeinfo.tm_mday, months[month]);
            sprFace.setTextColor(faceAccentColor, faceDialColor);
            sprFace.drawString(dateText, cx, cy + 57, 1);
        }
        sprFace.pushSprite(40, 40);
        return;
    }

    float exactSeconds = timeinfo.tm_sec + (millisValue / 1000.0f);
    float exactMinutes = timeinfo.tm_min + (exactSeconds / 60.0f);
    float exactHours   = (timeinfo.tm_hour % 12) + (exactMinutes / 60.0f);

    float angleHour = (exactHours * 30.0f - 90.0f) * (PI / 180.0f);
    float angleMin  = (exactMinutes * 6.0f - 90.0f) * (PI / 180.0f);
    float angleSec  = (exactSeconds * 6.0f - 90.0f) * (PI / 180.0f);

    int hx = cx + (int)(40 * cos(angleHour));
    int hy = cy + (int)(40 * sin(angleHour));
    for (int offset = -1; offset <= 1; offset++) {
        sprFace.drawLine(cx + offset, cy, hx + offset, hy, faceHandColor);
        sprFace.drawLine(cx, cy + offset, hx, hy + offset, faceHandColor);
    }

    int mx = cx + (int)(68 * cos(angleMin));
    int my = cy + (int)(68 * sin(angleMin));
    for (int offset = -1; offset <= 1; offset++) {
        sprFace.drawLine(cx + offset, cy, mx + offset, my, faceHandColor);
        sprFace.drawLine(cx, cy + offset, mx, my + offset, faceHandColor);
    }

    if (faceStudioFlags & 0x01) {
        int sx = cx + (int)(90 * cos(angleSec));
        int sy = cy + (int)(90 * sin(angleSec));
        int sx_back = cx - (int)(18 * cos(angleSec));
        int sy_back = cy - (int)(18 * sin(angleSec));
        sprFace.drawLine(sx_back, sy_back, sx, sy, faceAccentColor);
        sprFace.drawLine(sx_back + 1, sy_back, sx + 1, sy, faceAccentColor);
    }

    if (faceStyleId >= 60 && faceStyleId <= 62) {
        char hybridTime[6];
        snprintf(hybridTime, sizeof(hybridTime), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        sprFace.setTextDatum(MC_DATUM);
        sprFace.setTextColor(faceAccentColor, faceDialColor);
        sprFace.drawString(hybridTime, cx, cy + 43, faceStyleId == 61 ? 2 : 1);
        if (faceStyleId == 61) {
            sprFace.setTextColor(faceHandColor, faceDialColor);
            sprFace.drawString("UTC+7", cx, cy + 56, 1);
        }
    }

    sprFace.fillCircle(cx, cy, 5, faceAccentColor);
    sprFace.fillCircle(cx, cy, 2, faceHandColor);

    // Push the sprite to the main display at (40, 40).
    sprFace.pushSprite(40, 40);
}