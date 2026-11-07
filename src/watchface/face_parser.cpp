#include "face_parser.h"

#include <ArduinoJson.h>
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

bool parseObject(JsonObjectConst source, FaceObject& target) {
    memset(&target, 0, sizeof(target));
    if (!copyString(source["id"], target.id, sizeof(target.id), true) ||
        !copyString(source["name"], target.name, sizeof(target.name), true)) return false;

    char typeName[24] = {};
    if (!copyString(source["type"], typeName, sizeof(typeName), true) ||
        !parseType(typeName, target.type)) return false;

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

    const int32_t zIndex = transform["zIndex"] | 0;
    if (zIndex < -32768 || zIndex > 32767) return false;
    target.transform.zIndex = static_cast<int16_t>(zIndex);

    JsonObjectConst textStyle = source["textStyle"].as<JsonObjectConst>();
    target.fontSize = textStyle["fontSize"] | 16;
    target.fontWeight = textStyle["fontWeight"] | 400;
    target.letterSpacing = textStyle["letterSpacing"] | 0;
    if (target.fontSize == 0 || target.fontSize > 256 ||
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

    JsonObjectConst binding = source["dataBinding"].as<JsonObjectConst>();
    if (!binding.isNull()) {
        if (!copyString(binding["source"], target.dataSource, sizeof(target.dataSource), true) ||
            !copyString(binding["format"], target.dataFormat, sizeof(target.dataFormat), false)) return false;
    }

    JsonObjectConst style = source["style"].as<JsonObjectConst>();
    if (!readColor(style["fill"], target.fillColor, 0xFFFF) ||
        !readColor(style["stroke"], target.strokeColor, 0x0000)) return false;
    const float strokeWidth = style["strokeWidth"] | 0.0f;
    if (!isfinite(strokeWidth) || strokeWidth < 0.0f || strokeWidth > 64.0f) return false;
    target.strokeWidth = static_cast<uint8_t>(strokeWidth);
    const float cornerRadius = style["cornerRadius"] | 0.0f;
    if (!isfinite(cornerRadius) || cornerRadius < 0.0f || cornerRadius > 255.0f) return false;
    target.cornerRadius = static_cast<uint8_t>(cornerRadius);

    const char* shape = source["shape"] | "rect";
    if (strcmp(shape, "circle") == 0) target.shapeKind = 1;
    else if (strcmp(shape, "rounded-rect") == 0) target.shapeKind = 2;
    else if (strcmp(shape, "line") == 0) target.shapeKind = 3;
    else if (strcmp(shape, "arc") == 0) target.shapeKind = 4;
    else if (strcmp(shape, "rect") == 0) target.shapeKind = 0;
    else if (target.type == FaceElementType::Shape) return false;

    if (!copyString(source["timeFormat"], target.timeFormat, sizeof(target.timeFormat), false)) return false;
    target.showSeconds = source["showSeconds"] | false;

    if (target.type == FaceElementType::Image) {
        if (!copyString(source["assetId"], target.assetId, sizeof(target.assetId), true)) return false;
    }
    if ((target.type == FaceElementType::DigitalClock || target.type == FaceElementType::Date ||
         target.type == FaceElementType::Weather) && target.dataSource[0] == '\0') return false;
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

    if ((document["schemaVersion"] | 0) != 1) return {FaceParseError::UnsupportedSchema, 0};
    output.schemaVersion = 1;
    if (!copyString(document["designId"], output.designId, sizeof(output.designId), true) ||
        !copyString(document["name"], output.name, sizeof(output.name), true)) {
        return {FaceParseError::BufferOverflow, 0};
    }

    JsonObjectConst canvas = document["canvas"].as<JsonObjectConst>();
    if (canvas.isNull()) return {FaceParseError::MissingField, 0};
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
        case FaceParseError::BufferOverflow: return "BUFFER_OVERFLOW";
        default: return "UNKNOWN";
    }
}
