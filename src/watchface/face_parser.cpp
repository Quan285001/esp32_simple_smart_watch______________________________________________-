#include "face_parser.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr size_t MAX_FACE_JSON_BYTES = 16384; // Conservative v1 ceiling; revise after heap profiling.

bool copyString(JsonVariantConst value, char* destination, size_t capacity, bool required) {
    if (destination == nullptr || capacity == 0) return false;
    destination[0] = '\0';
    if (value.isNull()) return !required;
    if (!value.is<const char*>()) return false;
    const char* source = value.as<const char*>();
    if (source == nullptr) return !required;
    const size_t length = strlen(source);
    if (length >= capacity) return false;
    memcpy(destination, source, length + 1);
    return true;
}

bool readNumber(JsonVariantConst value, float& output, bool required, float fallback = 0.0f) {
    if (value.isNull()) {
        output = fallback;
        return !required;
    }
    if (!value.is<float>() && !value.is<int>() && !value.is<unsigned int>()) return false;
    output = value.as<float>();
    return isfinite(output);
}


bool validFormatTokens(const char* format, bool dateFormat) {
    if (format == nullptr || format[0] == '\0') return true;
    for (const char* cursor = format; *cursor != '\0';) {
        if (dateFormat && strncmp(cursor, "YYYY", 4) == 0) cursor += 4;
        else if (dateFormat && strncmp(cursor, "ddd", 3) == 0) cursor += 3;
        else if (dateFormat && strncmp(cursor, "MMM", 3) == 0) cursor += 3;
        else if (dateFormat && (strncmp(cursor, "MM", 2) == 0 ||
                                strncmp(cursor, "DD", 2) == 0)) cursor += 2;
        else if (!dateFormat && (strncmp(cursor, "HH", 2) == 0 ||
                                 strncmp(cursor, "mm", 2) == 0 ||
                                 strncmp(cursor, "ss", 2) == 0)) cursor += 2;
        else {
            const unsigned char value = static_cast<unsigned char>(*cursor);
            if (value < 0x20 || value > 0x7E || isalpha(value)) return false;
            ++cursor;
        }
    }
    return true;
}

bool readColor(JsonVariantConst value, uint16_t& output, uint16_t fallback) {
    if (value.isNull()) {
        output = fallback;
        return true;
    }
    if (!value.is<const char*>()) return false;
    const char* hex = value.as<const char*>();
    if (hex == nullptr || strlen(hex) != 7 || hex[0] != '#') return false;
    char* end = nullptr;
    const unsigned long rgb = strtoul(hex + 1, &end, 16);
    if (end == nullptr || *end != '\0' || rgb > 0xFFFFFFUL) return false;
    const uint8_t red = static_cast<uint8_t>((rgb >> 16) & 0xFF);
    const uint8_t green = static_cast<uint8_t>((rgb >> 8) & 0xFF);
    const uint8_t blue = static_cast<uint8_t>(rgb & 0xFF);
    output = static_cast<uint16_t>(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
    return true;
}

bool parseType(const char* value, FaceElementType& type) {
    if (value == nullptr) return false;
    if (strcmp(value, "text") == 0) type = FaceElementType::Text;
    else if (strcmp(value, "digital-clock") == 0) type = FaceElementType::DigitalClock;
    else if (strcmp(value, "analog-clock") == 0) type = FaceElementType::AnalogClock;
    else if (strcmp(value, "date") == 0) type = FaceElementType::Date;
    else if (strcmp(value, "weather") == 0) type = FaceElementType::Weather;
    else if (strcmp(value, "shape") == 0) type = FaceElementType::Shape;
    else if (strcmp(value, "image") == 0) type = FaceElementType::Image;
    else return false;
    return true;
}


bool hasNonEmptyString(JsonVariantConst value) {
    if (value.isNull() || !value.is<const char*>()) return false;
    const char* text = value.as<const char*>();
    return text != nullptr && text[0] != '\\0';
}

bool hasOnlyKeys(JsonObjectConst object, const char* const* allowedKeys, size_t allowedCount) {
    for (JsonPairConst pair : object) {
        const char* key = pair.key().c_str();
        bool found = false;
        for (size_t i = 0; i < allowedCount; ++i) {
            if (strcmp(key, allowedKeys[i]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool numberDiffersFrom(JsonObjectConst object, const char* key, float expected) {
    if (!object.containsKey(key)) return false;
    const JsonVariantConst value = object[key];
    if (!value.is<float>() && !value.is<int>() && !value.is<unsigned int>()) return true;
    const float number = value.as<float>();
    return !isfinite(number) || fabsf(number - expected) > 0.001f;
}

bool hasUnsupportedObjectProperties(JsonObjectConst source) {
    static const char* const objectKeys[] = {
        "id", "type", "name", "visible", "locked", "transform", "style",
        "assetId", "shape", "dataBinding", "clockMode", "timeFormat",
        "showSeconds", "showAmPm", "handConfig", "text", "textStyle",
        "glyphOverrides", "glyphAtlasAssetId", "glyphFallback",
        "weatherFields", "children", "renderHints"
    };
    if (!hasOnlyKeys(source, objectKeys, sizeof(objectKeys) / sizeof(objectKeys[0]))) return true;

    static const char* const transformKeys[] = {
        "x", "y", "width", "height", "rotation", "opacity", "anchor", "zIndex"
    };
    static const char* const styleKeys[] = {
        "fill", "stroke", "strokeWidth", "backgroundColor", "cornerRadius"
    };
    static const char* const textStyleKeys[] = {
        "fontId", "fontFamily", "textAlign", "fontWeight", "fontSize",
        "width", "height", "scaleX", "scaleY", "fill", "stroke",
        "strokeWidth", "backgroundColor", "backgroundShape", "backgroundRadius",
        "opacity", "rotation", "letterSpacing", "visible", "zIndex"
    };
    static const char* const bindingKeys[] = {
        "source", "format", "timezone", "fallbackText", "visibleWhenMissing"
    };
    static const char* const handConfigKeys[] = {"hour", "minute", "second"};
    static const char* const renderHintKeys[] = {
        "preferredUpdateMs", "staticLayer", "requiresAntialias", "requiresBitmap"
    };

    JsonObjectConst transformObject = source["transform"].as<JsonObjectConst>();
    JsonObjectConst styleObject = source["style"].as<JsonObjectConst>();
    JsonObjectConst textStyleObject = source["textStyle"].as<JsonObjectConst>();
    JsonObjectConst bindingObject = source["dataBinding"].as<JsonObjectConst>();
    JsonObjectConst handConfigObject = source["handConfig"].as<JsonObjectConst>();
    JsonObjectConst renderHintsObject = source["renderHints"].as<JsonObjectConst>();
    if ((!transformObject.isNull() &&
         !hasOnlyKeys(transformObject, transformKeys, sizeof(transformKeys) / sizeof(transformKeys[0]))) ||
        (!styleObject.isNull() &&
         !hasOnlyKeys(styleObject, styleKeys, sizeof(styleKeys) / sizeof(styleKeys[0]))) ||
        (!textStyleObject.isNull() &&
         !hasOnlyKeys(textStyleObject, textStyleKeys, sizeof(textStyleKeys) / sizeof(textStyleKeys[0]))) ||
        (!bindingObject.isNull() &&
         !hasOnlyKeys(bindingObject, bindingKeys, sizeof(bindingKeys) / sizeof(bindingKeys[0]))) ||
        (!handConfigObject.isNull() &&
         !hasOnlyKeys(handConfigObject, handConfigKeys, sizeof(handConfigKeys) / sizeof(handConfigKeys[0]))) ||
        (!renderHintsObject.isNull() &&
         !hasOnlyKeys(renderHintsObject, renderHintKeys, sizeof(renderHintKeys) / sizeof(renderHintKeys[0])))) {
        return true;
    }

    JsonArrayConst children = source["children"].as<JsonArrayConst>();
    if (source.containsKey("children") && (children.isNull() || children.size() > 0)) return true;
    JsonObjectConst transform = source["transform"].as<JsonObjectConst>();
    if (!transform.isNull() && transform.containsKey("anchor")) {
        const char* anchor = transform["anchor"] | "";
        if (strcmp(anchor, "top-left") != 0) return true;
    }

    JsonObjectConst textStyle = source["textStyle"].as<JsonObjectConst>();
    if (!textStyle.isNull()) {
        if (textStyle.containsKey("fontId") || textStyle.containsKey("width") ||
            textStyle.containsKey("height")) return true;
        if ((textStyle.containsKey("stroke") &&
             (!textStyle["stroke"].is<const char*>() || hasNonEmptyString(textStyle["stroke"]))) ||
            (textStyle.containsKey("backgroundColor") &&
             (!textStyle["backgroundColor"].is<const char*>() ||
              hasNonEmptyString(textStyle["backgroundColor"])))) return true;
        if (numberDiffersFrom(textStyle, "opacity", 1.0f) ||
            numberDiffersFrom(textStyle, "rotation", 0.0f) ||
            numberDiffersFrom(textStyle, "strokeWidth", 0.0f) ||
            numberDiffersFrom(textStyle, "backgroundRadius", 0.0f) ||
            numberDiffersFrom(textStyle, "letterSpacing", 0.0f) ||
            numberDiffersFrom(textStyle, "zIndex", 0.0f)) return true;
        if (textStyle.containsKey("backgroundShape") &&
            (!textStyle["backgroundShape"].is<const char*>() ||
             strcmp(textStyle["backgroundShape"] | "none", "none") != 0)) return true;
        if (textStyle.containsKey("visible") &&
            (!textStyle["visible"].is<bool>() || !(textStyle["visible"] | true))) return true;
    }

    JsonArrayConst glyphOverrides = source["glyphOverrides"].as<JsonArrayConst>();
    if ((source.containsKey("glyphOverrides") && glyphOverrides.isNull()) ||
        (!glyphOverrides.isNull() && glyphOverrides.size() > 0)) return true;
    if (source.containsKey("glyphAtlasAssetId") &&
        !source["glyphAtlasAssetId"].is<const char*>()) return true;
    if (hasNonEmptyString(source["glyphAtlasAssetId"])) return true;
    if (source.containsKey("glyphFallback") &&
        (!source["glyphFallback"].is<const char*>() ||
         strcmp(source["glyphFallback"] | "compatibility-error", "compatibility-error") != 0)) return true;

    JsonObjectConst handConfig = source["handConfig"].as<JsonObjectConst>();
    if (source.containsKey("handConfig") && handConfig.isNull()) return true;
    if (!handConfig.isNull()) {
        for (JsonPairConst pair : handConfig) {
            const char* key = pair.key().c_str();
            if (strcmp(key, "hour") != 0 && strcmp(key, "minute") != 0 &&
                strcmp(key, "second") != 0) return true;
            JsonObjectConst hand = pair.value().as<JsonObjectConst>();
            if (hand.isNull() || hand.size() != 0) return true;
        }
    }

    JsonObjectConst binding = source["dataBinding"].as<JsonObjectConst>();
    if (source.containsKey("dataBinding") && binding.isNull()) return false;
    if (!binding.isNull()) {
        if ((binding.containsKey("timezone") &&
             (!binding["timezone"].is<const char*>() || hasNonEmptyString(binding["timezone"]))) ||
            (binding.containsKey("fallbackText") &&
             (!binding["fallbackText"].is<const char*>() || hasNonEmptyString(binding["fallbackText"]))) ||
            (binding.containsKey("visibleWhenMissing") &&
             (!binding["visibleWhenMissing"].is<bool>() || (binding["visibleWhenMissing"] | false)))) return true;
    }

    JsonObjectConst style = source["style"].as<JsonObjectConst>();
    if (!style.isNull()) {
        if (style.containsKey("backgroundColor") &&
            (!style["backgroundColor"].is<const char*>() || hasNonEmptyString(style["backgroundColor"]))) return true;
        if (style.containsKey("strokeWidth")) {
            const float width = style["strokeWidth"] | -1.0f;
            if (!isfinite(width) || (width != 0.0f && width != 1.0f)) return true;
        }
    }

    JsonObjectConst renderHints = source["renderHints"].as<JsonObjectConst>();
    if (source.containsKey("renderHints") && renderHints.isNull()) return true;
    if (!renderHints.isNull()) {
        if ((renderHints.containsKey("requiresAntialias") &&
             !renderHints["requiresAntialias"].is<bool>()) ||
            (renderHints.containsKey("requiresBitmap") &&
             !renderHints["requiresBitmap"].is<bool>()) ||
            (renderHints["requiresAntialias"] | false) ||
            (renderHints["requiresBitmap"] | false)) return true;
    }

    if (source.containsKey("shape") && !source["shape"].is<const char*>()) return true;
    const char* shape = source["shape"] | "rect";
    if (strcmp(shape, "arc") == 0) return true;

    JsonArrayConst weatherFields = source["weatherFields"].as<JsonArrayConst>();
    if (source.containsKey("weatherFields") && weatherFields.isNull()) return true;
    if (!weatherFields.isNull()) {
        for (JsonVariantConst field : weatherFields) {
            if (field.is<const char*>() && strcmp(field.as<const char*>(), "icon") == 0) {
                return true;
            }
        }
    }

    if (source.containsKey("clockMode") && !source["clockMode"].is<const char*>()) return true;
    if (source.containsKey("showAmPm") && !source["showAmPm"].is<bool>()) return true;
    if (source.containsKey("showSeconds") && !source["showSeconds"].is<bool>()) return true;
    const char* type = source["type"] | "";
    const char* clockMode = source["clockMode"] | "";
    if (clockMode[0] != '\\0') {
        if ((strcmp(type, "analog-clock") == 0 && strcmp(clockMode, "analog") != 0) ||
            (strcmp(type, "digital-clock") == 0 && strcmp(clockMode, "digital") != 0) ||
            (strcmp(type, "analog-clock") != 0 && strcmp(type, "digital-clock") != 0)) return true;
    }
    const char* timeFormat = source["timeFormat"] | "";
    if (timeFormat[0] != '\\0' &&
        strcmp(timeFormat, "12h") != 0 && strcmp(timeFormat, "24h") != 0) return true;
    if ((source["showAmPm"] | false) && strcmp(timeFormat, "12h") != 0) return true;
    if (source.containsKey("showAmPm") && !(source["showAmPm"] | false) &&
        strcmp(timeFormat, "12h") == 0) return true;

    return false;
}

bool parseObject(JsonObjectConst source, FaceObject& target) {
    memset(&target, 0, sizeof(target));
    if (!copyString(source["id"], target.id, sizeof(target.id), true) ||
        !copyString(source["name"], target.name, sizeof(target.name), true)) return false;
    if ((source.containsKey("visible") && !source["visible"].is<bool>()) ||
        (source.containsKey("locked") && !source["locked"].is<bool>())) return false;

    char typeName[24] = {};
    if (!copyString(source["type"], typeName, sizeof(typeName), true) ||
        !parseType(typeName, target.type)) return false;

    if (source.containsKey("clockMode")) {
        if (!source["clockMode"].is<const char*>()) return false;
        const char* mode = source["clockMode"].as<const char*>();
        if ((target.type == FaceElementType::DigitalClock &&
             strcmp(mode, "digital") != 0) ||
            (target.type == FaceElementType::AnalogClock &&
             strcmp(mode, "analog") != 0) ||
            (target.type != FaceElementType::DigitalClock &&
             target.type != FaceElementType::AnalogClock)) return false;
    }
    if (source.containsKey("showSeconds") &&
        (target.type != FaceElementType::DigitalClock ||
         !source["showSeconds"].is<bool>())) return false;
    if (source.containsKey("showAmPm") &&
        (target.type != FaceElementType::DigitalClock ||
         !source["showAmPm"].is<bool>())) return false;
    if (source.containsKey("weatherFields") && target.type != FaceElementType::Weather) return false;
    if (source.containsKey("shape") && target.type != FaceElementType::Shape) return false;

    target.visible = source["visible"].isNull() ? true : source["visible"].as<bool>();
    JsonObjectConst transform = source["transform"].as<JsonObjectConst>();
    if (transform.isNull()) return false;

    if (!readNumber(transform["x"], target.transform.x, true) ||
        !readNumber(transform["y"], target.transform.y, true) ||
        !readNumber(transform["width"], target.transform.width, true) ||
        !readNumber(transform["height"], target.transform.height, true) ||
        !readNumber(transform["rotation"], target.transform.rotation, false, 0.0f) ||
        !readNumber(transform["opacity"], target.transform.opacity, false, 1.0f)) return false;

    if (target.transform.width <= 0.0f || target.transform.width > 2048.0f ||
        target.transform.height <= 0.0f || target.transform.height > 2048.0f ||
        target.transform.x < -1024.0f || target.transform.x > 2048.0f ||
        target.transform.y < -1024.0f || target.transform.y > 2048.0f ||
        target.transform.rotation < -3600.0f || target.transform.rotation > 3600.0f ||
        target.transform.opacity < 0.0f || target.transform.opacity > 1.0f) return false;

    if (transform.containsKey("zIndex") &&
        !transform["zIndex"].is<int>() && !transform["zIndex"].is<long>()) return false;
    const int32_t zIndex = transform["zIndex"] | 0;
    if (zIndex < -32768 || zIndex > 32767) return false;
    target.transform.zIndex = static_cast<int16_t>(zIndex);

    JsonObjectConst textStyle = source["textStyle"].as<JsonObjectConst>();
    if (source.containsKey("textStyle") && textStyle.isNull()) return false;
    if (!readNumber(textStyle["fontSize"], target.fontSize, false, 16.0f)) return false;
    target.glyphScaleX = 1.0f;
    target.glyphScaleY = 1.0f;
    if (!readNumber(textStyle["scaleX"], target.glyphScaleX, false, 1.0f) ||
        !readNumber(textStyle["scaleY"], target.glyphScaleY, false, 1.0f)) return false;
    if (textStyle.containsKey("fontWeight") &&
        !textStyle["fontWeight"].is<int>() && !textStyle["fontWeight"].is<unsigned int>()) return false;
    if (textStyle.containsKey("textAlign") && !textStyle["textAlign"].is<const char*>()) return false;
    if (textStyle.containsKey("letterSpacing") &&
        !textStyle["letterSpacing"].is<float>() &&
        !textStyle["letterSpacing"].is<int>() &&
        !textStyle["letterSpacing"].is<unsigned int>()) return false;
    target.fontWeight = textStyle["fontWeight"] | 400;
    target.letterSpacing = textStyle["letterSpacing"] | 0;
    if (target.fontSize <= 0.0f || target.fontSize > 256.0f ||
        target.glyphScaleX < 0.1f || target.glyphScaleX > 8.0f ||
        target.glyphScaleY < 0.1f || target.glyphScaleY > 8.0f ||
        !(target.fontWeight == 300 || target.fontWeight == 400 ||
          target.fontWeight == 500 || target.fontWeight == 600 ||
          target.fontWeight == 700 || target.fontWeight == 800 ||
          target.fontWeight == 900) ||
        target.letterSpacing < -64 || target.letterSpacing > 128) return false;
    if (!copyString(textStyle["fontFamily"], target.fontFamily, sizeof(target.fontFamily), false) ||
        !readColor(textStyle["fill"], target.textColor, 0xFFFF)) return false;

    const char* align = textStyle["textAlign"] | "center";
    if (strcmp(align, "left") == 0) target.textAlign = FaceTextAlign::Left;
    else if (strcmp(align, "right") == 0) target.textAlign = FaceTextAlign::Right;
    else if (strcmp(align, "center") == 0) target.textAlign = FaceTextAlign::Center;
    else return false;

    if (!copyString(source["text"], target.text, sizeof(target.text), false)) return false;
    for (const unsigned char* character = reinterpret_cast<const unsigned char*>(target.text);
         *character != 0; ++character) {
        if (*character < 0x20 || *character > 0x7E) return false;
    }

    JsonObjectConst binding = source["dataBinding"].as<JsonObjectConst>();
    if (!binding.isNull()) {
        if (!copyString(binding["source"], target.dataSource, sizeof(target.dataSource), true) ||
            !copyString(binding["format"], target.dataFormat, sizeof(target.dataFormat), false)) return false;
    }

    JsonObjectConst style = source["style"].as<JsonObjectConst>();
    if (source.containsKey("style") && style.isNull()) return false;
    if (!readColor(style["fill"], target.fillColor, 0xFFFF) ||
        !readColor(style["stroke"], target.strokeColor, 0x0000)) return false;
    if (style.containsKey("strokeWidth") &&
        !style["strokeWidth"].is<float>() &&
        !style["strokeWidth"].is<int>() &&
        !style["strokeWidth"].is<unsigned int>()) return false;
    const float strokeWidth = style["strokeWidth"] | 0.0f;
    if (!isfinite(strokeWidth) || strokeWidth < 0.0f || strokeWidth > 64.0f) return false;
    target.strokeWidth = static_cast<uint8_t>(strokeWidth);
    if (style.containsKey("cornerRadius") &&
        !style["cornerRadius"].is<float>() && !style["cornerRadius"].is<int>()) return false;
    const float cornerRadius = style["cornerRadius"] | 0.0f;
    if (!isfinite(cornerRadius) || cornerRadius < 0.0f || cornerRadius > 255.0f) return false;
    target.cornerRadius = static_cast<uint8_t>(cornerRadius);

    if (source.containsKey("shape") && !source["shape"].is<const char*>()) return false;
    const char* shape = source["shape"] | "rect";
    if (strcmp(shape, "circle") == 0) target.shapeKind = 1;
    else if (strcmp(shape, "rounded-rect") == 0) target.shapeKind = 2;
    else if (strcmp(shape, "line") == 0) target.shapeKind = 3;
    else if (strcmp(shape, "rect") == 0) target.shapeKind = 0;
    else if (target.type == FaceElementType::Shape) return false;

    if (!copyString(source["timeFormat"], target.timeFormat, sizeof(target.timeFormat), false)) return false;
    target.showSeconds = source["showSeconds"] | false;
    target.showWeatherTemperature = true;
    target.showWeatherCondition = false;
    JsonArrayConst weatherFields = source["weatherFields"].as<JsonArrayConst>();
    if (source.containsKey("weatherFields") && weatherFields.isNull()) return false;
    if (!weatherFields.isNull()) {
        target.showWeatherTemperature = false;
        for (JsonVariantConst field : weatherFields) {
            if (!field.is<const char*>()) return false;
            const char* fieldName = field.as<const char*>();
            if (strcmp(fieldName, "temperature") == 0) target.showWeatherTemperature = true;
            else if (strcmp(fieldName, "condition") == 0) target.showWeatherCondition = true;
            else return false;
        }
    }

    if (target.type == FaceElementType::Image) {
        if (!copyString(source["assetId"], target.assetId, sizeof(target.assetId), true)) return false;
    }
    if ((target.type == FaceElementType::DigitalClock || target.type == FaceElementType::Date ||
         target.type == FaceElementType::Weather) && target.dataSource[0] == '\0') return false;
    if (target.dataSource[0] != '\0') {
        if ((target.type == FaceElementType::DigitalClock && strcmp(target.dataSource, "time") != 0) ||
            (target.type == FaceElementType::Date && strcmp(target.dataSource, "date") != 0) ||
            (target.type == FaceElementType::Weather && strcmp(target.dataSource, "temperature") != 0) ||
            (target.type != FaceElementType::DigitalClock && target.type != FaceElementType::Date &&
             target.type != FaceElementType::Weather)) return false;
    }
    if (target.timeFormat[0] != '\0' &&
        strcmp(target.timeFormat, "12h") != 0 && strcmp(target.timeFormat, "24h") != 0) return false;
    if (source.containsKey("showAmPm") &&
        !source["showAmPm"].as<bool>() &&
        strcmp(target.timeFormat, "12h") == 0) return false;
    if ((target.type == FaceElementType::DigitalClock &&
         !validFormatTokens(target.dataFormat, false)) ||
        (target.type == FaceElementType::Date &&
         !validFormatTokens(target.dataFormat, true))) return false;
    return true;
}
}

FaceParseResult parseWatchFaceJson(const char* json, size_t length, FaceModel& output) {
    memset(&output, 0, sizeof(output));
    if (json == nullptr || length == 0) return {FaceParseError::MissingField, 0};
    if (length > MAX_FACE_JSON_BYTES) return {FaceParseError::InputTooLarge, 0};

    JsonDocument document;
    const DeserializationError error = deserializeJson(
        document, json, length, DeserializationOption::NestingLimit(8)
    );
    if (error) return {FaceParseError::InvalidJson, 0};

    static const char* const rootKeys[] = {
        "schemaVersion", "designId", "name", "description", "deviceProfileId",
        "createdAt", "updatedAt", "canvas", "metadata", "objects"
    };
    JsonObjectConst root = document.as<JsonObjectConst>();
    if (!hasOnlyKeys(root, rootKeys, sizeof(rootKeys) / sizeof(rootKeys[0]))) {
        return {FaceParseError::UnsupportedProperty, 0};
    }

    if ((document["schemaVersion"] | 0) != 1) return {FaceParseError::UnsupportedSchema, 0};
    output.schemaVersion = 1;
    if (!copyString(document["designId"], output.designId, sizeof(output.designId), true) ||
        !copyString(document["deviceProfileId"], output.deviceProfileId, sizeof(output.deviceProfileId), true) ||
        !copyString(document["name"], output.name, sizeof(output.name), true)) {
        return {FaceParseError::BufferOverflow, 0};
    }

    JsonObjectConst canvas = document["canvas"].as<JsonObjectConst>();
    if (canvas.isNull()) return {FaceParseError::MissingField, 0};
    static const char* const canvasKeys[] = {
        "width", "height", "shape", "backgroundColor", "clipToShape", "safeArea"
    };
    if (!hasOnlyKeys(canvas, canvasKeys, sizeof(canvasKeys) / sizeof(canvasKeys[0]))) {
        return {FaceParseError::UnsupportedProperty, 0};
    }
    if (canvas.containsKey("safeArea")) {
        JsonObjectConst safeArea = canvas["safeArea"].as<JsonObjectConst>();
        static const char* const safeAreaKeys[] = {"top", "right", "bottom", "left"};
        if (safeArea.isNull() ||
            !hasOnlyKeys(safeArea, safeAreaKeys, sizeof(safeAreaKeys) / sizeof(safeAreaKeys[0]))) {
            return {FaceParseError::InvalidField, 0};
        }
    }
    if (canvas.containsKey("shape") && !canvas["shape"].is<const char*>()) {
        return {FaceParseError::InvalidField, 0};
    }
    if (canvas.containsKey("clipToShape") && !canvas["clipToShape"].is<bool>()) {
        return {FaceParseError::InvalidField, 0};
    }
    const char* canvasShape = canvas["shape"] | "circle";
    if (strcmp(canvasShape, "circle") != 0) return {FaceParseError::UnsupportedProperty, 0};
    output.canvasWidth = canvas["width"] | 0;
    output.canvasHeight = canvas["height"] | 0;
    if (output.canvasWidth != 240 || output.canvasHeight != 240) {
        return {FaceParseError::InvalidField, 0};
    }
    if (!readColor(canvas["backgroundColor"], output.backgroundColor, 0x0000)) {
        return {FaceParseError::InvalidField, 0};
    }
    output.clipToShape = canvas["clipToShape"] | true;

    JsonArrayConst objects = document["objects"].as<JsonArrayConst>();
    if (objects.isNull()) return {FaceParseError::MissingField, 0};
    if (objects.size() > FACE_MAX_OBJECTS) return {FaceParseError::TooManyObjects, 0};

    uint8_t index = 0;
    for (JsonObjectConst object : objects) {
        if (hasUnsupportedObjectProperties(object)) return {FaceParseError::UnsupportedProperty, index};
        if (!parseObject(object, output.objects[index])) {
            const char* type = object["type"] | "";
            if (strcmp(type, "text") != 0 && strcmp(type, "digital-clock") != 0 &&
                strcmp(type, "analog-clock") != 0 && strcmp(type, "date") != 0 &&
                strcmp(type, "weather") != 0 && strcmp(type, "shape") != 0 &&
                strcmp(type, "image") != 0) {
                return {FaceParseError::UnsupportedElement, index};
            }
            return {FaceParseError::InvalidField, index};
        }
        if (output.objects[index].type == FaceElementType::Image) output.containsImageAssets = true;
        ++index;
    }
    output.objectCount = index;
    return {FaceParseError::None, 0};
}

const char* faceParseErrorName(FaceParseError error) {
    switch (error) {
        case FaceParseError::None: return "NONE";
        case FaceParseError::InputTooLarge: return "INPUT_TOO_LARGE";
        case FaceParseError::InvalidJson: return "INVALID_JSON";
        case FaceParseError::UnsupportedSchema: return "UNSUPPORTED_SCHEMA";
        case FaceParseError::MissingField: return "MISSING_FIELD";
        case FaceParseError::InvalidField: return "INVALID_FIELD";
        case FaceParseError::TooManyObjects: return "TOO_MANY_OBJECTS";
        case FaceParseError::UnsupportedElement: return "UNSUPPORTED_ELEMENT";
        case FaceParseError::UnsupportedProperty: return "UNSUPPORTED_PROPERTY";
        case FaceParseError::BufferOverflow: return "BUFFER_OVERFLOW";
        default: return "UNKNOWN";
    }
}
