#include "image.h"
#include "display_canvas.h"
#include "face_storage.h"

#include <assert.h>
#include <stdint.h>
#include <iostream>
#include <vector>

namespace {
std::vector<uint8_t> packageBytes;
uint8_t selectedSlot = 0;

struct CapturedRow {
    int x;
    int y;
    std::vector<uint16_t> pixels;
};

class TestCanvas final : public DisplayCanvas {
public:
    std::vector<CapturedRow> rows;
    uint16_t width() const override { return 240; }
    uint16_t height() const override { return 240; }
    void clearScreen(uint16_t) override {}
    void fillRect(int, int, int, int, uint16_t) override {}
    void drawRect(int, int, int, int, uint16_t) override {}
    void fillRoundRect(int, int, int, int, int, uint16_t) override {}
    void drawRoundRect(int, int, int, int, int, uint16_t) override {}
    void fillCircle(int, int, int, uint16_t) override {}
    void drawCircle(int, int, int, uint16_t) override {}
    void drawLine(int, int, int, int, uint16_t) override {}
    void pushRgb565Row(int x, int y, uint16_t count, const uint16_t* pixels) override {
        CapturedRow row{x, y, {}};
        row.pixels.assign(pixels, pixels + count);
        rows.push_back(row);
    }
    void setClipRect(int, int, int, int) override {}
    void resetClip() override {}
    void drawText(const char*, int, int, int, int, uint16_t, float, float, float,
                  uint16_t, float, CanvasTextAlignment) override {}
};

TestCanvas canvas;
} // namespace

DisplayCanvas& getDisplayCanvas() { return canvas; }

namespace FaceStorage {
uint8_t activeSlot() { return selectedSlot; }
fs::File openSlot(uint8_t, const char*) {
    return fs::File(packageBytes.data(), packageBytes.size());
}
} // namespace FaceStorage

static FaceModel makeFace(uint32_t packageOffset = 0, uint32_t byteLength = 8) {
    FaceModel face{};
    face.assetCount = 1;
    face.assets[0].id[0] = 'i';
    face.assets[0].id[1] = 'm';
    face.assets[0].id[2] = 'g';
    face.assets[0].width = 2;
    face.assets[0].height = 2;
    face.assets[0].packageOffset = packageOffset;
    face.assets[0].byteLength = byteLength;
    return face;
}

static FaceObject makeImageObject() {
    FaceObject object{};
    object.type = FaceElementType::Image;
    object.transform.x = 11;
    object.transform.y = 23;
    object.transform.width = 2;
    object.transform.height = 2;
    object.assetId[0] = 'i';
    object.assetId[1] = 'm';
    object.assetId[2] = 'g';
    return object;
}

int main() {
    // A real 2x2 RGB565LE asset is read from the fake package file, decoded
    // row-by-row, and forwarded to the DisplayCanvas at the correct position.
    packageBytes = {
        0x00, 0xF8, 0xE0, 0x07, // red, green
        0x1F, 0x00, 0xFF, 0xFF  // blue, white
    };
    selectedSlot = 0;
    canvas.rows.clear();
    FaceModel face = makeFace();
    FaceObject object = makeImageObject();
    assert(drawImageElement(face, object));
    assert(canvas.rows.size() == 2);
    assert(canvas.rows[0].x == 11 && canvas.rows[0].y == 23);
    assert(canvas.rows[1].x == 11 && canvas.rows[1].y == 24);
    assert(canvas.rows[0].pixels.size() == 2);
    assert(canvas.rows[0].pixels[0] == 0xF800);
    assert(canvas.rows[0].pixels[1] == 0x07E0);
    assert(canvas.rows[1].pixels[0] == 0x001F);
    assert(canvas.rows[1].pixels[1] == 0xFFFF);

    // Reject malformed metadata and file ranges before touching the canvas.
    canvas.rows.clear();
    face = makeFace(0, 7);
    assert(!drawImageElement(face, object));
    assert(canvas.rows.empty());

    face = makeFace(7, 8);
    assert(!drawImageElement(face, object));
    assert(canvas.rows.empty());

    face = makeFace(0, 8);
    face.assets[0].width = 0;
    assert(!drawImageElement(face, object));
    assert(canvas.rows.empty());

    face = makeFace();
    selectedSlot = 2;
    assert(!drawImageElement(face, object));
    assert(canvas.rows.empty());

    std::cout << "WFS image element storage-to-canvas integration tests passed.\n";
    return 0;
}
