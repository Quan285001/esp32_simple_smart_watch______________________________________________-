#pragma once

#include <Arduino.h>
#include <SPIFFS.h>

namespace StorageManager {

bool begin();
bool isReady();
size_t totalBytes();
size_t usedBytes();
size_t freeBytes();
bool exists(const char* path);
bool remove(const char* path);
bool rename(const char* from, const char* to);
fs::File open(const char* path, const char* mode);

} // namespace StorageManager
