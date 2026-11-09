#pragma once

#include <stddef.h>
#include <stdint.h>

// WFS image payloads are always RGB565 little-endian, independent of the
// byte order used by the host or display backend.
inline uint16_t decodeRgb565LittleEndian(uint8_t lowByte, uint8_t highByte) {
    return static_cast<uint16_t>(lowByte) |
           (static_cast<uint16_t>(highByte) << 8);
}

// Decode exactly one complete RGB565LE row. Requiring an exact byte count
// prevents callers from silently accepting truncated or oversized rows.
inline bool decodeRgb565LittleEndianRow(const uint8_t* encoded,
                                        size_t encodedLength,
                                        uint16_t* pixels,
                                        size_t pixelCount) {
    if (encoded == nullptr || pixels == nullptr || pixelCount == 0 ||
        pixelCount > static_cast<size_t>(-1) / 2U ||
        encodedLength != pixelCount * 2U) {
        return false;
    }
    for (size_t i = 0; i < pixelCount; ++i) {
        pixels[i] = decodeRgb565LittleEndian(encoded[i * 2U], encoded[i * 2U + 1U]);
    }
    return true;
}
