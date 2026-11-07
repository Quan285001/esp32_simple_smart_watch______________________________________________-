#include "face_renderer.h"

#include <TFT_eSPI.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../display/display_manager.h"
#include "elements/image.h"

namespace {
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

uint8_t textScale(float fontSize) {
    int scale = static_cast<int>(lroundf(fontSize / 8.0f));
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    return static_cast<uint8_t>(scale);
}

bool isDynamic(FaceElementType type) {
    return type == FaceElementType::DigitalClock ||
           type == FaceElementType::AnalogClock ||
           type == FaceElementType::Date ||
           type == FaceElementType::Weather;
}

bool intersects(const FaceObject& object, int x, int y, int w, int h) {
    const int ox = static_cast<int>(lroundf(object.transform.x));
    const int oy = static_cast<int>(lroundf(object.transform.y));
    const int ow = static_cast<int>(lroundf(object.transform.width));
    const int oh = static_cast<int>(lroundf(object.transform.height));
    return ox < x + w && ox + ow > x && oy < y + h && oy + oh > y;
}

FaceRenderStatus preflight(const FaceModel& face) {
    if (face.objectCount > FACE_MAX_OBJECTS || face.assetCount > FACE_MAX_ASSETS ||
        face.canvasWidth != 240 || face.canvasHeight != 240) {
        return FaceRenderStatus::UnsupportedTransform;
    }
    for (uint8_t i = 0; i < face.objectCount; ++i) {
        const FaceObject& object = face.objects[i];
        if (object.transform.rotation != 0.0f || object.transform.opacity < 0.999f ||
            object.letterSpacing != 0 || (object.fontWeight != 400 && object.fontWeight != 600) ||
            !isfinite(object.fontSize) || object.fontSize <= 0.0f || object.fontSize > 256.0f ||
            !isfinite(object.glyphScaleX) || !isfinite(object.glyphScaleY) ||
            object.glyphScaleX < 0.1f || object.glyphScaleX > 8.0f ||
            object.glyphScaleY < 0.1f || object.glyphScaleY > 8.0f) {
            return FaceRenderStatus::UnsupportedTransform;
        }
        if (object.type == FaceElementType::Image) {
            bool foundAsset = false;
            for (uint8_t assetIndex = 0; assetIndex < face.assetCount; ++assetIndex) {
                const FaceAsset& asset = face.assets[assetIndex];
                if (strcmp(asset.id, object.assetId) == 0) {
                    foundAsset = asset.width == static_cast<uint16_t>(lroundf(object.transform.width)) &&
                                 asset.height == static_cast<uint16_t>(lroundf(object.transform.height));
                    break;
                }
            }
            if (!foundAsset) return FaceRenderStatus::UnsupportedAsset;
        }
        if (object.fontFamily[0] != '\0' &&
            strcmp(object.fontFamily, "system-ui") != 0 &&
            strcmp(object.fontFamily, "sans-serif") != 0 &&
            strcmp(object.fontFamily, "inherit") != 0) {
            return FaceRenderStatus::UnsupportedFont;
        }
    }
    return FaceRenderStatus::Ok;
}

void drawTextInBounds(const char* text, const FaceObject& object, uint16_t color) {
    if (text == nullptr || text[0] == '\0') return;
    const int x = static_cast<int>(lroundf(object.transform.x));
    const int y = static_cast<int>(lroundf(object.transform.y));
    const int width = static_cast<int>(lroundf(object.transform.width));
    const int height = static_cast<int>(lroundf(object.transform.height));
    if (width <= 0 || height <= 0) return;

    const float scaleX = (object.fontSize / 8.0f) * object.glyphScaleX;
    const float scaleY = (object.fontSize / 8.0f) * object.glyphScaleY;
    const float advance = 6.0f * scaleX + object.letterSpacing;
    const float textWidth = strlen(text) * advance - object.letterSpacing;
    float cursorX = static_cast<float>(x);
    if (object.textAlign == FaceTextAlign::Center) cursorX += (width - textWidth) * 0.5f;
    else if (object.textAlign == FaceTextAlign::Right) cursorX += width - textWidth;
    const float drawY = y + (height - 8.0f * scaleY) * 0.5f;

    // A 6x8 scratch glyph keeps the working buffer below 100 bytes. Each glyph
    // is rasterized once, then scaled independently in X and Y into the TFT.
    static TFT_eSprite glyphScratch(&tft);
    static bool glyphScratchReady = false;
    if (!glyphScratchReady) {
        glyphScratch.setColorDepth(16);
        glyphScratchReady = glyphScratch.createSprite(6, 8) != nullptr;
        if (glyphScratchReady) {
            glyphScratch.setTextSize(1);
            glyphScratch.setTextColor(TFT_WHITE, TFT_BLACK);
            glyphScratch.setTextDatum(TL_DATUM);
        }
    }

    if (!glyphScratchReady) {
        // Low-memory fallback: retain legible text rather than dropping it.
        const uint8_t fallbackScale = textScale(object.fontSize);
        tft.setTextSize(fallbackScale);
        tft.setTextColor(color);
        tft.setTextDatum(TL_DATUM);
        tft.drawString(text, x, y + (height - 8 * fallbackScale) / 2, 1);
        tft.setTextSize(1);
        return;
    }

    const uint16_t* glyphPixels = static_cast<const uint16_t*>(glyphScratch.getPointer());
    if (glyphPixels == nullptr) return;
    const int clipLeft = x;
    const int clipTop = y;
    const int clipRight = x + width;
    const int clipBottom = y + height;

    tft.startWrite();
    for (const unsigned char* character = reinterpret_cast<const unsigned char*>(text);
         *character != 0; ++character) {
        glyphScratch.fillSprite(TFT_BLACK);
        glyphScratch.drawChar(static_cast<uint16_t>(*character), 0, 0, 1);

        for (int row = 0; row < 8; ++row) {
            int runStart = -1;
            for (int column = 0; column <= 6; ++column) {
                const bool pixelOn = column < 6 &&
                    glyphPixels[row * 6 + column] != TFT_BLACK;
                if (pixelOn && runStart < 0) {
                    runStart = column;
                } else if (!pixelOn && runStart >= 0) {
                    int left = static_cast<int>(floorf(cursorX + runStart * scaleX));
                    int right = static_cast<int>(ceilf(cursorX + column * scaleX));
                    int top = static_cast<int>(floorf(drawY + row * scaleY));
                    int bottom = static_cast<int>(ceilf(drawY + (row + 1) * scaleY));
                    if (left < clipLeft) left = clipLeft;
                    if (right > clipRight) right = clipRight;
                    if (top < clipTop) top = clipTop;
                    if (bottom > clipBottom) bottom = clipBottom;
                    if (right > left && bottom > top) {
                        tft.fillRect(left, top, right - left, bottom - top, color);
                        if (object.fontWeight >= 600 && left + 1 < clipRight) {
                            const int boldWidth = right - left;
                            tft.fillRect(left + 1, top,
                                         boldWidth > clipRight - left - 1
                                             ? clipRight - left - 1 : boldWidth,
                                         bottom - top, color);
                        }
                    }
                    runStart = -1;
                }
            }
        }
        cursorX += advance;
    }
    tft.endWrite();
}

void appendText(char* output, size_t capacity, const char* value) {
    if (output == nullptr || value == nullptr || capacity == 0) return;
    const size_t used = strlen(output);
    if (used >= capacity - 1) return;
    size_t count = strlen(value);
    if (count > capacity - 1 - used) count = capacity - 1 - used;
    memcpy(output + used, value, count);
    output[used + count] = '\0';
}

void appendNumber(char* output, size_t capacity, int value, const char* format) {
    char buffer[12] = {};
    snprintf(buffer, sizeof(buffer), format, value);
    appendText(output, capacity, buffer);
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
    const char* format = object.dataFormat[0] ? object.dataFormat : "HH:mm";
    const bool formatHasSeconds = strstr(format, "ss") != nullptr;
    out[0] = '\0';
    for (const char* cursor = format; *cursor != '\0';) {
        if (strncmp(cursor, "HH", 2) == 0) {
            appendNumber(out, capacity, hour, "%02d"); cursor += 2;
        } else if (strncmp(cursor, "mm", 2) == 0) {
            appendNumber(out, capacity, timeInfo.tm_min, "%02d"); cursor += 2;
        } else if (strncmp(cursor, "ss", 2) == 0) {
            appendNumber(out, capacity, timeInfo.tm_sec, "%02d"); cursor += 2;
        } else {
            char literal[2] = {*cursor++, '\0'};
            appendText(out, capacity, literal);
        }
    }
    if (object.showSeconds && !formatHasSeconds) {
        appendText(out, capacity, ":");
        appendNumber(out, capacity, timeInfo.tm_sec, "%02d");
    }
    if (twelveHour) appendText(out, capacity, suffix);
}

void formatDate(char* out, size_t capacity, const FaceObject& object,
                const struct tm& timeInfo) {
    static const char* const weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* const months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    const int weekday = (timeInfo.tm_wday >= 0 && timeInfo.tm_wday < 7) ? timeInfo.tm_wday : 0;
    const int month = (timeInfo.tm_mon >= 0 && timeInfo.tm_mon < 12) ? timeInfo.tm_mon : 0;
    const char* format = object.dataFormat[0] ? object.dataFormat : "ddd, DD MMM";
    out[0] = '\0';
    for (const char* cursor = format; *cursor != '\0';) {
        if (strncmp(cursor, "YYYY", 4) == 0) {
            appendNumber(out, capacity, timeInfo.tm_year + 1900, "%04d"); cursor += 4;
        } else if (strncmp(cursor, "ddd", 3) == 0) {
            appendText(out, capacity, weekdays[weekday]); cursor += 3;
        } else if (strncmp(cursor, "MMM", 3) == 0) {
            appendText(out, capacity, months[month]); cursor += 3;
        } else if (strncmp(cursor, "MM", 2) == 0) {
            appendNumber(out, capacity, month + 1, "%02d"); cursor += 2;
        } else if (strncmp(cursor, "DD", 2) == 0) {
            appendNumber(out, capacity, timeInfo.tm_mday, "%02d"); cursor += 2;
        } else {
            char literal[2] = {*cursor++, '\0'};
            appendText(out, capacity, literal);
        }
    }
}

void drawShape(const FaceObject& object) {
    const int x = static_cast<int>(lroundf(object.transform.x));
    const int y = static_cast<int>(lroundf(object.transform.y));
    const int w = static_cast<int>(lroundf(object.transform.width));
    const int h = static_cast<int>(lroundf(object.transform.height));
    const bool hasStroke = object.strokeWidth > 0;
    switch (object.shapeKind) {
        case 1:
            tft.fillCircle(x + w / 2, y + h / 2, min(w, h) / 2, object.fillColor);
            if (hasStroke) tft.drawCircle(x + w / 2, y + h / 2, min(w, h) / 2, object.strokeColor);
            break;
        case 2:
            tft.fillRoundRect(x, y, w, h, object.cornerRadius, object.fillColor);
            if (hasStroke) tft.drawRoundRect(x, y, w, h, object.cornerRadius, object.strokeColor);
            break;
        case 3:
            tft.drawLine(x, y, x + w, y + h, hasStroke ? object.strokeColor : object.fillColor);
            break;
        default:
            tft.fillRect(x, y, w, h, object.fillColor);
            if (hasStroke) tft.drawRect(x, y, w, h, object.strokeColor);
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
        const int inset = tick % 3 == 0 ? 9 : 5;
        const int innerX = cx + static_cast<int>(cosf(angle) * (radius - inset));
        const int innerY = cy + static_cast<int>(sinf(angle) * (radius - inset));
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

bool drawObject(const FaceModel& face, const FaceObject& object,
                const struct tm& timeInfo, const WeatherData& weather) {
    if (!object.visible) return true;
    switch (object.type) {
        case FaceElementType::Shape:
            drawShape(object);
            return true;
        case FaceElementType::AnalogClock:
            drawAnalogClock(object, timeInfo);
            return true;
        case FaceElementType::DigitalClock: {
            char value[FACE_TEXT_CAPACITY] = {};
            formatTime(value, sizeof(value), object, timeInfo);
            drawTextInBounds(value, object, object.textColor);
            return true;
        }
        case FaceElementType::Date: {
            char value[FACE_TEXT_CAPACITY] = {};
            formatDate(value, sizeof(value), object, timeInfo);
            drawTextInBounds(value, object, object.textColor);
            return true;
        }
        case FaceElementType::Weather: {
            if (!weather.valid) {
                drawTextInBounds(object.text[0] ? object.text : "Weather unavailable",
                                 object, object.textColor);
                return true;
            }
            if (object.showWeatherTemperature && object.showWeatherCondition) {
                char temperature[24] = {};
                snprintf(temperature, sizeof(temperature), "%d C", weather.currentTemp);
                FaceObject top = object;
                top.transform.height = object.transform.height * 0.58f;
                drawTextInBounds(temperature, top, object.textColor);
                FaceObject bottom = object;
                bottom.transform.y += object.transform.height * 0.48f;
                bottom.transform.height = object.transform.height * 0.52f;
                bottom.fontSize = object.fontSize > 12 ? object.fontSize / 2 : 8;
                drawTextInBounds(weather.conditionText, bottom, object.textColor);
            } else if (object.showWeatherCondition) {
                drawTextInBounds(weather.conditionText, object, object.textColor);
            } else {
                char temperature[24] = {};
                snprintf(temperature, sizeof(temperature), "%d C", weather.currentTemp);
                drawTextInBounds(temperature, object, object.textColor);
            }
            return true;
        }
        case FaceElementType::Text:
            drawTextInBounds(object.text, object, object.textColor);
            return true;
        case FaceElementType::Image:
            return drawImageElement(face, object);
    }
    return false;
}
}

FaceRenderStatus validateFaceModelForRenderer(const FaceModel& face) {
    return preflight(face);
}

FaceRenderStatus renderWatchFace(const FaceModel& face, const struct tm& timeInfo,
                                 const WeatherData& weather) {
    const FaceRenderStatus status = preflight(face);
    if (status != FaceRenderStatus::Ok) return status;

    tft.fillScreen(face.backgroundColor);
    uint8_t order[FACE_MAX_OBJECTS] = {};
    sortObjectIndices(face, order);
    for (uint8_t i = 0; i < face.objectCount; ++i) {
        if (!drawObject(face, face.objects[order[i]], timeInfo, weather)) return FaceRenderStatus::AssetReadFailed;
    }
    return FaceRenderStatus::Ok;
}

FaceRenderStatus renderWatchFaceDynamic(const FaceModel& face, const struct tm& timeInfo,
                                        const WeatherData& weather) {
    const FaceRenderStatus status = preflight(face);
    if (status != FaceRenderStatus::Ok) return status;

    uint8_t order[FACE_MAX_OBJECTS] = {};
    sortObjectIndices(face, order);

    for (uint8_t i = 0; i < face.objectCount; ++i) {
        const FaceObject& dynamicObject = face.objects[order[i]];
        if (!dynamicObject.visible || !isDynamic(dynamicObject.type)) continue;

        const int x = static_cast<int>(lroundf(dynamicObject.transform.x));
        const int y = static_cast<int>(lroundf(dynamicObject.transform.y));
        const int w = static_cast<int>(lroundf(dynamicObject.transform.width));
        const int h = static_cast<int>(lroundf(dynamicObject.transform.height));
        if (w <= 0 || h <= 0) continue;

        // Clear only the dynamic element's bounds, then redraw every layer
        // intersecting that region in z-order to preserve overlaps.
        tft.fillRect(x, y, w, h, face.backgroundColor);
        for (uint8_t j = 0; j < face.objectCount; ++j) {
            const FaceObject& candidate = face.objects[order[j]];
            if (candidate.visible && intersects(candidate, x, y, w, h)) {
                if (!drawObject(face, candidate, timeInfo, weather)) return FaceRenderStatus::AssetReadFailed;
            }
        }
    }
    return FaceRenderStatus::Ok;
}
