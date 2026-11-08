#include "face_manager.h"

#include <stdlib.h>
#include <string.h>

#include "../storage/storage_manager.h"
#include "../storage/face_storage.h"
#include "package_validator.h"
#include "../display/display_profile.h"

namespace FaceManager {
namespace {
constexpr size_t MAX_LAYOUT_FILE_BYTES = 16384;
FaceModel activeFace{};
FaceModel candidateFace{};
bool activeFaceValid = false;
bool activeFaceNeedsFullRender = false;
}

bool begin() {
    if (!StorageManager::isReady()) return false;

    const uint8_t slot = FaceStorage::activeSlot();
    if (slot <= 1) {
        fs::File packageFile = FaceStorage::openSlot(slot, "r");
        PackageValidationError packageError = PackageValidationError::None;
        if (packageFile) {
            memset(&candidateFace, 0, sizeof(candidateFace));
            const bool valid = validateWfsPackage(packageFile, packageFile.size(),
                                                   nullptr, nullptr, candidateFace, packageError);
            packageFile.close();
            if (valid) {
                activeFace = candidateFace;
                activeFaceValid = true;
                activeFaceNeedsFullRender = true;
                Serial.printf("[FACE] Restored package '%s' (%u objects).\\n",
                              activeFace.name, activeFace.objectCount);
                return true;
            }
        }
        Serial.printf("[FACE] Active package rejected: %s.\\n",
                      packageValidationErrorName(packageError));
        const uint8_t fallbackSlot = slot == 0 ? 1 : 0;
        fs::File fallbackFile = FaceStorage::openSlot(fallbackSlot, "r");
        if (fallbackFile) {
            memset(&candidateFace, 0, sizeof(candidateFace));
            PackageValidationError fallbackError = PackageValidationError::None;
            const bool fallbackValid = validateWfsPackage(
                fallbackFile, fallbackFile.size(), nullptr, nullptr,
                candidateFace, fallbackError);
            fallbackFile.close();
            if (fallbackValid && FaceStorage::activateSlot(fallbackSlot)) {
                activeFace = candidateFace;
                activeFaceValid = true;
                activeFaceNeedsFullRender = true;
                Serial.printf("[FACE] Recovered previous package from slot %u.\\n", fallbackSlot);
                return true;
            }
        }
    }

    if (StorageManager::exists("/face.json")) {
        const FaceParseResult result = loadJsonFile("/face.json");
        if (result) {
            Serial.printf("[FACE] Loaded raw test design '%s' (%u objects).\\n",
                          activeFace.name, activeFace.objectCount);
            return true;
        }
        Serial.printf("[FACE] Raw test design rejected: %s (object=%u).\\n",
                      faceParseErrorName(result.error), result.objectIndex);
    }

    Serial.println("[FACE] No valid installed custom face; using built-in fallback.");
    return false;
}

bool hasActiveFace() {
    return activeFaceValid;
}

bool setActiveModel(const FaceModel& model) {
    if (model.schemaVersion != 1 || model.objectCount > DisplayProfileConfig::MAX_OBJECTS || model.objectCount > FACE_MAX_OBJECTS ||
        model.assetCount > DisplayProfileConfig::MAX_ASSETS || model.assetCount > FACE_MAX_ASSETS ||
        model.canvasWidth != DisplayProfileConfig::WIDTH ||
        model.canvasHeight != DisplayProfileConfig::HEIGHT ||
        strcmp(model.deviceProfileId, DisplayProfileConfig::ID) != 0) return false;
    activeFace = model;
    activeFaceValid = true;
    activeFaceNeedsFullRender = true;
    return true;
}

void disableActive() {
    activeFaceValid = false;
    activeFaceNeedsFullRender = false;
}

const FaceModel* getActiveFace() {
    return activeFaceValid ? &activeFace : nullptr;
}

FaceParseResult loadJsonFile(const char* path) {
    if (!StorageManager::isReady() || path == nullptr) {
        return {FaceParseError::MissingField, 0};
    }

    fs::File file = StorageManager::open(path, "r");
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
    activeFaceNeedsFullRender = true;
    return result;
}

FaceRenderStatus renderActive(const struct tm& timeInfo, const WeatherData& weather) {
    if (!activeFaceValid) return FaceRenderStatus::UnsupportedTransform;
    const FaceRenderStatus status = renderWatchFace(activeFace, timeInfo, weather);
    if (status == FaceRenderStatus::Ok) activeFaceNeedsFullRender = false;
    return status;
}

FaceRenderStatus renderActiveDynamic(const struct tm& timeInfo, const WeatherData& weather) {
    if (!activeFaceValid) return FaceRenderStatus::UnsupportedTransform;
    if (activeFaceNeedsFullRender) return renderActive(timeInfo, weather);
    return renderWatchFaceDynamic(activeFace, timeInfo, weather);
}

} // namespace FaceManager
