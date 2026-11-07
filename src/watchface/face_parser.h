#pragma once

#include <stddef.h>
#include "face_model.h"

enum class FaceParseError : uint8_t {
    None,
    InputTooLarge,
    InvalidJson,
    UnsupportedSchema,
    MissingField,
    InvalidField,
    TooManyObjects,
    UnsupportedElement,
    UnsupportedProperty,
    BufferOverflow
};

struct FaceParseResult {
    FaceParseError error;
    uint8_t objectIndex;
    explicit operator bool() const { return error == FaceParseError::None; }
};

FaceParseResult parseWatchFaceJson(const char* json, size_t length, FaceModel& output);
const char* faceParseErrorName(FaceParseError error);
