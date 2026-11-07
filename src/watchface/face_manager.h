#pragma once

#include <time.h>
#include "../config.h"
#include "face_model.h"
#include "face_parser.h"
#include "face_renderer.h"

namespace FaceManager {

bool begin();
bool hasActiveFace();
void disableActive();
const FaceModel* getActiveFace();
FaceParseResult loadJsonFile(const char* path);
FaceRenderStatus renderActive(const struct tm& timeInfo, const WeatherData& weather);
FaceRenderStatus renderActiveDynamic(const struct tm& timeInfo, const WeatherData& weather);

} // namespace FaceManager
