
#include <Arduino.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

namespace {
constexpr uint16_t BG = 0x0000;
constexpr uint16_t PANEL = 0x2104;
constexpr uint16_t BORDER = 0xFDE0;
constexpr uint16_t MUTED = 0xCE79;
constexpr uint16_t GREEN = 0x07E0;
constexpr uint16_t RED = 0xF800;
constexpr uint16_t AMBER = 0xFDE0;

const char *const MENU_LABELS[4] = {
    "Clock", "Weather", "Wi-Fi Mode", "Return"
};

constexpr int MENU_START_Y = 62;
constexpr int MENU_ITEM_STEP = 32;
constexpr int BOX_X = 22;
constexpr int BOX_W = 196;
constexpr int BOX_H = 24;

// Thời gian chuyển cảnh
constexpr uint32_t ANIMATION_DURATION = 180;

int itemCenterY(int index) {
    return MENU_START_Y + index * MENU_ITEM_STEP;
}

int boxTop(int index) {
    return itemCenterY(index) - BOX_H / 2;
}

void drawMenuStatic(bool wifiStatus, bool wifiConnected) {
    tft.fillScreen(BG);

    tft.setTextColor(TFT_WHITE, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("MENU", 120, 20, 2);
    tft.drawFastHLine(60, 34, 120, 0x3186);

    for (int i = 0; i < 4; ++i) {
        int y = itemCenterY(i);

        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(MUTED, BG);
        tft.drawString(MENU_LABELS[i], 32, y, 2);

        if (i == 2) {
            tft.setTextDatum(MR_DATUM);
            tft.setTextColor(wifiStatus ? GREEN : RED, BG);
            tft.drawString(wifiStatus ? "ON" : "OFF", 205, y, 2);
        }
    }

    tft.setTextDatum(MC_DATUM);

    uint16_t connectionColor = !wifiStatus ? 0x7BEF :
        (wifiConnected ? GREEN : AMBER);

    const char *connectionText = !wifiStatus ? "Wi-Fi Disabled" :
        (wifiConnected ? "Connected" : "Connecting...");

    tft.setTextColor(connectionColor, BG);
    tft.drawString(connectionText, 120, 192, 1);

    tft.setTextColor(0x7BEF, BG);
    tft.drawString("[Press: Next | Hold: Select]", 120, 211, 1);
}

void drawSelectionBox(int topY) {
    tft.fillRoundRect(BOX_X, topY, BOX_W, BOX_H, 5, PANEL);
    tft.drawRoundRect(BOX_X, topY, BOX_W, BOX_H, 5, BORDER);
}

void drawSelectedLabel(int index, bool wifiStatus) {
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(BORDER, PANEL);
    tft.drawString(MENU_LABELS[index], 32, itemCenterY(index), 2);

    if (index == 2) {
        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(wifiStatus ? GREEN : RED, PANEL);
        tft.drawString(wifiStatus ? "ON" : "OFF", 205,
                       itemCenterY(index), 2);
    }
}

// Vẽ lại vùng box cũ mà không vẽ lại toàn màn hình
void eraseSelectionBox(int topY) {
    tft.fillRect(BOX_X, topY, BOX_W, BOX_H, BG);
}

// Ease-out cubic: đầu nhanh, cuối chậm
float easeOutCubic(float t) {
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

void redrawUnderlyingItem(int topY, bool wifiStatus) {
    for (int i = 0; i < 4; ++i) {
        int center = itemCenterY(i);

        if (center >= topY - 2 && center <= topY + BOX_H + 2) {
            tft.setTextDatum(ML_DATUM);
            tft.setTextColor(MUTED, BG);
            tft.drawString(MENU_LABELS[i], 32, center, 2);

            if (i == 2) {
                tft.setTextDatum(MR_DATUM);
                tft.setTextColor(wifiStatus ? GREEN : RED, BG);
                tft.drawString(wifiStatus ? "ON" : "OFF", 205, center, 2);
            }
        }
    }
}
}

void drawMenuScreen(int selectedIndex, bool wifiStatus, bool wifiConnected) {
    selectedIndex = constrain(selectedIndex, 0, 3);

    static bool initialized = false;
    static int currentY = 0;
    static int targetY = 0;
    static int previousIndex = -1;

    static uint32_t animationStart = 0;
    static int animationFromY = 0;
    static bool animating = false;

    static bool previousWifiStatus = false;
    static bool previousWifiConnected = false;

    bool wifiChanged =
        previousWifiStatus != wifiStatus ||
        previousWifiConnected != wifiConnected;

    if (!initialized || wifiChanged) {
        drawMenuStatic(wifiStatus, wifiConnected);

        currentY = boxTop(selectedIndex);
        targetY = currentY;
        previousIndex = selectedIndex;
        animating = false;

        drawSelectionBox(currentY);

        initialized = true;
        previousWifiStatus = wifiStatus;
        previousWifiConnected = wifiConnected;
        return;
    }

    int newTargetY = boxTop(selectedIndex);

    // Bắt đầu chuyển động khi người dùng chọn mục khác
    if (selectedIndex != previousIndex && newTargetY != targetY) {
        animationFromY = currentY;
        targetY = newTargetY;
        animationStart = millis();
        animating = true;
        previousIndex = selectedIndex;
    }

    if (!animating) {
        return;
    }

    uint32_t elapsed = millis() - animationStart;
    float t = (float)elapsed / ANIMATION_DURATION;

    if (t >= 1.0f) {
        t = 1.0f;
        animating = false;
    }

    float eased = easeOutCubic(t);

    int nextY = animationFromY +
        (int)((targetY - animationFromY) * eased);

    if (nextY != currentY || !animating) {
    tft.startWrite();

    eraseSelectionBox(currentY);
    redrawUnderlyingItem(currentY, wifiStatus);

    currentY = nextY;

    drawSelectionBox(currentY);
    drawSelectedLabel(selectedIndex, wifiStatus);

    tft.endWrite();
    }
}
