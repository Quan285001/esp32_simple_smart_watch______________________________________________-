#pragma once

#include <time.h>
#include "../config.h"
#include "face_model.h"

enum class FaceRenderStatus : uint8_t {
    Ok,
    UnsupportedTransform,
    UnsupportedAsset,
    UnsupportedFont
};

// Performs a complete frame render. Call on screen/design changes; a separate
// dirty-region scheduler will be introduced before second-by-second activation.
FaceRenderStatus renderWatchFace(const FaceModel& face, const struct tm& timeInfo,
                                 const WeatherData& weather);
