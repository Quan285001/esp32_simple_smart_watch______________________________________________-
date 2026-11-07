#include "face_manager.h"

#include <stdlib.h>
#include <string.h>

#include "../storage/storage_manager.h"

namespace FaceManager {
namespace {
constexpr size_t MAX_LAYOUT_FILE_BYTES = 16384;
FaceModel activeFace{};
FaceModel candidateFace{};
bool activeFaceValid = false;
}

bool begin() {
    if (!StorageManager::isReady()) return false;
    if (!StorageManager::exists("/face.json")) {
        Serial.println("[FACE] No installed custom face; using built-in fallback.");
        return false;
    }

    const FaceParseResult result = loadJsonFile("/face.json");
    if (!result) {
        Serial.printf("[FACE] Stored design rejected: %s (object=%u).\n",
                      faceParseErrorName(result.error), result.objectIndex);
        return false;
    }
    Serial.printf("[FACE] Loaded design '%s' (%u objects).\n",
                  activeFace.name, activeFace.objectCount);
    return true;
}

bool hasActiveFace() {
    return activeFaceValid;
}

void disableActive() {
    activeFaceValid = false;
}

const FaceModel* getActiveFace() {
    return activeFaceValid ? &activeFace : nullptr;
}

FaceParseResult loadJsonFile(const char* path) {
    if (!StorageManager::isReady() || path == nullptr) {
        return {FaceParseError::MissingField, 0};
    }

    File file = StorageManager::open(path, "r");
    if (!file) return {FaceParseError::MissingField, 0};
    const size_t length = file.size();
    if (length == 0 || length > MAX_LAYOUT_FILE_BYTES) {
        file.close();
        return {FaceParseError::InputTooLarge, 0};
    }

    char* buffer = static_cast<char*>(malloc(length + 1));
    if (buffer == nullptr) {
        file.close();
        return {FaceParseError::InputTooLarge, 0};
    }

    const size_t bytesRead = file.readBytes(buffer, length);
    file.close();
    if (bytesRead != length) {
        free(buffer);
        return {FaceParseError::InvalidJson, 0};
    }
    buffer[length] = '\0';

    const FaceParseResult result = parseWatchFaceJson(buffer, length, candidateFace);
    free(buffer);
    if (!result) return result;

    activeFace = candidateFace;
    activeFaceValid = true;
    return result;
}

FaceRenderStatus renderActive(const struct tm& timeInfo, const WeatherData& weather) {
    if (!activeFaceValid) return FaceRenderStatus::UnsupportedTransform;
    return renderWatchFace(activeFace, timeInfo, weather);
}

FaceRenderStatus renderActiveDynamic(const struct tm& timeInfo, const WeatherData& weather) {
    if (!activeFaceValid) return FaceRenderStatus::UnsupportedTransform;
    return renderWatchFaceDynamic(activeFace, timeInfo, weather);
}

} // namespace FaceManager
