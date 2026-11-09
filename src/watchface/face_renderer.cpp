#include "face_renderer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../display/display_canvas.h"
#include "../display/display_profile.h"
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
    const DisplayCanvas& canvas = getDisplayCanvas();
    if (face.objectCount > DisplayProfileConfig::MAX_OBJECTS || face.objectCount > FACE_MAX_OBJECTS ||
        face.assetCount > DisplayProfileConfig::MAX_ASSETS || face.assetCount > FACE_MAX_ASSETS ||
        face.canvasWidth != canvas.width() || face.canvasHeight != canvas.height()) {
        return FaceRenderStatus::UnsupportedTransform;
    }
    for (uint8_t i = 0; i < face.objectCount; ++i) {
        const FaceObject& object = face.objects[i];
        if (object.transform.rotation != 0.0f || object.transform.opacity < 1.0f ||
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
    getDisplayCanvas().drawText(
        text,
        static_cast<int>(lroundf(object.transform.x)),
        static_cast<int>(lroundf(object.transform.y)),
        static_cast<int>(lroundf(object.transform.width)),
        static_cast<int>(lroundf(object.transform.height)),
        color, object.fontSize, object.glyphScaleX, object.glyphScaleY,
        object.fontWeight, object.letterSpacing,
        object.textAlign == FaceTextAlign::Left ? CanvasTextAlignment::Left :
        object.textAlign == FaceTextAlign::Right ? CanvasTextAlignment::Right :
                                                   CanvasTextAlignment::Center);
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

struct DynamicTextCacheEntry {
    char objectId[FACE_ID_CAPACITY];
    char value[FACE_TEXT_CAPACITY];
    bool valid;
};

DynamicTextCacheEntry dynamicTextCache[FACE_MAX_OBJECTS] = {};

struct AnalogHandCacheEntry {
    char objectId[FACE_ID_CAPACITY];
    int centerX;
    int centerY;
    int endpointX[3];
    int endpointY[3];
    bool valid;
};

AnalogHandCacheEntry analogHandCache[FACE_MAX_OBJECTS] = {};

void invalidateDynamicTextCache() {
    memset(dynamicTextCache, 0, sizeof(dynamicTextCache));
    memset(analogHandCache, 0, sizeof(analogHandCache));
}

bool calculateAnalogHandEndpoints(const FaceObject& object, const struct tm& timeInfo,
                                  int& centerX, int& centerY,
                                  int (&endpointX)[3], int (&endpointY)[3]) {
    const int width = static_cast<int>(lroundf(object.transform.width));
    const int height = static_cast<int>(lroundf(object.transform.height));
    centerX = static_cast<int>(lroundf(object.transform.x)) + width / 2;
    centerY = static_cast<int>(lroundf(object.transform.y)) + height / 2;
    const int radius = (width < height ? width : height) / 2 - 2;
    if (radius < 8) return false;

    const float seconds = static_cast<float>(timeInfo.tm_sec);
    const float minutes = static_cast<float>(timeInfo.tm_min) + seconds / 60.0f;
    const float hours = static_cast<float>(timeInfo.tm_hour % 12) + minutes / 60.0f;
    const float angles[3] = {
        (hours * 30.0f - 90.0f) * PI_F / 180.0f,
        (minutes * 6.0f - 90.0f) * PI_F / 180.0f,
        (seconds * 6.0f - 90.0f) * PI_F / 180.0f
    };
    const float lengths[3] = {radius * 0.48f, radius * 0.72f, radius * 0.86f};
    for (int hand = 0; hand < 3; ++hand) {
        endpointX[hand] = centerX + static_cast<int>(cosf(angles[hand]) * lengths[hand]);
        endpointY[hand] = centerY + static_cast<int>(sinf(angles[hand]) * lengths[hand]);
    }
    return true;
}

bool analogHandsUnchanged(uint8_t objectIndex, const FaceObject& object,
                          const struct tm& timeInfo,
                          int& dirtyX, int& dirtyY, int& dirtyWidth, int& dirtyHeight) {
    dirtyX = dirtyY = dirtyWidth = dirtyHeight = 0;
    if (objectIndex >= FACE_MAX_OBJECTS) return false;

    int centerX = 0, centerY = 0, endpointX[3] = {}, endpointY[3] = {};
    if (!calculateAnalogHandEndpoints(object, timeInfo, centerX, centerY, endpointX, endpointY)) {
        return true;
    }

    AnalogHandCacheEntry& entry = analogHandCache[objectIndex];
    const bool sameObject = entry.valid && strcmp(entry.objectId, object.id) == 0;
    if (sameObject && entry.centerX == centerX && entry.centerY == centerY &&
        memcmp(entry.endpointX, endpointX, sizeof(endpointX)) == 0 &&
        memcmp(entry.endpointY, endpointY, sizeof(endpointY)) == 0) {
        return true;
    }

    int minX = centerX, maxX = centerX, minY = centerY, maxY = centerY;
    if (sameObject) {
        minX = entry.centerX < minX ? entry.centerX : minX;
        maxX = entry.centerX > maxX ? entry.centerX : maxX;
        minY = entry.centerY < minY ? entry.centerY : minY;
        maxY = entry.centerY > maxY ? entry.centerY : maxY;
        for (int hand = 0; hand < 3; ++hand) {
            if (entry.endpointX[hand] < minX) minX = entry.endpointX[hand];
            if (entry.endpointX[hand] > maxX) maxX = entry.endpointX[hand];
            if (entry.endpointY[hand] < minY) minY = entry.endpointY[hand];
            if (entry.endpointY[hand] > maxY) maxY = entry.endpointY[hand];
        }
    }
    for (int hand = 0; hand < 3; ++hand) {
        if (endpointX[hand] < minX) minX = endpointX[hand];
        if (endpointX[hand] > maxX) maxX = endpointX[hand];
        if (endpointY[hand] < minY) minY = endpointY[hand];
        if (endpointY[hand] > maxY) maxY = endpointY[hand];
    }

    const int padding = 4; // line thickness plus the center hub
    dirtyX = minX - padding;
    dirtyY = minY - padding;
    dirtyWidth = maxX - minX + padding * 2 + 1;
    dirtyHeight = maxY - minY + padding * 2 + 1;

    strncpy(entry.objectId, object.id, sizeof(entry.objectId) - 1);
    entry.objectId[sizeof(entry.objectId) - 1] = '\0';
    entry.centerX = centerX;
    entry.centerY = centerY;
    memcpy(entry.endpointX, endpointX, sizeof(endpointX));
    memcpy(entry.endpointY, endpointY, sizeof(endpointY));
    entry.valid = true;
    return false;
}

bool dynamicTextUnchanged(uint8_t objectIndex, const FaceObject& object,
                          const struct tm& timeInfo, const WeatherData& weather,
                          int& dirtyX, int& dirtyWidth) {
    dirtyX = static_cast<int>(lroundf(object.transform.x));
    dirtyWidth = static_cast<int>(lroundf(object.transform.width));
    if (objectIndex >= FACE_MAX_OBJECTS) return false;

    char value[FACE_TEXT_CAPACITY] = {};
    const bool textElement = object.type == FaceElementType::DigitalClock ||
                             object.type == FaceElementType::Date;
    switch (object.type) {
        case FaceElementType::DigitalClock:
            formatTime(value, sizeof(value), object, timeInfo);
            break;
        case FaceElementType::Date:
            formatDate(value, sizeof(value), object, timeInfo);
            break;
        case FaceElementType::Weather:
            if (!weather.valid) {
                snprintf(value, sizeof(value), "unavailable:%.244s", object.text);
            } else {
                snprintf(value, sizeof(value), "%d|%s|%u|%u",
                         weather.currentTemp, weather.conditionText,
                         object.showWeatherTemperature ? 1U : 0U,
                         object.showWeatherCondition ? 1U : 0U);
            }
            break;
        default:
            return false;
    }

    DynamicTextCacheEntry& entry = dynamicTextCache[objectIndex];
    const bool sameObject = entry.valid && strcmp(entry.objectId, object.id) == 0;
    if (sameObject && strcmp(entry.value, value) == 0) return true;

    // For fixed-width digital/date strings, repaint only the character cells
    // that changed. The clipped redraw still restores every intersecting
    // layer, but avoids blanking the entire clock box on each seconds tick.
    if (textElement && sameObject) {
        const size_t oldLength = strlen(entry.value);
        const size_t newLength = strlen(value);
        if (oldLength == newLength && newLength > 0) {
            size_t first = newLength;
            size_t last = 0;
            for (size_t index = 0; index < newLength; ++index) {
                if (entry.value[index] != value[index]) {
                    if (first == newLength) first = index;
                    last = index;
                }
            }
            if (first < newLength) {
                const float scaleX = (object.fontSize / 8.0f) * object.glyphScaleX;
                const float advance = 6.0f * scaleX + object.letterSpacing;
                const float textWidth = newLength * advance - object.letterSpacing;
                float textStart = object.transform.x;
                if (object.textAlign == FaceTextAlign::Center) {
                    textStart += (object.transform.width - textWidth) * 0.5f;
                } else if (object.textAlign == FaceTextAlign::Right) {
                    textStart += object.transform.width - textWidth;
                }
                const int boundsLeft = static_cast<int>(lroundf(object.transform.x));
                const int boundsRight = boundsLeft +
                    static_cast<int>(lroundf(object.transform.width));
                int left = static_cast<int>(floorf(textStart + first * advance)) - 1;
                int right = static_cast<int>(ceilf(textStart + (last + 1) * advance)) + 1;
                if (left < boundsLeft) left = boundsLeft;
                if (right > boundsRight) right = boundsRight;
                dirtyX = left;
                dirtyWidth = right > left ? right - left : 0;
            }
        }
    }

    strncpy(entry.objectId, object.id, sizeof(entry.objectId) - 1);
    entry.objectId[sizeof(entry.objectId) - 1] = '\0';
    strncpy(entry.value, value, sizeof(entry.value) - 1);
    entry.value[sizeof(entry.value) - 1] = '\0';
    entry.valid = true;
    return false;
}

void drawShape(const FaceObject& object) {
    const int x = static_cast<int>(lroundf(object.transform.x));
    const int y = static_cast<int>(lroundf(object.transform.y));
    const int w = static_cast<int>(lroundf(object.transform.width));
    const int h = static_cast<int>(lroundf(object.transform.height));
    const bool hasStroke = object.strokeWidth > 0;
    switch (object.shapeKind) {
        case 1:
            getDisplayCanvas().fillCircle(x + w / 2, y + h / 2, (w < h ? w : h) / 2, object.fillColor);
            if (hasStroke) getDisplayCanvas().drawCircle(x + w / 2, y + h / 2, (w < h ? w : h) / 2, object.strokeColor);
            break;
        case 2:
            getDisplayCanvas().fillRoundRect(x, y, w, h, object.cornerRadius, object.fillColor);
            if (hasStroke) getDisplayCanvas().drawRoundRect(x, y, w, h, object.cornerRadius, object.strokeColor);
            break;
        case 3:
            getDisplayCanvas().drawLine(x, y, x + w, y + h, hasStroke ? object.strokeColor : object.fillColor);
            break;
        default:
            getDisplayCanvas().fillRect(x, y, w, h, object.fillColor);
            if (hasStroke) getDisplayCanvas().drawRect(x, y, w, h, object.strokeColor);
            break;
    }
}

void drawAnalogClock(const FaceObject& object, const struct tm& timeInfo) {
    const int w = static_cast<int>(lroundf(object.transform.width));
    const int h = static_cast<int>(lroundf(object.transform.height));
    const int cx = static_cast<int>(lroundf(object.transform.x)) + w / 2;
    const int cy = static_cast<int>(lroundf(object.transform.y)) + h / 2;
    const int radius = (w < h ? w : h) / 2 - 2;
    if (radius < 8) return;

    const uint16_t color = object.textColor;
    getDisplayCanvas().drawCircle(cx, cy, radius, color);
    for (int tick = 0; tick < 12; ++tick) {
        const float angle = (tick * 30.0f - 90.0f) * PI_F / 180.0f;
        const int outerX = cx + static_cast<int>(cosf(angle) * (radius - 2));
        const int outerY = cy + static_cast<int>(sinf(angle) * (radius - 2));
        const int inset = tick % 3 == 0 ? 9 : 5;
        const int innerX = cx + static_cast<int>(cosf(angle) * (radius - inset));
        const int innerY = cy + static_cast<int>(sinf(angle) * (radius - inset));
        getDisplayCanvas().drawLine(innerX, innerY, outerX, outerY, color);
    }

    const float seconds = static_cast<float>(timeInfo.tm_sec);
    const float minutes = static_cast<float>(timeInfo.tm_min) + seconds / 60.0f;
    const float hours = static_cast<float>(timeInfo.tm_hour % 12) + minutes / 60.0f;
    const float hourAngle = (hours * 30.0f - 90.0f) * PI_F / 180.0f;
    const float minuteAngle = (minutes * 6.0f - 90.0f) * PI_F / 180.0f;
    const float secondAngle = (seconds * 6.0f - 90.0f) * PI_F / 180.0f;
    getDisplayCanvas().drawLine(cx, cy, cx + static_cast<int>(cosf(hourAngle) * radius * 0.48f),
                 cy + static_cast<int>(sinf(hourAngle) * radius * 0.48f), color);
    getDisplayCanvas().drawLine(cx, cy, cx + static_cast<int>(cosf(minuteAngle) * radius * 0.72f),
                 cy + static_cast<int>(sinf(minuteAngle) * radius * 0.72f), color);
    getDisplayCanvas().drawLine(cx, cy, cx + static_cast<int>(cosf(secondAngle) * radius * 0.86f),
                 cy + static_cast<int>(sinf(secondAngle) * radius * 0.86f), 0xF800);
    getDisplayCanvas().fillCircle(cx, cy, 3, color);
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
    invalidateDynamicTextCache();
    const FaceRenderStatus status = preflight(face);
    if (status != FaceRenderStatus::Ok) return status;

    getDisplayCanvas().clearScreen(face.backgroundColor);
    uint8_t order[FACE_MAX_OBJECTS] = {};
    sortObjectIndices(face, order);
    for (uint8_t i = 0; i < face.objectCount; ++i) {
        const FaceObject& object = face.objects[order[i]];
        if (!drawObject(face, object, timeInfo, weather)) return FaceRenderStatus::AssetReadFailed;
        if (object.visible && object.type == FaceElementType::AnalogClock) {
            int centerX = 0, centerY = 0, endpointX[3] = {}, endpointY[3] = {};
            if (calculateAnalogHandEndpoints(object, timeInfo, centerX, centerY, endpointX, endpointY) &&
                order[i] < FACE_MAX_OBJECTS) {
                AnalogHandCacheEntry& entry = analogHandCache[order[i]];
                strncpy(entry.objectId, object.id, sizeof(entry.objectId) - 1);
                entry.objectId[sizeof(entry.objectId) - 1] = '\0';
                entry.centerX = centerX;
                entry.centerY = centerY;
                memcpy(entry.endpointX, endpointX, sizeof(endpointX));
                memcpy(entry.endpointY, endpointY, sizeof(endpointY));
                entry.valid = true;
            }
        }
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
        // Skip unchanged dynamic text. Analog clocks calculate a dirty union
        // from the previous and current hand endpoints; text uses changed glyph cells.
        int x = 0, y = 0, w = 0, h = 0;
        if (dynamicObject.type == FaceElementType::AnalogClock) {
            if (analogHandsUnchanged(order[i], dynamicObject, timeInfo, x, y, w, h)) continue;
        } else {
            int dirtyX = 0;
            int dirtyWidth = 0;
            if (dynamicTextUnchanged(order[i], dynamicObject, timeInfo, weather,
                                     dirtyX, dirtyWidth)) continue;
            x = dirtyX;
            y = static_cast<int>(lroundf(dynamicObject.transform.y));
            w = dirtyWidth;
            h = static_cast<int>(lroundf(dynamicObject.transform.height));
        }
        if (w <= 0 || h <= 0) continue;

        // Clear only the dynamic element's bounds. A TFT viewport clips
        // every intersecting layer to that same region; without this, a large
        // lower-z image/shape could overwrite unrelated objects outside it.
        const int clipLeft = x < 0 ? 0 : x;
        const int clipTop = y < 0 ? 0 : y;
        const int displayWidth = getDisplayCanvas().width();
        const int displayHeight = getDisplayCanvas().height();
        const int clipRight = x + w > displayWidth ? displayWidth : x + w;
        const int clipBottom = y + h > displayHeight ? displayHeight : y + h;
        if (clipRight <= clipLeft || clipBottom <= clipTop) continue;

        getDisplayCanvas().setClipRect(clipLeft, clipTop,
                        clipRight - clipLeft, clipBottom - clipTop);
        getDisplayCanvas().fillRect(x, y, w, h, face.backgroundColor);
        bool drawOk = true;
        for (uint8_t j = 0; j < face.objectCount; ++j) {
            const FaceObject& candidate = face.objects[order[j]];
            if (candidate.visible && intersects(candidate, x, y, w, h) &&
                !drawObject(face, candidate, timeInfo, weather)) {
                drawOk = false;
                break;
            }
        }
        getDisplayCanvas().resetClip();
        if (!drawOk) return FaceRenderStatus::AssetReadFailed;
    }
    return FaceRenderStatus::Ok;
}
