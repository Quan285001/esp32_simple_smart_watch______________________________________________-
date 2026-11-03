#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>

extern TFT_eSPI tft;

/**
 * @brief Renders device uptime, memory, Wi-Fi, IP address, and CPU information.

 */
void drawSystemInfoScreen() {
    constexpr uint16_t BG = 0x0000;
    constexpr uint16_t WHITE = 0xFFFF;
    constexpr uint16_t MUTED = 0x9CF3;
    constexpr uint16_t GREEN = 0x07E0;
    constexpr uint16_t RED = 0xF800;
    constexpr uint16_t AMBER = 0xFDE0;

    tft.fillScreen(BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(WHITE, BG);
    tft.drawString("SYSTEM INFO", 120, 20, 2);
    tft.drawFastHLine(40, 34, 160, 0x3186);

    const uint32_t uptime = millis() / 1000UL;
    char uptimeText[20];
    snprintf(
        uptimeText, sizeof(uptimeText), "%02lu:%02lu:%02lu",
        static_cast<unsigned long>(uptime / 3600UL),
        static_cast<unsigned long>((uptime / 60UL) % 60UL),
        static_cast<unsigned long>(uptime % 60UL)
    );

    const bool connected = WiFi.status() == WL_CONNECTED;
    const String ssid = connected ? WiFi.SSID() : String("Disconnected");
    const String ip = connected ? WiFi.localIP().toString() : String("No IP");

    tft.setTextDatum(ML_DATUM);
    const int labelX = 24;
    const int valueX = 91;
    const int firstY = 54;
    const int stepY = 27;

    tft.setTextColor(MUTED, BG);
    tft.drawString("Uptime", labelX, firstY, 1);
    tft.setTextColor(WHITE, BG);
    tft.drawString(uptimeText, valueX, firstY, 2);

    char heapText[20];
    snprintf(heapText, sizeof(heapText), "%lu KB",
             static_cast<unsigned long>(ESP.getFreeHeap() / 1024UL));
    tft.setTextColor(MUTED, BG);
    tft.drawString("Free RAM", labelX, firstY + stepY, 1);
    tft.setTextColor(AMBER, BG);
    tft.drawString(heapText, valueX, firstY + stepY, 2);

    tft.setTextColor(MUTED, BG);
    tft.drawString("Wi-Fi", labelX, firstY + stepY * 2, 1);
    tft.setTextColor(connected ? GREEN : RED, BG);
    tft.drawString(ssid.substring(0, 17), valueX, firstY + stepY * 2, 1);

    tft.setTextColor(MUTED, BG);
    tft.drawString("IP", labelX, firstY + stepY * 3, 1);
    tft.setTextColor(connected ? GREEN : MUTED, BG);
    tft.drawString(ip, valueX, firstY + stepY * 3, 1);

    char cpuText[20];
    snprintf(cpuText, sizeof(cpuText), "%u MHz", ESP.getCpuFreqMHz());
    tft.setTextColor(MUTED, BG);
    tft.drawString("CPU", labelX, firstY + stepY * 4, 1);
    tft.setTextColor(WHITE, BG);
    tft.drawString(cpuText, valueX, firstY + stepY * 4, 1);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x7BEF, BG);
    tft.drawString("Short press: Back to menu", 120, 211, 1);
}
