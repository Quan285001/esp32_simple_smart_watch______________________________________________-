
#include <Arduino.h>
#include <TFT_eSPI.h>

#include "../app/app_state.h"

extern TFT_eSPI tft;
extern bool isBLEConnected();
extern bool isBLEEnabled();

namespace {

constexpr uint16_t BG       = 0x0000;
constexpr uint16_t PANEL    = 0x2104;
constexpr uint16_t BORDER   = 0xFDE0;
constexpr uint16_t MUTED    = 0xCE79;
constexpr uint16_t GREEN    = 0x07E0;
constexpr uint16_t RED      = 0xF800;
constexpr uint16_t AMBER    = 0xFDE0;
constexpr uint16_t WHITE    = 0xFFFF;

constexpr int MENU_START_Y = 52;
constexpr int MENU_ITEM_STEP = 24;

constexpr int BOX_X = 22;
constexpr int BOX_W = 196;
constexpr int BOX_H = 23;

constexpr uint32_t ANIMATION_DURATION = 180;

// Menu labels
const char *const MAIN_LABELS[] = {
    "Clock",
    "Weather",
    "Connectivity",
    "System"
};

const char *const CONNECTIVITY_LABELS[] = {
    "Bluetooth",
    "Wi-Fi",
    "Back"
};

const char *const SYSTEM_LABELS[] = {
    "Sync Time",
    "Sync Weather",
    "Device Info",
    "Light Sleep",
    "Restart",
    "Back"
};

// Animation state
bool initialized = false;
bool animating = false;

int currentY = 0;
int targetY = 0;
int animationFromY = 0;

int previousIndex = -1;
MenuLevel previousLevel = MENU_MAIN;

uint32_t animationStart = 0;

bool previousWifiStatus = false;
bool previousWifiConnected = false;
bool previousBLEEnabled = false;

// =====================================================
// MENU DATA
// =====================================================

const char *getMenuTitle(MenuLevel level) {

    switch (level) {

        case MENU_MAIN:
            return "MAIN MENU";

        case MENU_CONNECTIVITY:
            return "CONNECTIVITY";

        case MENU_SYSTEM:
            return "SYSTEM";

        default:
            return "MENU";
    }
}

const char *getMenuLabel(MenuLevel level, int index) {

    switch (level) {

        case MENU_MAIN:
            if (index < MAIN_MENU_COUNT)
                return MAIN_LABELS[index];
            break;

        case MENU_CONNECTIVITY:
            if (index < CONNECTIVITY_MENU_COUNT)
                return CONNECTIVITY_LABELS[index];
            break;

        case MENU_SYSTEM:
            if (index < SYSTEM_MENU_COUNT)
                return SYSTEM_LABELS[index];
            break;
    }

    return "";
}

int getMenuCount(MenuLevel level) {

    switch (level) {

        case MENU_MAIN:
            return MAIN_MENU_COUNT;

        case MENU_CONNECTIVITY:
            return CONNECTIVITY_MENU_COUNT;

        case MENU_SYSTEM:
            return SYSTEM_MENU_COUNT;

        default:
            return MAIN_MENU_COUNT;
    }
}

// =====================================================
// LAYOUT
// =====================================================

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

// =====================================================
// STATUS
// =====================================================

void drawItemStatus(
    MenuLevel level,
    int index,
    bool wifiStatus,
    bool wifiConnected,
    uint16_t background
) {

    tft.setTextDatum(MR_DATUM);

    if (level == MENU_CONNECTIVITY) {

        if (index == 0) {

            const bool enabled = isBLEEnabled();
            tft.setTextColor(enabled ? GREEN : RED, background);
            tft.drawString(enabled ? "ON" : "OFF", 205, itemCenterY(index), 1);
        }

        else if (index == 1) {

            tft.setTextColor(
                wifiStatus ? GREEN : RED,
                background
            );

            tft.drawString(
                !wifiStatus ? "OFF" :
                (wifiConnected ? "ON" : "WAIT"),
                205,
                itemCenterY(index),
                1
            );
        }
    }
}

// =====================================================
// DRAW MENU ITEM
// =====================================================

void drawMenuItem(
    MenuLevel level,
    int index,
    bool wifiStatus,
    bool wifiConnected
) {

    int y = itemCenterY(index);

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(MUTED, BG);

    tft.drawString(
        getMenuLabel(level, index),
        32,
        y,
        2
    );

    drawItemStatus(
        level,
        index,
        wifiStatus,
        wifiConnected,
        BG
    );
}

void drawSelectedLabel(
    MenuLevel level,
    int index,
    bool wifiStatus,
    bool wifiConnected
) {

    int y = itemCenterY(index);

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(BORDER, PANEL);

    tft.drawString(
        getMenuLabel(level, index),
        32,
        y,
        2
    );

    drawItemStatus(
        level,
        index,
        wifiStatus,
        wifiConnected,
        PANEL
    );
}

void redrawAllMenuItems(
    MenuLevel level,
    bool wifiStatus,
    bool wifiConnected
) {

    int count = getMenuCount(level);

    for (int i = 0; i < count; i++) {

        drawMenuItem(
            level,
            i,
            wifiStatus,
            wifiConnected
        );
    }
}

// =====================================================
// DRAW SELECTION BOX
// =====================================================

void drawSelectionBox(int topY) {

    tft.fillRoundRect(
        BOX_X,
        topY,
        BOX_W,
        BOX_H,
        5,
        PANEL
    );

    tft.drawRoundRect(
        BOX_X,
        topY,
        BOX_W,
        BOX_H,
        5,
        BORDER
    );
}

// =====================================================
// STATIC SCREEN
// =====================================================

void drawMenuStatic(
    MenuLevel level,
    bool wifiStatus,
    bool wifiConnected
) {

    tft.fillScreen(BG);

    // Header
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(WHITE, BG);

    tft.drawString(
        getMenuTitle(level),
        120,
        20,
        2
    );

    tft.drawFastHLine(
        60,
        35,
        120,
        0x3186
    );

    // Menu items
    redrawAllMenuItems(
        level,
        wifiStatus,
        wifiConnected
    );

    // Footer status
    tft.setTextDatum(MC_DATUM);

    if (level == MENU_CONNECTIVITY) {

        uint16_t color =
            !wifiStatus ? MUTED :
            (wifiConnected ? GREEN : AMBER);

        const char *status =
            !wifiStatus ? "Wi-Fi Disabled" :
            (wifiConnected ? "Wi-Fi Connected" : "Wi-Fi Connecting");

        tft.setTextColor(color, BG);
        tft.drawString(status, 120, 190, 1);
    }

    else if (level == MENU_SYSTEM) {

        tft.setTextColor(MUTED, BG);
        tft.drawString("System Control", 120, 190, 1);
    }

    else {

        uint16_t color =
            !wifiStatus ? MUTED :
            (wifiConnected ? GREEN : AMBER);

        const char *status =
            !wifiStatus ? "Wi-Fi Disabled" :
            (wifiConnected ? "Connected" : "Connecting...");

        tft.setTextColor(color, BG);
        tft.drawString(status, 120, 190, 1);
    }

    // Button guide
    tft.setTextColor(0x7BEF, BG);

    tft.drawString(
        "[Press: Next | Hold: Select]",
        120,
        214,
        1
    );
}

} // namespace

// =====================================================
// RESET MENU
// =====================================================

void resetMenuScreen() {

    initialized = false;
    animating = false;

    previousIndex = -1;
    previousLevel = MENU_MAIN;
}

// =====================================================
// MAIN MENU RENDERER
// =====================================================

void drawMenuScreen(
    int selectedIndex,
    MenuLevel level,
    bool wifiStatus,
    bool wifiConnected
) {

    int count = getMenuCount(level);

    if (count <= 0) {
        return;
    }

    selectedIndex = constrain(
        selectedIndex,
        0,
        count - 1
    );

    const bool bleEnabled = isBLEEnabled();
    bool wifiChanged =
        previousWifiStatus != wifiStatus ||
        previousWifiConnected != wifiConnected;
    bool bleChanged = previousBLEEnabled != bleEnabled;

    bool menuChanged =
        previousLevel != level;

    // -------------------------------------------------
    // INITIALIZE / REDRAW
    // -------------------------------------------------

    if (!initialized || wifiChanged || bleChanged || menuChanged) {

        tft.startWrite();

        drawMenuStatic(
            level,
            wifiStatus,
            wifiConnected
        );

        currentY = boxTop(selectedIndex);
        targetY = currentY;

        drawSelectionBox(currentY);

        drawSelectedLabel(
            level,
            selectedIndex,
            wifiStatus,
            wifiConnected
        );

        tft.endWrite();

        previousIndex = selectedIndex;
        previousLevel = level;

        previousWifiStatus = wifiStatus;
        previousWifiConnected = wifiConnected;
        previousBLEEnabled = bleEnabled;

        animating = false;
        initialized = true;

        return;
    }

    // -------------------------------------------------
    // START ANIMATION
    // -------------------------------------------------

    if (selectedIndex != previousIndex) {

        animationFromY = currentY;

        targetY = boxTop(selectedIndex);

        animationStart = millis();

        animating = true;

        previousIndex = selectedIndex;
    }

    if (!animating) {
        return;
    }

    // -------------------------------------------------
    // ANIMATION PROGRESS
    // -------------------------------------------------

    uint32_t elapsed =
        millis() - animationStart;

    float t =
        (float)elapsed / ANIMATION_DURATION;

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

    // -------------------------------------------------
    // DRAW FRAME
    // -------------------------------------------------

    if (nextY != currentY || animationFinished) {

        tft.startWrite();

        // Clear previous selection area
        tft.fillRect(
            BOX_X,
            currentY,
            BOX_W,
            BOX_H,
            BG
        );

        // Restore menu labels
        redrawAllMenuItems(
            level,
            wifiStatus,
            wifiConnected
        );

        // Draw new selection
        currentY = nextY;

        drawSelectionBox(currentY);

        if (animationFinished) {

            drawSelectedLabel(
                level,
                selectedIndex,
                wifiStatus,
                wifiConnected
            );

            animating = false;
        }

        tft.endWrite();
    }
}
