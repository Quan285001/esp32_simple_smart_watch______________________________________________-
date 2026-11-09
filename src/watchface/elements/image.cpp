#include "image.h"
#include "image_rgb565.h"

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
    uint8_t encodedRow[DisplayProfileConfig::MAX_BITMAP_WIDTH * 2] = {};
    uint16_t row[DisplayProfileConfig::MAX_BITMAP_WIDTH] = {};
    const size_t rowBytes = static_cast<size_t>(asset->width) * 2U;
    for (uint16_t rowIndex = 0; rowIndex < asset->height; ++rowIndex) {
        if (file.read(encodedRow, rowBytes) != rowBytes) {
            file.close();
            return false;
        }
        if (!decodeRgb565LittleEndianRow(encodedRow, rowBytes, row, asset->width)) {
            file.close();
            return false;
        }
        getDisplayCanvas().pushRgb565Row(x, y + rowIndex, asset->width, row);
    }
    file.close();
    return true;
}

bool drawImageElementMasked(const FaceModel& face, const FaceObject& object,
                            const uint8_t* pixelMask, uint16_t maskWidth,
                            uint16_t maskHeight) {
    if (pixelMask == nullptr || maskWidth == 0 || maskHeight == 0) return false;
    const FaceAsset* asset = nullptr;
    for (uint8_t i = 0; i < face.assetCount; ++i) {
        if (strcmp(face.assets[i].id, object.assetId) == 0) { asset = &face.assets[i]; break; }
    }
    if (asset == nullptr || asset->width == 0 || asset->width > DisplayProfileConfig::MAX_BITMAP_WIDTH ||
        asset->height == 0 || asset->height > DisplayProfileConfig::MAX_BITMAP_HEIGHT ||
        asset->byteLength != static_cast<size_t>(asset->width) * asset->height * sizeof(uint16_t)) return false;

    const int x = static_cast<int>(lroundf(object.transform.x));
    const int y = static_cast<int>(lroundf(object.transform.y));
    const int left = x < 0 ? 0 : x;
    const int top = y < 0 ? 0 : y;
    const int right = x + asset->width > maskWidth ? maskWidth : x + asset->width;
    const int bottom = y + asset->height > maskHeight ? maskHeight : y + asset->height;
    if (right <= left || bottom <= top) return true;

    const uint8_t slot = FaceStorage::activeSlot();
    if (slot > 1) return false;
    fs::File file = FaceStorage::openSlot(slot, "r");
    if (!file) return false;
    const size_t fileSize = file.size();
    if (asset->packageOffset > fileSize ||
        asset->byteLength > fileSize - asset->packageOffset) {
        file.close();
        return false;
    }

    const size_t rowBytes = static_cast<size_t>(asset->width) * 2U;
    uint8_t encodedRow[DisplayProfileConfig::MAX_BITMAP_WIDTH * 2] = {};
    uint16_t decodedRow[DisplayProfileConfig::MAX_BITMAP_WIDTH] = {};
    const size_t maskStride = (static_cast<size_t>(maskWidth) + 7U) / 8U;
    for (int screenY = top; screenY < bottom; ++screenY) {
        const int assetY = screenY - y;
        const int assetLeft = left - x;
        const int assetRight = right - x;
        bool rowHasPixels = false;
        for (int screenX = left; screenX < right; ++screenX) {
            const size_t index = static_cast<size_t>(screenY) * maskStride +
                                 static_cast<size_t>(screenX >> 3);
            if ((pixelMask[index] & static_cast<uint8_t>(1U << (screenX & 7))) != 0) {
                rowHasPixels = true;
                break;
            }
        }
        if (!rowHasPixels) continue;

        const size_t offset = asset->packageOffset +
                              static_cast<size_t>(assetY) * rowBytes;
        if (!file.seek(offset, fs::SeekSet) || file.read(encodedRow, rowBytes) != rowBytes ||
            !decodeRgb565LittleEndianRow(encodedRow, rowBytes, decodedRow, asset->width)) {
            file.close();
            return false;
        }

        int screenX = left;
        while (screenX < right) {
            auto isMarked = [&](int px) {
                const size_t index = static_cast<size_t>(screenY) * maskStride +
                                     static_cast<size_t>(px >> 3);
                return (pixelMask[index] & static_cast<uint8_t>(1U << (px & 7))) != 0;
            };
            while (screenX < right && !isMarked(screenX)) ++screenX;
            const int runStart = screenX;
            while (screenX < right && isMarked(screenX)) ++screenX;
            if (screenX > runStart) {
                const int sourceX = runStart - x;
                getDisplayCanvas().pushRgb565Row(runStart, screenY,
                    static_cast<uint16_t>(screenX - runStart), decodedRow + sourceX);
            }
        }
    }
    file.close();
    return true;
}
