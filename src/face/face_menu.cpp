
#include <Arduino.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

struct MenuItem {
    const char* label;
    bool hasStatus;
    bool status;
};

void drawMenuScreen(int selectedIndex, bool wifiStatus, bool wifiConnected) {
    tft.fillScreen(0x0000);

    int centerX = 120;

    // Tieu de MENU
    tft.setTextColor(0xFFFF, 0x0000);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("MENU", centerX, 20, 2);
    tft.drawFastHLine(60, 34, 120, 0x3186);

    MenuItem items[4] = {
        {"Clock",       false, false},
        {"Weather",     false, false},
        {"Wi-Fi Mode",  true,  wifiStatus},
        {"Return",      false, false}
    };

    int startY = 62;
    int itemHeight = 32;

    for (int i = 0; i < 4; i++) {
        int yPos = startY + (i * itemHeight);

        uint16_t bgColor = 0x0000;
        uint16_t textColor = 0xCE79;

        if (i == selectedIndex) {
            bgColor = 0x2104;
            textColor = 0xFDE0;

            tft.fillRoundRect(22, yPos - 10, 196, 24, 5, bgColor);
            tft.drawRoundRect(22, yPos - 10, 196, 24, 5, 0xFDE0);
        } else {
            tft.fillRoundRect(22, yPos - 10, 196, 24, 5, bgColor);
        }

        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(textColor, bgColor);
        tft.drawString(items[i].label, 32, yPos, 2);

        if (items[i].hasStatus) {
            tft.setTextDatum(MR_DATUM);

            uint16_t statusColor = wifiStatus ? 0x07E0 : 0xF800;

            tft.setTextColor(statusColor, bgColor);
            tft.drawString(wifiStatus ? "ON" : "OFF", 205, yPos, 2);
        }
    }

    // Trang thai ket noi Wi-Fi
    tft.setTextDatum(MC_DATUM);

    uint16_t connectionColor =
        !wifiStatus ? 0x7BEF :
        wifiConnected ? 0x07E0 : 0xFDE0;

    tft.setTextColor(connectionColor, 0x0000);

    const char* connectionText =
        !wifiStatus ? "Wi-Fi Disabled" :
        wifiConnected ? "Connected" : "Connecting...";

    tft.drawString(connectionText, centerX, 192, 1);

    tft.setTextColor(0x7BEF, 0x0000);
    tft.drawString("[Press: Next | Hold: Select]", centerX, 211, 1);
}
