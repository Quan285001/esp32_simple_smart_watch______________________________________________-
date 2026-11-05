#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_sleep.h>
#include <esp_err.h>

static TFT_eSPI tft;

void setup() {
    Serial.begin(115200);
    delay(300);

    // Red means setup completed and the chip is about to enter Light Sleep.
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_RED);
    Serial.println("[SLEEP-TEST] Display RED; isolated timer test starts in 2 seconds.");
    Serial.flush();
    delay(2000);

    // No Wi-Fi, BLE, GPIO wake source, tasks, NVS, or application code.
    const esp_err_t timerResult = esp_sleep_enable_timer_wakeup(8ULL * 1000000ULL);
    Serial.printf("[SLEEP-TEST] timer setup result=%d\n", static_cast<int>(timerResult));
    Serial.flush();

    if (timerResult != ESP_OK) {
        tft.fillScreen(TFT_YELLOW);
        Serial.println("[SLEEP-TEST] TIMER CONFIG FAILED.");
        return;
    }

    Serial.println("[SLEEP-TEST] Entering Light Sleep for 8 seconds.");
    Serial.flush();
    const esp_err_t sleepResult = esp_light_sleep_start();

    // Green means esp_light_sleep_start() returned to normal execution.
    Serial.printf("[SLEEP-TEST] Light Sleep returned; result=%d wake_cause=%d\n",
                  static_cast<int>(sleepResult),
                  static_cast<int>(esp_sleep_get_wakeup_cause()));
    Serial.flush();
    tft.fillScreen(TFT_GREEN);
}

void loop() {
    delay(1000);
}
