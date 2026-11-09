#pragma once

#include <stdint.h>
#include <string.h>

namespace WfsDigest {

inline int hexNibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

// Parse exactly 64 hexadecimal characters. Output is left untouched on failure.
inline bool parseSha256Hex(const char* hex, uint8_t output[32]) {
    if (hex == nullptr || output == nullptr || strlen(hex) != 64) return false;
    uint8_t parsed[32] = {};
    for (size_t i = 0; i < 32; ++i) {
        const int high = hexNibble(hex[i * 2]);
        const int low = hexNibble(hex[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        parsed[i] = static_cast<uint8_t>((high << 4) | low);
    }
    memcpy(output, parsed, sizeof(parsed));
    return true;
}

} // namespace WfsDigest
