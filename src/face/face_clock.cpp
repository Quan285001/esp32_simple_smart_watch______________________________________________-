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

uint8_t getFaceStyleConfig() { return faceStyleId; }

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

    if (!sprFaceCreated) {
        sprFace.setColorDepth(16);
        if (!sprFace.createSprite(160, 160)) return;
        sprFaceCreated = true;
    }

    // Every frame starts from a clean, opaque 160x160 canvas. No previous
    // style is allowed to remain underneath the selected renderer.
    sprFace.fillSprite(faceBackgroundColor);
    const int cx = 80;
    const int cy = 80;
    sprFace.setTextDatum(MC_DATUM);

    const bool digitalStyle = faceStyleId >= 20 && faceStyleId <= 27;
    const bool typographyStyle = faceStyleId >= 40 && faceStyleId <= 44;

    // DIGITAL: a standalone renderer. It deliberately does not draw an
    // analog dial, analog metadata, weather icon, or hands.
    if (digitalStyle) {
        sprFace.fillRoundRect(8, 35, 144, 90, 10, faceDialColor);
        sprFace.setTextColor(faceAccentColor, faceDialColor);
        if (faceStyleId == 26) sprFace.drawString("LED TIME", cx, 49, 1);
        else if (faceStyleId != 21) sprFace.drawString("LOCAL TIME", cx, 49, 1);

        char hhmm[6];
        char hhmmss[9];
        snprintf(hhmm, sizeof(hhmm), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        snprintf(hhmmss, sizeof(hhmmss), "%02d:%02d:%02d",
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        if (faceStyleId == 27) {
            // Split-flap inspired tiles; each tile is painted before its digit.
            const int widths[5] = {25, 25, 10, 25, 25};
            int x = 17;
            for (int i = 0; i < 5; ++i) {
                const int w = widths[i];
                if (i == 2) {
                    sprFace.setTextColor(faceAccentColor, faceDialColor);
                    sprFace.drawString(":", cx, 88, 4);
                } else {
                    const int charIndex = i < 2 ? i : i - 1;
                    sprFace.fillRoundRect(x, 66, w, 39, 3, faceBackgroundColor);
                    sprFace.drawFastHLine(x + 2, 85, w - 4, faceAccentColor);
                    sprFace.setTextColor(faceHandColor, faceBackgroundColor);
                    char digit[2] = {hhmm[charIndex], '\0'};
                    sprFace.drawString(digit, x + w / 2, 84, 4);
                }
                x += w + 2;
            }
        } else if (faceStyleId == 23 || faceStyleId == 24 || faceStyleId == 25) {
            sprFace.setTextColor(faceHandColor, faceDialColor);
            sprFace.drawString(hhmm, cx, 82, faceStyleId == 25 ? 7 : 4);
            if (faceStudioFlags & 0x01) {
                char seconds[3];
                snprintf(seconds, sizeof(seconds), "%02d", timeinfo.tm_sec);
                sprFace.setTextColor(faceAccentColor, faceDialColor);
                sprFace.drawString(seconds, cx, 111, 2);
            }
        } else {
            sprFace.setTextColor(faceHandColor, faceDialColor);
            const bool showSeconds = (faceStudioFlags & 0x01) != 0;
            const char* display = showSeconds ? hhmmss : hhmm;
            const uint8_t font = faceStyleId == 22 ? 4 : 2;
            sprFace.drawString(display, cx, 83, font);
        }

        if (faceStudioFlags & 0x02) {
            char dateText[20];
            static const char* months[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                "JUL","AUG","SEP","OCT","NOV","DEC"};
            const int month = (timeinfo.tm_mon >= 0 && timeinfo.tm_mon < 12)
                ? timeinfo.tm_mon : 0;
            snprintf(dateText, sizeof(dateText), "%02d %s", timeinfo.tm_mday, months[month]);
            sprFace.setTextColor(faceAccentColor, faceBackgroundColor);
            sprFace.drawString(dateText, cx, 143, 1);
        }
        sprFace.pushSprite(40, 40);
        return;
    }

    // TYPOGRAPHY: words only, with no analog dial or hand geometry.
    if (typographyStyle) {
        sprFace.setTextColor(faceAccentColor, faceBackgroundColor);
        if (faceStyleId == 41) sprFace.drawString("LOCAL TIME", cx, 38, 1);
        else if (faceStyleId == 44) sprFace.drawString("THE TIME IS", cx, 47, 1);
        else sprFace.drawString("IT IS", cx, 47, 1);

        static const char* hours[] = {"TWELVE","ONE","TWO","THREE","FOUR","FIVE",
            "SIX","SEVEN","EIGHT","NINE","TEN","ELEVEN"};
        static const char* minutes[] = {"O'CLOCK","FIVE","TEN","FIFTEEN","TWENTY",
            "TWENTY FIVE","THIRTY","THIRTY FIVE","FORTY","FORTY FIVE","FIFTY","FIFTY FIVE"};
        const uint8_t h = static_cast<uint8_t>((timeinfo.tm_hour % 12 + 12) % 12);
        const uint8_t m = static_cast<uint8_t>(((timeinfo.tm_min + 2) / 5) % 12);

        if (faceStyleId == 41) {
            char exactTime[6];
            snprintf(exactTime, sizeof(exactTime), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
            sprFace.setTextColor(faceHandColor, faceBackgroundColor);
            sprFace.drawString(exactTime, cx, 85, 4);
            sprFace.setTextColor(faceAccentColor, faceBackgroundColor);
            sprFace.drawString(hours[h], cx, 119, 1);
        } else if (faceStyleId == 42) {
            sprFace.setTextColor(faceHandColor, faceBackgroundColor);
            sprFace.drawString(hours[h], cx, 78, 2);
            sprFace.setTextColor(faceAccentColor, faceBackgroundColor);
            sprFace.drawString(minutes[m], cx, 108, 1);
            sprFace.drawFastHLine(35, 128, 90, faceAccentColor);
        } else {
            sprFace.setTextColor(faceHandColor, faceBackgroundColor);
            sprFace.drawString(hours[h], cx, 83, faceStyleId == 43 ? 4 : 2);
            sprFace.setTextColor(faceAccentColor, faceBackgroundColor);
            sprFace.drawString(minutes[m], cx, 116, faceStyleId == 43 ? 2 : 1);
        }

        if (faceStudioFlags & 0x02) {
            char dateText[20];
            static const char* months[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                "JUL","AUG","SEP","OCT","NOV","DEC"};
            const int month = (timeinfo.tm_mon >= 0 && timeinfo.tm_mon < 12)
                ? timeinfo.tm_mon : 0;
            snprintf(dateText, sizeof(dateText), "%02d %s", timeinfo.tm_mday, months[month]);
            sprFace.setTextColor(faceAccentColor, faceBackgroundColor);
            sprFace.drawString(dateText, cx, 148, 1);
        }
        sprFace.pushSprite(40, 40);
        return;
    }

    // ANALOG and HYBRID share a dial renderer. Hybrid styles intentionally
    // add a small digital complication; ordinary analog styles do not.
    sprFace.fillCircle(cx, cy, 78, faceDialColor);

    if (faceStyleId == 2) {
        static const char* roman[] = {"XII","I","II","III","IV","V","VI","VII","VIII","IX","X","XI"};
        sprFace.setTextColor(faceHandColor, faceDialColor);
        for (int i = 0; i < 12; ++i) {
            const float a = i * PI / 6.0f - PI / 2.0f;
            sprFace.drawString(roman[i], cx + (int)(58*cos(a)), cy + (int)(58*sin(a)), 1);
        }
    } else if (faceStyleId == 5 || faceStyleId == 7 || faceStyleId == 11) {
        sprFace.drawCircle(cx, cy, 73, faceAccentColor);
        sprFace.drawCircle(cx, cy, 70, faceAccentColor);
        if (faceStyleId == 7) {
            sprFace.drawLine(cx - 48, cy - 48, cx + 48, cy + 48, faceAccentColor);
            sprFace.drawLine(cx + 48, cy - 48, cx - 48, cy + 48, faceAccentColor);
        }
    } else {
        for (int i = 0; i < 60; ++i) {
            const float a = i * PI / 30.0f - PI / 2.0f;
            const bool major = (i % 5) == 0;
            const int outer = (faceStyleId == 4 ? 72 : 74);
            const int inner = outer - (major ? 9 : 4);
            const uint16_t color = major ? faceAccentColor : faceHandColor;
            sprFace.drawLine(cx + (int)(inner*cos(a)), cy + (int)(inner*sin(a)),
                             cx + (int)(outer*cos(a)), cy + (int)(outer*sin(a)), color);
        }
        if (faceStyleId == 6 || faceStyleId == 8 || faceStyleId == 9) {
            static const int cardinal[4] = {12, 3, 6, 9};
            for (int n : cardinal) {
                const float a = n * PI / 6.0f - PI / 2.0f;
                sprFace.setTextColor(faceHandColor, faceDialColor);
                sprFace.drawString(String(n), cx + (int)(52*cos(a)), cy + (int)(52*sin(a)), 1);
            }
        }
    }

    const float exactSeconds = timeinfo.tm_sec + (millisValue / 1000.0f);
    const float exactMinutes = timeinfo.tm_min + (exactSeconds / 60.0f);
    const float exactHours = (timeinfo.tm_hour % 12) + (exactMinutes / 60.0f);
    const float angleHour = (exactHours * 30.0f - 90.0f) * (PI / 180.0f);
    const float angleMin = (exactMinutes * 6.0f - 90.0f) * (PI / 180.0f);
    const float angleSec = (exactSeconds * 6.0f - 90.0f) * (PI / 180.0f);

    const int hx = cx + (int)(40 * cos(angleHour));
    const int hy = cy + (int)(40 * sin(angleHour));
    const int mx = cx + (int)(68 * cos(angleMin));
    const int my = cy + (int)(68 * sin(angleMin));
    for (int offset = -1; offset <= 1; ++offset) {
        sprFace.drawLine(cx + offset, cy, hx + offset, hy, faceHandColor);
        sprFace.drawLine(cx, cy + offset, hx, hy + offset, faceHandColor);
        sprFace.drawLine(cx + offset, cy, mx + offset, my, faceHandColor);
        sprFace.drawLine(cx, cy + offset, mx, my + offset, faceHandColor);
    }

    if (faceStudioFlags & 0x01) {
        const int sx = cx + (int)(90 * cos(angleSec));
        const int sy = cy + (int)(90 * sin(angleSec));
        const int sxBack = cx - (int)(18 * cos(angleSec));
        const int syBack = cy - (int)(18 * sin(angleSec));
        sprFace.drawLine(sxBack, syBack, sx, sy, faceAccentColor);
        sprFace.drawLine(sxBack + 1, syBack, sx + 1, sy, faceAccentColor);
    }

    if (faceStyleId >= 60 && faceStyleId <= 62) {
        char hybridTime[6];
        snprintf(hybridTime, sizeof(hybridTime), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        sprFace.setTextColor(faceAccentColor, faceDialColor);
        sprFace.drawString(hybridTime, cx, cy + 43, faceStyleId == 61 ? 2 : 1);
        if (faceStyleId == 61) {
            sprFace.setTextColor(faceHandColor, faceDialColor);
            sprFace.drawString("UTC+7", cx, cy + 56, 1);
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

    if (faceStyleId == 3 || faceStyleId == 4 || faceStyleId == 12) {
        drawSmartWeatherIconSpr(timeinfo.tm_hour, cx, cy - 42);
    }

    sprFace.fillCircle(cx, cy, 5, faceAccentColor);
    sprFace.fillCircle(cx, cy, 2, faceHandColor);
    sprFace.pushSprite(40, 40);
}