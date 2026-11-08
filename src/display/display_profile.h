#pragma once

#include <stdint.h>

// Compile-time profile selected by the board/display build configuration.
// The shared JSON profile is validated against these values in CI.
namespace DisplayProfileConfig {
static const char ID[] = "esp32-c3-gc9a01-240";
static const uint16_t WIDTH = 240;
static const uint16_t HEIGHT = 240;
static const uint16_t MAX_BITMAP_WIDTH = 240;
static const uint16_t MAX_BITMAP_HEIGHT = 240;
static const uint8_t MAX_OBJECTS = 16;
static const uint8_t MAX_ASSETS = 16;
static const bool IS_CIRCULAR = true;
}
