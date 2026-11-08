#include "display_canvas.h"

#include <assert.h>

class TestCanvas final : public DisplayCanvas {
public:
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
    void pushRgb565Row(int, int, uint16_t, const uint16_t*) override {}
    void setClipRect(int, int, int, int) override {}
    void resetClip() override {}
    void drawText(const char*, int, int, int, int, uint16_t, float, float, float,
                  uint16_t, float, CanvasTextAlignment) override {}
};

int main() {
    TestCanvas canvas;
    assert(canvas.width() == 240);
    assert(canvas.height() == 240);
    canvas.clearScreen(0);
    canvas.fillRect(0, 0, 10, 10, 0xFFFF);
    canvas.drawText("test", 0, 0, 100, 20, 0xFFFF, 16.0f, 1.0f, 1.0f,
                    400, 0.0f, CanvasTextAlignment::Center);
    return 0;
}
