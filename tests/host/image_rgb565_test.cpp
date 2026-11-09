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
    std::cout << "WFS RGB565 little-endian decoder host tests passed.\\n";
    return 0;
}
