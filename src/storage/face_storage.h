#pragma once

#include <Arduino.h>
#include <SPIFFS.h>

namespace FaceStorage {

constexpr uint8_t INVALID_SLOT = 0xFF;

bool begin();
bool isReady();
uint8_t activeSlot();
uint8_t inactiveSlot();
const char* slotPath(uint8_t slot);
size_t slotSize(uint8_t slot);
bool removeSlot(uint8_t slot);
fs::File openSlot(uint8_t slot, const char* mode);
bool activateSlot(uint8_t slot);
bool restoreActiveSlot(uint8_t slot);

} // namespace FaceStorage
