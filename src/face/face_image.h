#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

/**
 * Binary Face Studio image protocol:
 * 0xF0 BEGIN: version, width, height, byte length, CRC32 (little endian)
 * 0xF1 DATA:  offset (uint32 little endian), followed by RGB565 bytes
 * 0xF2 END:   finalize, verify CRC and atomically activate staging slot
 * 0xF3 CLEAR: clear the active photo background
 */
void handleFaceImagePacket(const uint8_t* packet, size_t length);
bool drawFaceImageToSprite(TFT_eSprite& sprite);
