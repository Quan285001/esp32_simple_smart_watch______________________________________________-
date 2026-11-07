#include "storage_manager.h"

#include <SPIFFS.h>

namespace StorageManager {
namespace {
bool mounted = false;
}

bool begin() {
    if (mounted) return true;

    // Never auto-format on mount failure: preserve user data for recovery.
    mounted = SPIFFS.begin(false);
    if (!mounted) {
        Serial.println("[STORAGE] SPIFFS mount failed; persistent face storage unavailable.");
        return false;
    }

    Serial.printf(
        "[STORAGE] SPIFFS mounted: total=%lu used=%lu free=%lu bytes.\n",
        static_cast<unsigned long>(SPIFFS.totalBytes()),
        static_cast<unsigned long>(SPIFFS.usedBytes()),
        static_cast<unsigned long>(SPIFFS.totalBytes() - SPIFFS.usedBytes())
    );
    return true;
}

bool isReady() {
    return mounted;
}

size_t totalBytes() {
    return mounted ? SPIFFS.totalBytes() : 0;
}

size_t usedBytes() {
    return mounted ? SPIFFS.usedBytes() : 0;
}

size_t freeBytes() {
    return mounted ? SPIFFS.totalBytes() - SPIFFS.usedBytes() : 0;
}

bool exists(const char* path) {
    return mounted && path != nullptr && SPIFFS.exists(path);
}

bool remove(const char* path) {
    return mounted && path != nullptr && SPIFFS.remove(path);
}

bool rename(const char* from, const char* to) {
    return mounted && from != nullptr && to != nullptr && SPIFFS.rename(from, to);
}

fs::File open(const char* path, const char* mode) {
    if (!mounted || path == nullptr || mode == nullptr) return fs::File();
    return SPIFFS.open(path, mode);
}

} // namespace StorageManager
