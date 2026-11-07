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

FaceRenderStatus renderWatchFace(const FaceModel& face, const struct tm& timeInfo,
                                 const WeatherData& weather);
FaceRenderStatus renderWatchFaceDynamic(const FaceModel& face, const struct tm& timeInfo,
                                        const WeatherData& weather);
