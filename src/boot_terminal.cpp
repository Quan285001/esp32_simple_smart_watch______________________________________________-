// boot_terminal.cpp
#include <Arduino.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

static int currentY = 42;
static constexpr int LINE_HEIGHT = 15;
static bool terminalDisplayActive = true;

// Stop drawing boot diagnostics over the normal watch UI.
void stopBootTerminalDisplay() {
    terminalDisplayActive = false;
}

void bootTerminalLog(const char* msg, uint16_t color);

/**
 * @brief Initializes the boot log screen and prints the initial startup diagnostics.

 */
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

/**
 * @brief Draws one boot message and writes it to the serial console.
 * @param msg Input or state used by this operation.
 * @param color Input or state used by this operation.
 */
void bootTerminalLog(const char* msg, uint16_t color) {
    // Keep runtime diagnostics in Serial only after the clock UI takes over.
    if (!terminalDisplayActive) {
        Serial.println(msg);
        return;
    }

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
