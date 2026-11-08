#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

enum class CanvasTextAlignment : uint8_t { Left, Center, Right };

// Logical drawing contract for Watch Face rendering. Physical display drivers
// implement this interface; face model/rendering code stays driver-agnostic.
class DisplayCanvas {
public:
    virtual ~DisplayCanvas() = default;
    virtual uint16_t width() const = 0;
    virtual uint16_t height() const = 0;
    virtual void clearScreen(uint16_t color) = 0;
    virtual void fillRect(int x, int y, int w, int h, uint16_t color) = 0;
    virtual void drawRect(int x, int y, int w, int h, uint16_t color) = 0;
    virtual void fillRoundRect(int x, int y, int w, int h, int radius, uint16_t color) = 0;
    virtual void drawRoundRect(int x, int y, int w, int h, int radius, uint16_t color) = 0;
    virtual void fillCircle(int x, int y, int radius, uint16_t color) = 0;
    virtual void drawCircle(int x, int y, int radius, uint16_t color) = 0;
    virtual void drawLine(int x0, int y0, int x1, int y1, uint16_t color) = 0;
    virtual void pushRgb565Row(int x, int y, uint16_t count, const uint16_t* pixels) = 0;
    virtual void setClipRect(int x, int y, int w, int h) = 0;
    virtual void resetClip() = 0;
    virtual void drawText(const char* text, int x, int y, int w, int h,
                          uint16_t color, float fontSize, float scaleX, float scaleY,
                          uint16_t fontWeight, float letterSpacing,
                          CanvasTextAlignment alignment) = 0;
};

// Transitional handle for legacy screens. New Watch Face modules should use
// DisplayCanvas and must not depend directly on TFT_eSPI.
extern TFT_eSPI tft;
DisplayCanvas& getDisplayCanvas();

bool beginDisplay(uint8_t rotation = 0);
bool isDisplayInitialized();
void blankDisplay();
void clearDisplay(uint16_t color);
