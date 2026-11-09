#pragma once

#include <stdint.h>

// WFS image payloads are always RGB565 little-endian, independent of the
// byte order used by the host or display backend.
inline uint16_t decodeRgb565LittleEndian(uint8_t lowByte, uint8_t highByte) {
    return static_cast<uint16_t>(lowByte) |
           (static_cast<uint16_t>(highByte) << 8);
}
