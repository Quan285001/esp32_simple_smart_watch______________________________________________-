#pragma once

#include <Arduino.h>

constexpr uint8_t FACE_MAX_OBJECTS = 16;
constexpr size_t FACE_ID_CAPACITY = 65;
constexpr size_t FACE_NAME_CAPACITY = 33;
constexpr size_t FACE_TEXT_CAPACITY = 129;
constexpr size_t FACE_FONT_CAPACITY = 25;
constexpr size_t FACE_BINDING_CAPACITY = 25;
constexpr size_t FACE_FORMAT_CAPACITY = 41;
constexpr size_t FACE_ASSET_ID_CAPACITY = 33;

enum class FaceElementType : uint8_t {
    Text,
    DigitalClock,
    AnalogClock,
    Date,
    Weather,
    Shape,
    Image
};

enum class FaceTextAlign : uint8_t {
    Left,
    Center,
    Right
};

struct FaceTransform {
    float x;
    float y;
    float width;
    float height;
    float rotation;
    float opacity;
    int16_t zIndex;
};

struct FaceObject {
    char id[FACE_ID_CAPACITY];
    char name[FACE_NAME_CAPACITY];
    FaceElementType type;
    bool visible;
    FaceTransform transform;

    char text[FACE_TEXT_CAPACITY];
    char dataSource[FACE_BINDING_CAPACITY];
    char dataFormat[FACE_FORMAT_CAPACITY];
    char assetId[FACE_ASSET_ID_CAPACITY];

    char fontFamily[FACE_FONT_CAPACITY];
    uint16_t fontSize;
    uint16_t fontWeight;
    uint16_t textColor;
    FaceTextAlign textAlign;
    int8_t letterSpacing;

    uint16_t fillColor;
    uint16_t strokeColor;
    uint8_t strokeWidth;
    uint8_t shapeKind;
};

struct FaceModel {
    uint16_t schemaVersion;
    char designId[FACE_ID_CAPACITY];
    char name[FACE_NAME_CAPACITY];
    uint16_t canvasWidth;
    uint16_t canvasHeight;
    uint16_t backgroundColor;
    bool clipToShape;
    uint8_t objectCount;
    FaceObject objects[FACE_MAX_OBJECTS];
    bool containsImageAssets;
};
