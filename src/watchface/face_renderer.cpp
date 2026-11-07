#include "face_renderer.h"

#include <TFT_eSPI.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../display/display_manager.h"

namespace {
constexpr uint16_t COLOR_WHITE = 0xFFFF;
constexpr float PI_F = 3.14159265358979323846f;

void sortObjectIndices(const FaceModel& face, uint8_t* indices) {
    for (uint8_t i = 0; i < face.objectCount; ++i) {
        indices[i] = i;
        uint8_t j = i;
        while (j > 0 &&
               face.objects[indices[j - 1]].transform.zIndex >
               face.objects[indices[j]].transform.zIndex) {
            const uint8_t swap = indices[j - 1];
            indices[j - 1] = indices[j];
            indices[j] = swap;
            --j;
        }
    }
}

uint8_t textScale(uint16_t fontSize) {
    uint16_t scale = static_cast<uint16_t>((fontSize + 7U) / 8U);
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    return static_cast<uint8_t>(scale);
}

void drawTextInBounds(const char* text, const FaceObject& object, uint16_t color) {
    if (text == nullptr || text[0] == '\0') return;
    const int x = static_cast<int>(lroundf(object.transform.x));
    const int y = static_cast<int>(lroundf(object.transform.y));
    const int width = static_cast<int>(lroundf(object.transform.width));
    const int height = static_cast<int>(lroundf(object.transform.height));
    const uint8_t scale = textScale(object.fontSize);
    const int charWidth = 6 * scale + object.letterSpacing;
    const int textWidth = static_cast<int>(strlen(text)) * charWidth;
    int drawX = x;
    if (object.textAlign == FaceTextAlign::Center) drawX += (width - textWidth) / 2;
    else if (object.textAlign == FaceTextAlign::Right) drawX += width - textWidth;
    const int drawY = y + (height - (8 * scale)) / 2;

    tft.setTextSize(scale);
    tft.setTextColor(color);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(text, drawX, drawY, 1);
    if (object.fontWeight >= 600) {
        tft.drawString(text, drawX + 1, drawY, 1);
    }
    tft.setTextSize(1);
}

void formatTime(char* out, size_t capacity, const FaceObject& object,
                const struct tm& timeInfo) {
    const bool twelveHour = strcmp(object.timeFormat, "12h") == 0;
    int hour = timeInfo.tm_hour;
    const char* suffix = "";
    if (twelveHour) {
        suffix = hour >= 12 ? " PM" : " AM";
        hour %= 12;
        if (hour == 0) hour = 12;
    }
    const bool seconds = object.showSeconds ||
                         strstr(object.dataFormat, "ss") != nullptr;
    if (seconds) {
        snprintf(out, capacity, "%02d:%02d:%02d%s",
                 hour, timeInfo.tm_min, timeInfo.tm_sec, suffix);
    } else {
        snprintf(out, capacity, "%02d:%02d%s", hour, timeInfo.tm_min, suffix);
    }
}

void formatDate(char* out, size_t capacity, const FaceObject& object,
                const struct tm& timeInfo) {
    static const char* const weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* const months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    const int weekday = (timeInfo.tm_wday >= 0 && timeInfo.tm_wday < 7) ? timeInfo.tm_wday : 0;
    const int month = (timeInfo.tm_mon >= 0 && timeInfo.tm_mon < 12) ? timeInfo.tm_mon : 0;
    if (strstr(object.dataFormat, "YYYY") != nullptr) {
        snprintf(out, capacity, "%04d-%02d-%02d",
                 timeInfo.tm_year + 1900, month + 1, timeInfo.tm_mday);
    } else if (strstr(object.dataFormat, "ddd") != nullptr) {
        snprintf(out, capacity, "%s - %02d %s",
                 weekdays[weekday], timeInfo.tm_mday, months[month]);
    } else {
        snprintf(out, capacity, "%02d %s", timeInfo.tm_mday, months[month]);
    }
}

void drawShape(const FaceObject& object) {
    const int x = static_cast<int>(lroundf(object.transform.x));
    const int y = static_cast<int>(lroundf(object.transform.y));
    const int w = static_cast<int>(lroundf(object.transform.width));
    const int h = static_cast<int>(lroundf(object.transform.height));
    switch (object.shapeKind) {
        case 1:
            if (object.strokeWidth) tft.drawCircle(x + w / 2, y + h / 2, min(w, h) / 2, object.strokeColor);
            else tft.fillCircle(x + w / 2, y + h / 2, min(w, h) / 2, object.fillColor);
            break;
        case 2:
            if (object.strokeWidth) tft.drawRoundRect(x, y, w, h, object.cornerRadius, object.strokeColor);
            else tft.fillRoundRect(x, y, w, h, object.cornerRadius, object.fillColor);
            break;
        case 3:
            tft.drawLine(x, y, x + w, y + h, object.strokeWidth ? object.strokeColor : object.fillColor);
            break;
        default:
            if (object.strokeWidth) tft.drawRect(x, y, w, h, object.strokeColor);
            else tft.fillRect(x, y, w, h, object.fillColor);
            break;
    }
}

void drawAnalogClock(const FaceObject& object, const struct tm& timeInfo) {
    const int w = static_cast<int>(lroundf(object.transform.width));
    const int h = static_cast<int>(lroundf(object.transform.height));
    const int cx = static_cast<int>(lroundf(object.transform.x)) + w / 2;
    const int cy = static_cast<int>(lroundf(object.transform.y)) + h / 2;
    const int radius = min(w, h) / 2 - 2;
    if (radius < 8) return;

    const uint16_t color = object.textColor;
    tft.drawCircle(cx, cy, radius, color);
    for (int tick = 0; tick < 12; ++tick) {
        const float angle = (tick * 30.0f - 90.0f) * PI_F / 180.0f;
        const int outerX = cx + static_cast<int>(cosf(angle) * (radius - 2));
        const int outerY = cy + static_cast<int>(sinf(angle) * (radius - 2));
        const int innerX = cx + static_cast<int>(cosf(angle) * (radius - (tick % 3 == 0 ? 9 : 5)));
        const int innerY = cy + static_cast<int>(sinf(angle) * (radius - (tick % 3 == 0 ? 9 : 5)));
        tft.drawLine(innerX, innerY, outerX, outerY, color);
    }

    const float seconds = static_cast<float>(timeInfo.tm_sec);
    const float minutes = static_cast<float>(timeInfo.tm_min) + seconds / 60.0f;
    const float hours = static_cast<float>(timeInfo.tm_hour % 12) + minutes / 60.0f;
    const float hourAngle = (hours * 30.0f - 90.0f) * PI_F / 180.0f;
    const float minuteAngle = (minutes * 6.0f - 90.0f) * PI_F / 180.0f;
    const float secondAngle = (seconds * 6.0f - 90.0f) * PI_F / 180.0f;
    tft.drawLine(cx, cy, cx + static_cast<int>(cosf(hourAngle) * radius * 0.48f),
                 cy + static_cast<int>(sinf(hourAngle) * radius * 0.48f), color);
    tft.drawLine(cx, cy, cx + static_cast<int>(cosf(minuteAngle) * radius * 0.72f),
                 cy + static_cast<int>(sinf(minuteAngle) * radius * 0.72f), color);
    tft.drawLine(cx, cy, cx + static_cast<int>(cosf(secondAngle) * radius * 0.86f),
                 cy + static_cast<int>(sinf(secondAngle) * radius * 0.86f), TFT_RED);
    tft.fillCircle(cx, cy, 3, color);
}
}

FaceRenderStatus renderWatchFace(const FaceModel& face, const struct tm& timeInfo,
                                 const WeatherData& weather) {
    if (face.objectCount > FACE_MAX_OBJECTS ||
        face.canvasWidth != 240 || face.canvasHeight != 240) {
        return FaceRenderStatus::UnsupportedTransform;
    }

    // Preflight before touching the panel. Never blank a working face for an
    // unsupported design feature.
    for (uint8_t i = 0; i < face.objectCount; ++i) {
        const FaceObject& object = face.objects[i];
        if (object.transform.rotation != 0.0f || object.transform.opacity < 0.999f) {
            return FaceRenderStatus::UnsupportedTransform;
        }
        if (object.type == FaceElementType::Image) return FaceRenderStatus::UnsupportedAsset;
        if (object.fontFamily[0] != '\0' &&
            strcmp(object.fontFamily, "system-ui") != 0 &&
            strcmp(object.fontFamily, "sans-serif") != 0) {
            return FaceRenderStatus::UnsupportedFont;
        }
    }

    tft.fillScreen(face.backgroundColor);
    uint8_t order[FACE_MAX_OBJECTS] = {};
    sortObjectIndices(face, order);

    for (uint8_t sorted = 0; sorted < face.objectCount; ++sorted) {
        const FaceObject& object = face.objects[order[sorted]];
        if (!object.visible) continue;
        switch (object.type) {
            case FaceElementType::Shape:
                drawShape(object);
                break;
            case FaceElementType::AnalogClock:
                drawAnalogClock(object, timeInfo);
                break;
            case FaceElementType::DigitalClock: {
                char value[FACE_TEXT_CAPACITY] = {};
                formatTime(value, sizeof(value), object, timeInfo);
                drawTextInBounds(value, object, object.textColor);
                break;
            }
            case FaceElementType::Date: {
                char value[FACE_TEXT_CAPACITY] = {};
                formatDate(value, sizeof(value), object, timeInfo);
                drawTextInBounds(value, object, object.textColor);
                break;
            }
            case FaceElementType::Weather: {
                char value[FACE_TEXT_CAPACITY] = {};
                if (weather.valid) {
                    snprintf(value, sizeof(value), "%d C", weather.currentTemp);
                } else {
                    snprintf(value, sizeof(value), "%s",
                             object.text[0] ? object.text : "Weather unavailable");
                }
                drawTextInBounds(value, object, object.textColor);
                break;
            }
            case FaceElementType::Text:
                drawTextInBounds(object.text, object, object.textColor);
                break;
            case FaceElementType::Image:
                return FaceRenderStatus::UnsupportedAsset;
        }
    }
    return FaceRenderStatus::Ok;
}
