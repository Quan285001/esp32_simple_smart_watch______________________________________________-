#pragma once

#include "../face_model.h"
#include <stdint.h>

bool drawImageElement(const FaceModel& face, const FaceObject& object);
bool drawImageElementMasked(const FaceModel& face, const FaceObject& object,
                            const uint8_t* pixelMask, uint16_t maskWidth,
                            uint16_t maskHeight);
