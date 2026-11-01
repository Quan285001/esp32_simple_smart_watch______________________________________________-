#include <Arduino.h>
#include <TFT_eSPI.h>
#include "icons.h"
#include "config.h"
#include <time.h>

extern TFT_eSPI tft;

// Chỉ số đang xem trong mảng hourly[24].
// weather_service.cpp sắp xếp hourly[0] bắt đầu từ giờ hiện tại.
static int selectedForecastIndex = 0;
static bool forecastSelectionInitialized = false;

static void drawTemperatureWithDegree(
    int temp, int x, int y, int font, uint16_t color
) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", temp);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(color, 0x1082);

    // Vẽ phần số nhiệt độ
    tft.drawString(buf, x, y, font);

    // Tính chiều rộng theo đúng font đang sử dụng
    int textW = tft.textWidth(buf, font);

    // Vị trí ký hiệu độ
    int degreeX = x + textW + 3;
    int degreeY = y + ((font >= 4) ? 5 : 3);
    int radius = (font >= 4) ? 3 : 2;

    // Vẽ một vòng tròn duy nhất
    tft.drawCircle( degreeX + radius ,degreeY ,radius ,color );

    // Vẽ chữ C độc lập theo kích thước font
    int cX = degreeX + radius * 2 + 4;
    tft.drawString("C", cX, y, font);
}

static void drawLargeWeatherIcon(int x, int y, int code, bool isNight) {
    if (code == 0) {
        if (isNight) {
            tft.fillCircle(x, y, 15, 0xFCE0);
            tft.fillCircle(x + 5, y - 2, 12, 0x1082);
            tft.fillCircle(x, y, 12, 0xFCE0);
        } else {
            tft.fillCircle(x, y, 14, 0xFDE0);
            tft.drawFastVLine(x, y - 22, 9, 0xFDE0);
            tft.drawFastVLine(x, y + 14, 9, 0xFDE0);
            tft.drawFastHLine(x - 22, y, 9, 0xFDE0);
            tft.drawFastHLine(x + 14, y, 9, 0xFDE0);
            for (int i = -2; i <= 2; ++i) {
                tft.drawPixel(x - 12 + i, y - 12, 0xFDE0);
                tft.drawPixel(x + 12 + i, y - 12, 0xFDE0);
                tft.drawPixel(x - 12 + i, y + 12, 0xFDE0);
                tft.drawPixel(x + 12 + i, y + 12, 0xFDE0);
            }
        }
    } else if (code == 1 || code == 2) {
        if (isNight) {
            tft.fillCircle(x - 8, y - 8, 12, 0xFCE0);
            tft.fillCircle(x - 4, y - 9, 10, 0x1082);
        } else {
            tft.fillCircle(x - 8, y - 8, 11, 0xFDE0);
        }
        tft.fillCircle(x - 6, y + 4, 11, TFT_WHITE);
        tft.fillCircle(x + 10, y + 2, 14, TFT_WHITE);
        tft.fillCircle(x + 2, y - 6, 13, TFT_WHITE);
        tft.fillRect(x - 15, y + 4, 30, 14, TFT_WHITE);
    } else if (code == 3 || (code >= 45 && code <= 48)) {
        const uint16_t c = (code == 3) ? 0xCE79 : 0x7BEF;
        tft.fillCircle(x - 12, y + 4, 11, c);
        tft.fillCircle(x + 10, y, 15, c);
        tft.fillCircle(x, y - 8, 13, c);
        tft.fillRect(x - 16, y, 32, 16, c);
    } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        tft.fillCircle(x - 12, y + 2, 11, TFT_WHITE);
        tft.fillCircle(x + 10, y - 2, 15, TFT_WHITE);
        tft.fillCircle(x, y - 10, 13, TFT_WHITE);
        tft.fillRect(x - 16, y - 2, 32, 16, TFT_WHITE);
        for (int dx = -10; dx <= 10; dx += 10) {
            tft.drawFastVLine(x + dx, y + 16, 8, 0x001F);
            tft.drawFastVLine(x + dx + 2, y + 16, 8, 0x001F);
        }
    } else if (code >= 95) {
        tft.fillCircle(x - 12, y, 11, 0xCE79);
        tft.fillCircle(x + 10, y - 4, 15, 0xCE79);
        tft.fillCircle(x, y - 12, 13, 0xCE79);
        tft.fillRect(x - 16, y - 4, 32, 16, 0xCE79);
        tft.drawLine(x, y + 6, x - 8, y + 16, 0xFDE0);
        tft.drawLine(x - 8, y + 16, x + 4, y + 16, 0xFDE0);
        tft.drawLine(x + 4, y + 16, x - 4, y + 26, 0xFDE0);
    } else {
        tft.fillCircle(x - 8, y - 8, 11, 0xFDE0);
        tft.fillCircle(x - 7, y + 4, 11, TFT_WHITE);
        tft.fillCircle(x + 10, y + 2, 14, TFT_WHITE);
        tft.fillRect(x - 15, y + 4, 30, 14, TFT_WHITE);
    }
}

static void drawDynamicMiniIcon(int x, int y, int code, bool isNight) {
    if (code == 0) {
        if (isNight) drawMiniCrescentMoon(tft, x, y, 0xFCE0);
        else drawMiniSun(tft, x, y, 0xFDE0);
    } else if (code == 1 || code == 2) {
        if (isNight) drawMiniMoonCloud(tft, x, y, TFT_WHITE);
        else drawMiniSunCloud(tft, x, y, TFT_WHITE);
    } else if (code == 3 || (code >= 45 && code <= 48)) {
        drawMiniCloud(tft, x, y, code == 3 ? 0xCE79 : 0x7BEF);
    } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        drawMiniRain(tft, x, y, TFT_WHITE);
    } else if (code >= 95) {
        drawMiniThunder(tft, x, y, 0xCE79);
    } else {
        drawMiniSunCloud(tft, x, y, TFT_WHITE);
    }
}

void drawWeatherFaceData(const WeatherData& w) {
    tft.fillScreen(0x0000);

    const int centerX = 120;
    const int selected = selectedForecastIndex % 24;
    const HourlyForecast& selectedHour = w.hourly[selected];

    const int hour = selectedHour.hour;
    const bool isNight = (hour < 6 || hour >= 18);

    // Khung thời tiết chính.
    tft.fillRoundRect(15, 30, 210, 88, 16, 0x1082);
    tft.drawRoundRect(15, 30, 210, 88, 16, 0x3186);

    // Nhiệt độ và icon lớn đi theo giờ đang được chọn.
    drawLargeWeatherIcon(52, 74, selectedHour.weatherCode, isNight);
    drawTemperatureWithDegree(selected == 0 ? w.currentTemp : selectedHour.temp, 90, 60, 4, TFT_WHITE);
    drawTemperatureWithDegree(w.maxTemp, 175, 63, 2, TFT_WHITE);
    drawTemperatureWithDegree(w.minTemp, 175, 82, 2, 0x7BEF);

    // Nhãn giờ đang xem.
    char selectedLabel[16];
    snprintf(selectedLabel, sizeof(selectedLabel), "%02d:00", hour);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(0xFDE0, 0x1082);
    tft.drawString(selectedLabel, 91, 98, 1);

    // Bốn giờ dự báo bắt đầu từ giờ đang chọn.
    const int colX[4] = {47, 94, 141, 188};
    const int boxWidth = 42;
    const int boxHeight = 78;
    const int boxY = 124;

    for (int p = 0; p < 4; ++p) {
        const int dataIdx = (selected + p) % 24;
        const HourlyForecast& f = w.hourly[dataIdx];
        const int px = colX[p];
        const int boxX = px - boxWidth / 2;

        const uint16_t bgColor = (p == 0) ? 0x2184 : 0x1082;
        const uint16_t borderColor = (p == 0) ? 0xFDE0 : 0x3186;

        tft.fillRoundRect(boxX, boxY, boxWidth, boxHeight, 10, bgColor);
        tft.drawRoundRect(boxX, boxY, boxWidth, boxHeight, 10, borderColor);

        // Nhiệt độ.
        char tempBuf[8];
        snprintf(tempBuf, sizeof(tempBuf), "%d", f.temp);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, bgColor);
        tft.drawString(tempBuf, px, boxY + 17, 1);
        const int degreeX = px + tft.textWidth(tempBuf) / 2 + 2;
        tft.drawCircle(degreeX, boxY + 14, 2, TFT_WHITE);

        // Icon theo thời tiết và ngày/đêm của từng giờ.
        const bool hourlyNight = (f.hour < 6 || f.hour >= 18);
        drawDynamicMiniIcon(px, boxY + 42, f.weatherCode, hourlyNight);

        // Nhãn giờ 24h để tránh nhầm AM/PM và dễ nhận biết qua nửa đêm.
        char timeLabel[6];
        snprintf(timeLabel, sizeof(timeLabel), "%02d:00", f.hour);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor((p == 0) ? TFT_WHITE : 0x7BEF, bgColor);
        tft.drawString(timeLabel, px, boxY + 66, 1);
    }

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x7BEF, TFT_BLACK);
    tft.drawString("[Press: Next hour]", centerX, 216, 1);
}

// Được gọi khi nhấn ngắn ở màn Weather.
void advanceWeatherForecast(const WeatherData& w) {
    if (!forecastSelectionInitialized) {
        selectedForecastIndex = 0;
        forecastSelectionInitialized = true;
    }

    selectedForecastIndex = (selectedForecastIndex + 1) % 24;

    drawWeatherFaceData(w);
}
