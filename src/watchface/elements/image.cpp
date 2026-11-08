#include "image.h"

#include <math.h>
#include <string.h>

#include "../../display/display_canvas.h"
#include "../../storage/face_storage.h"
#include "../../display/display_profile.h"

bool drawImageElement(const FaceModel& face, const FaceObject& object) {
    const FaceAsset* asset = nullptr;
    for (uint8_t i = 0; i < face.assetCount; ++i) {
        if (strcmp(face.assets[i].id, object.assetId) == 0) {
            asset = &face.assets[i];
            break;
        }
    }
    if (asset == nullptr || asset->width == 0 || asset->width > DisplayProfileConfig::MAX_BITMAP_WIDTH ||
        asset->height == 0 || asset->height > DisplayProfileConfig::MAX_BITMAP_HEIGHT ||
        asset->byteLength != static_cast<size_t>(asset->width) * asset->height * sizeof(uint16_t)) return false;

    const uint8_t slot = FaceStorage::activeSlot();
    if (slot > 1) return false;
    fs::File file = FaceStorage::openSlot(slot, "r");
    if (!file) return false;
    const size_t fileSize = file.size();
    if (asset->packageOffset > fileSize ||
        asset->byteLength > fileSize - asset->packageOffset ||
        !file.seek(asset->packageOffset, fs::SeekSet)) {
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
        getDisplayCanvas().pushRgb565Row(x, y + rowIndex, asset->width, row);
    }
    file.close();
    return true;
}
