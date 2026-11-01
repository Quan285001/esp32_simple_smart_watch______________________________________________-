
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

extern TFT_eSPI tft;

void drawMenuScreen(int selectedIndex) {
    tft.fillScreen(TFT_BLACK);

    const int centerX = 120;

    // Tieu de
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("MENU", centerX, 20, 2);
    tft.drawFastHLine(60, 34, 120, 0x3186);

    // 3 muc menu
    const char* items[3] = {
        "Clock",
        "Weather",
        "Return"
    };

    const int startY = 65;
    const int itemHeight = 36;

    for (int i = 0; i < 3; i++) {
        int yPos = startY + i * itemHeight;

        uint16_t bgColor = TFT_BLACK;
        uint16_t textColor = 0xCE79;

        if (i == selectedIndex) {
            bgColor = 0x2104;
            textColor = 0xFDE0;

            tft.fillRoundRect(
                22, yPos - 10, 196, 27, 5, bgColor
            );

            tft.drawRoundRect(
                22, yPos - 10, 196, 27, 5, 0xFDE0
            );
        } else {
            tft.fillRoundRect(
                22, yPos - 10, 196, 27, 5, TFT_BLACK
            );
        }

        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(textColor, bgColor);
        tft.drawString(items[i], 32, yPos + 3, 2);
    }

    // Huong dan
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString(
        "[Press: Next | Hold: Select]",
        centerX, 202, 1
    );
}
