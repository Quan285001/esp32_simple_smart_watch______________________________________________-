// boot_terminal.cpp
#include <Arduino.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

static int currentY = 42;
static constexpr int LINE_HEIGHT = 15;

void bootTerminalLog(const char* msg, uint16_t color);

void showBootTerminal() {
    currentY = 42;
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("=== START UP ===", 120, 18, 1);
    tft.drawFastHLine(25, 28, 190, TFT_DARKGREY);
    bootTerminalLog("[INIT] Kernel ready", TFT_WHITE);
    bootTerminalLog("[OK] GC9A01 display", TFT_GREEN);
    char heapMsg[32];
    snprintf(heapMsg, sizeof(heapMsg), "[MEM] Heap: %u KB", static_cast<unsigned int>(ESP.getFreeHeap() / 1024));
    bootTerminalLog(heapMsg, TFT_GREEN);
}

void bootTerminalLog(const char* msg, uint16_t color) {
    if (currentY > 220) {
        // Keep the latest boot stages visible on the small round display.
        tft.fillRect(0, 35, 240, 205, TFT_BLACK);
        currentY = 42;
    }
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(color, TFT_BLACK);
    tft.drawString(msg, 12, currentY, 1);
    currentY += LINE_HEIGHT;
    Serial.println(msg);
    delay(60);
}
