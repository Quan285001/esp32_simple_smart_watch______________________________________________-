#include "display_manager.h"
#include "display_profile.h"

#include <math.h>
#include <string.h>

TFT_eSPI tft = TFT_eSPI();

namespace {
bool displayInitialized = false;
uint8_t activeRotation = 0;

uint8_t fallbackTextScale(float fontSize) {
    int scale = static_cast<int>(lroundf(fontSize / 8.0f));
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    return static_cast<uint8_t>(scale);
}

class TftDisplayCanvas final : public DisplayCanvas {
public:
    uint16_t width() const override { return static_cast<uint16_t>(tft.width()); }
    uint16_t height() const override { return static_cast<uint16_t>(tft.height()); }
    void clearScreen(uint16_t color) override { tft.fillScreen(color); }
    void fillRect(int x, int y, int w, int h, uint16_t color) override { tft.fillRect(x, y, w, h, color); }
    void drawRect(int x, int y, int w, int h, uint16_t color) override { tft.drawRect(x, y, w, h, color); }
    void fillRoundRect(int x, int y, int w, int h, int radius, uint16_t color) override {
        tft.fillRoundRect(x, y, w, h, radius, color);
    }
    void drawRoundRect(int x, int y, int w, int h, int radius, uint16_t color) override {
        tft.drawRoundRect(x, y, w, h, radius, color);
    }
    void fillCircle(int x, int y, int radius, uint16_t color) override { tft.fillCircle(x, y, radius, color); }
    void drawCircle(int x, int y, int radius, uint16_t color) override { tft.drawCircle(x, y, radius, color); }
    void drawLine(int x0, int y0, int x1, int y1, uint16_t color) override {
        tft.drawLine(x0, y0, x1, y1, color);
    }
    void pushRgb565Row(int x, int y, uint16_t count, const uint16_t* pixels) override {
        if (pixels == nullptr || count == 0) return;

        // WFS assets are decoded from RGB565LE bytes into native uint16_t
        // colour values. ESP32 is little-endian, so the in-memory bytes are
        // low-byte first; the GC9A01 SPI stream requires the RGB565 high byte
        // first. TFT_eSPI's swap path writes each uint16_t as MSB then LSB.
        // Do not copy TFT_eSprite's false setting: sprite storage has a
        // different byte-layout contract from this decoded native-word row.
        const bool previousSwapBytes = tft.getSwapBytes();
        tft.setSwapBytes(true);
        tft.pushImage(x, y, count, 1, pixels);
        tft.setSwapBytes(previousSwapBytes);
    }
    void setClipRect(int x, int y, int w, int h) override { tft.setViewport(x, y, w, h, false); }
    void resetClip() override { tft.resetViewport(); }

    void drawText(const char* text, int x, int y, int w, int h,
                  uint16_t color, float fontSize, float scaleX, float scaleY,
                  uint16_t fontWeight, float letterSpacing,
                  CanvasTextAlignment alignment) override {
        if (text == nullptr || text[0] == '\0' || w <= 0 || h <= 0) return;
        const float glyphScaleX = (fontSize / 8.0f) * scaleX;
        const float glyphScaleY = (fontSize / 8.0f) * scaleY;
        const float advance = 6.0f * glyphScaleX + letterSpacing;
        const float textWidth = strlen(text) * advance - letterSpacing;
        float cursorX = static_cast<float>(x);
        if (alignment == CanvasTextAlignment::Center) cursorX += (w - textWidth) * 0.5f;
        else if (alignment == CanvasTextAlignment::Right) cursorX += w - textWidth;
        const float drawY = y + (h - 8.0f * glyphScaleY) * 0.5f;

        // GC9A01 backend: rasterize one 6x8 glyph at a time to keep scratch
        // memory bounded. Other backends can implement native font rendering.
        static TFT_eSprite glyphScratch(&tft);
        static bool glyphScratchReady = false;
        if (!glyphScratchReady) {
            glyphScratch.setColorDepth(16);
            glyphScratchReady = glyphScratch.createSprite(6, 8) != nullptr;
            if (glyphScratchReady) {
                glyphScratch.setTextSize(1);
                glyphScratch.setTextColor(TFT_WHITE, TFT_BLACK);
                glyphScratch.setTextDatum(TL_DATUM);
            }
        }
        if (!glyphScratchReady) {
            const uint8_t fallbackScale = fallbackTextScale(fontSize);
            tft.setTextSize(fallbackScale);
            tft.setTextColor(color);
            tft.setTextDatum(TL_DATUM);
            tft.drawString(text, x, y + (h - 8 * fallbackScale) / 2, 1);
            tft.setTextSize(1);
            return;
        }

        const uint16_t* glyphPixels = static_cast<const uint16_t*>(glyphScratch.getPointer());
        if (glyphPixels == nullptr) return;
        const int clipLeft = x, clipTop = y, clipRight = x + w, clipBottom = y + h;
        tft.startWrite();
        for (const unsigned char* character = reinterpret_cast<const unsigned char*>(text);
             *character != 0; ++character) {
            glyphScratch.fillSprite(TFT_BLACK);
            glyphScratch.drawChar(static_cast<uint16_t>(*character), 0, 0, 1);
            for (int row = 0; row < 8; ++row) {
                int runStart = -1;
                for (int column = 0; column <= 6; ++column) {
                    const bool pixelOn = column < 6 && glyphPixels[row * 6 + column] != TFT_BLACK;
                    if (pixelOn && runStart < 0) runStart = column;
                    else if (!pixelOn && runStart >= 0) {
                        int left = static_cast<int>(floorf(cursorX + runStart * glyphScaleX));
                        int right = static_cast<int>(ceilf(cursorX + column * glyphScaleX));
                        int top = static_cast<int>(floorf(drawY + row * glyphScaleY));
                        int bottom = static_cast<int>(ceilf(drawY + (row + 1) * glyphScaleY));
                        if (left < clipLeft) left = clipLeft;
                        if (right > clipRight) right = clipRight;
                        if (top < clipTop) top = clipTop;
                        if (bottom > clipBottom) bottom = clipBottom;
                        if (right > left && bottom > top) {
                            tft.fillRect(left, top, right - left, bottom - top, color);
                            if (fontWeight >= 600 && left + 1 < clipRight) {
                                const int boldWidth = right - left;
                                tft.fillRect(left + 1, top,
                                    boldWidth > clipRight - left - 1 ? clipRight - left - 1 : boldWidth,
                                    bottom - top, color);
                            }
                        }
                        runStart = -1;
                    }
                }
            }
            cursorX += advance;
        }
        tft.endWrite();
    }
};

TftDisplayCanvas displayCanvas;
}

DisplayCanvas& getDisplayCanvas() { return displayCanvas; }

bool beginDisplay(uint8_t rotation) {
    if (displayInitialized) {
        if (rotation != activeRotation) {
            tft.setRotation(rotation);
            activeRotation = rotation;
        }
        return true;
    }
    tft.init();
    tft.setRotation(rotation);
    if (tft.width() != DisplayProfileConfig::WIDTH ||
        tft.height() != DisplayProfileConfig::HEIGHT) {
        Serial.printf("[DISPLAY] Profile mismatch: expected %ux%u, driver reports %ux%u.\\n",
                      static_cast<unsigned>(DisplayProfileConfig::WIDTH),
                      static_cast<unsigned>(DisplayProfileConfig::HEIGHT),
                      static_cast<unsigned>(tft.width()), static_cast<unsigned>(tft.height()));
        return false;
    }
    tft.fillScreen(TFT_BLACK);
    activeRotation = rotation;
    displayInitialized = true;
    Serial.printf("[DISPLAY] %s initialized (%ux%u).\\n",
                  DisplayProfileConfig::ID, static_cast<unsigned>(tft.width()),
                  static_cast<unsigned>(tft.height()));
    return true;
}

bool isDisplayInitialized() { return displayInitialized; }
void blankDisplay() { if (displayInitialized) getDisplayCanvas().clearScreen(TFT_BLACK); }
void clearDisplay(uint16_t color) { if (displayInitialized) getDisplayCanvas().clearScreen(color); }
