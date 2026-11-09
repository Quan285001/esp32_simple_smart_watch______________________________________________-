#include "image_rgb565.h"

#include <assert.h>
#include <stdint.h>
#include <iostream>

int main() {
    assert(decodeRgb565LittleEndian(0x00, 0xF8) == 0xF800); // red
    assert(decodeRgb565LittleEndian(0xE0, 0x07) == 0x07E0); // green
    assert(decodeRgb565LittleEndian(0x1F, 0x00) == 0x001F); // blue
    assert(decodeRgb565LittleEndian(0xFF, 0xFF) == 0xFFFF); // white
    assert(decodeRgb565LittleEndian(0x00, 0x00) == 0x0000); // black
    assert(decodeRgb565LittleEndian(0x34, 0x12) == 0x1234); // byte order

    // Exercise the same complete-row decoder used by the firmware renderer.
    const uint8_t encoded[] = {
        0x00, 0xF8, // red
        0xE0, 0x07, // green
        0x1F, 0x00  // blue
    };
    uint16_t pixels[3] = {};
    assert(decodeRgb565LittleEndianRow(encoded, sizeof(encoded), pixels, 3));
    assert(pixels[0] == 0xF800);
    assert(pixels[1] == 0x07E0);
    assert(pixels[2] == 0x001F);

    // A row must be complete and exact; never decode partial or extra bytes.
    assert(!decodeRgb565LittleEndianRow(encoded, sizeof(encoded) - 1, pixels, 3));
    assert(!decodeRgb565LittleEndianRow(encoded, sizeof(encoded) + 1, pixels, 3));
    assert(!decodeRgb565LittleEndianRow(nullptr, sizeof(encoded), pixels, 3));
    assert(!decodeRgb565LittleEndianRow(encoded, sizeof(encoded), nullptr, 3));
    assert(!decodeRgb565LittleEndianRow(encoded, sizeof(encoded), pixels, 0));
    assert(!decodeRgb565LittleEndianRow(encoded, sizeof(encoded), pixels,
                                        static_cast<size_t>(-1)));

    std::cout << "WFS RGB565 little-endian row decoder host tests passed.\\n";
    return 0;
}
