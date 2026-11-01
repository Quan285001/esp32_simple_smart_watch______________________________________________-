
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

constexpr uint32_t ANIMATION_DURATION = 180;

// Animation state
bool initialized = false;
bool animating = false;

int currentY = 0;
int targetY = 0;
int animationFromY = 0;
int previousIndex = -1;

uint32_t animationStart = 0;

bool previousWifiStatus = false;
bool previousWifiConnected = false;

int itemCenterY(int index) {
    return MENU_START_Y + index * MENU_ITEM_STEP;
}

int boxTop(int index) {
    return itemCenterY(index) - BOX_H / 2;
}

float easeOutCubic(float t) {
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

void drawMenuStatic(bool wifiStatus, bool wifiConnected) {
    tft.fillScreen(BG);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, BG);
    tft.drawString("MENU", 120, 20, 2);

    tft.drawFastHLine(60, 34, 120, 0x3186);

    for (int i = 0; i < 4; i++) {
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

    uint16_t connectionColor =
        !wifiStatus ? 0x7BEF :
        (wifiConnected ? GREEN : AMBER);

    const char *connectionText =
        !wifiStatus ? "Wi-Fi Disabled" :
        (wifiConnected ? "Connected" : "Connecting...");

    tft.setTextColor(connectionColor, BG);
    tft.drawString(connectionText, 120, 192, 1);

    tft.setTextColor(0x7BEF, BG);
    tft.drawString("[Press: Next | Hold: Select]", 120, 211, 1);
}

void drawMenuItem(int index, bool wifiStatus) {
    int y = itemCenterY(index);

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(MUTED, BG);
    tft.drawString(MENU_LABELS[index], 32, y, 2);

    if (index == 2) {
        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(wifiStatus ? GREEN : RED, BG);
        tft.drawString(
            wifiStatus ? "ON" : "OFF",
            205, y, 2
        );
    }
}

void redrawAllMenuItems(bool wifiStatus) {
    for (int i = 0; i < 4; i++) {
        drawMenuItem(i, wifiStatus);
    }
}

void drawSelectionBox(int topY) {
    tft.fillRoundRect(
        BOX_X, topY, BOX_W, BOX_H, 5, PANEL
    );

    tft.drawRoundRect(
        BOX_X, topY, BOX_W, BOX_H, 5, BORDER
    );
}

void drawSelectedLabel(int index, bool wifiStatus) {
    int y = itemCenterY(index);

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(BORDER, PANEL);
    tft.drawString(MENU_LABELS[index], 32, y, 2);

    if (index == 2) {
        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(wifiStatus ? GREEN : RED, PANEL);
        tft.drawString(
            wifiStatus ? "ON" : "OFF",
            205, y, 2
        );
    }
}

} // namespace

// Reset trạng thái khi mở menu
void resetMenuScreen() {
    initialized = false;
    animating = false;
    previousIndex = -1;
}

// Main menu renderer
void drawMenuScreen(
    int selectedIndex,
    bool wifiStatus,
    bool wifiConnected
) {
    selectedIndex = constrain(selectedIndex, 0, 3);

    bool wifiChanged =
        previousWifiStatus != wifiStatus ||
        previousWifiConnected != wifiConnected;

    // Khởi tạo hoặc vẽ lại khi Wi-Fi thay đổi
    if (!initialized || wifiChanged) {
        tft.startWrite();

        drawMenuStatic(wifiStatus, wifiConnected);

        currentY = boxTop(selectedIndex);
        targetY = currentY;

        drawSelectionBox(currentY);
        drawSelectedLabel(selectedIndex, wifiStatus);

        tft.endWrite();

        previousIndex = selectedIndex;
        previousWifiStatus = wifiStatus;
        previousWifiConnected = wifiConnected;

        animating = false;
        initialized = true;
        return;
    }

    // Bắt đầu animation khi đổi mục
    if (selectedIndex != previousIndex) {
        animationFromY = currentY;
        targetY = boxTop(selectedIndex);
        animationStart = millis();

        animating = true;
        previousIndex = selectedIndex;
    }

    // Không có animation
    if (!animating) {
        return;
    }

    // Tính tiến trình animation
    uint32_t elapsed = millis() - animationStart;

    float t = (float)elapsed / ANIMATION_DURATION;

    bool animationFinished = (t >= 1.0f);

    if (animationFinished) {
        t = 1.0f;
    }

    float eased = easeOutCubic(t);

    int nextY = animationFromY +
        (int)((targetY - animationFromY) * eased);

    if (animationFinished) {
        nextY = targetY;
    }

    // Chỉ vẽ khi vị trí thực sự thay đổi
    if (nextY != currentY || animationFinished) {
        tft.startWrite();

        // Xóa toàn bộ vùng hộp cũ
        tft.fillRect(
            BOX_X, currentY,
            BOX_W, BOX_H,
            BG
        );

        // Phục hồi tất cả chữ có thể bị hộp che
        redrawAllMenuItems(wifiStatus);

        // Vẽ hộp tại vị trí mới
        currentY = nextY;
        drawSelectionBox(currentY);

        // Chỉ hiện chữ nổi bật khi animation kết thúc
        if (animationFinished) {
            drawSelectedLabel(selectedIndex, wifiStatus);
            animating = false;
        }

        tft.endWrite();
    }
}
