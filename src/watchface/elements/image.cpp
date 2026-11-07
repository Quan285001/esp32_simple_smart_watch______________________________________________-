#include "image.h"

#include <TFT_eSPI.h>
#include <math.h>
#include <string.h>

#include "../../display/display_manager.h"
#include "../../storage/face_storage.h"

bool drawImageElement(const FaceModel& face, const FaceObject& object) {
    const FaceAsset* asset = nullptr;
    for (uint8_t i = 0; i < face.assetCount; ++i) {
        if (strcmp(face.assets[i].id, object.assetId) == 0) {
            asset = &face.assets[i];
            break;
        }
    }
    if (asset == nullptr || asset->width == 0 || asset->width > 240 ||
        asset->height == 0 || asset->height > 240) return false;

    const uint8_t slot = FaceStorage::activeSlot();
    if (slot > 1) return false;
    fs::File file = FaceStorage::openSlot(slot, "r");
    if (!file) return false;
    if (!file.seek(asset->packageOffset, SeekSet)) {
        file.close();
        return false;
    }

    const int x = static_cast<int>(lroundf(object.transform.x));
    const int y = static_cast<int>(lroundf(object.transform.y));
    uint16_t row[240] = {};
    const size_t rowBytes = static_cast<size_t>(asset->width) * sizeof(uint16_t);
    for (uint16_t rowIndex = 0; rowIndex < asset->height; ++rowIndex) {
        if (file.read(reinterpret_cast<uint8_t*>(row), rowBytes) != rowBytes) {
            file.close();
            return false;
        }
        tft.pushImage(x, y + rowIndex, asset->width, 1, row);
    }
    file.close();
    return true;
}
