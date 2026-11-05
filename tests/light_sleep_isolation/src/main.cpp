#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_sleep.h>
#include <esp_err.h>
#include <driver/gpio.h>

static constexpr gpio_num_t WAKE_BUTTON_PIN = GPIO_NUM_9;
static constexpr uint64_t TIMER_WAKE_US = 8ULL * 1000000ULL;

static TFT_eSPI tft;

void setup() {
    Serial.begin(115200);
    delay(300);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_RED);
    Serial.println("[SLEEP-TEST-2] Display RED; GPIO9 + 8-second timer test.");
    Serial.flush();

    // BOOT is active-low. Wait for release so a held button does not
    // immediately satisfy the low-level wake condition on entry.
    pinMode(static_cast<int>(WAKE_BUTTON_PIN), INPUT_PULLUP);
    while (digitalRead(static_cast<int>(WAKE_BUTTON_PIN)) == LOW) {
        delay(20);
    }
    delay(300);

    const esp_err_t gpioPinResult =
        gpio_wakeup_enable(WAKE_BUTTON_PIN, GPIO_INTR_LOW_LEVEL);
    const esp_err_t gpioSourceResult = esp_sleep_enable_gpio_wakeup();
    const esp_err_t timerResult = esp_sleep_enable_timer_wakeup(TIMER_WAKE_US);

    Serial.printf("[SLEEP-TEST-2] gpio pin setup=%d, gpio source setup=%d, timer setup=%d\n",
                  static_cast<int>(gpioPinResult),
                  static_cast<int>(gpioSourceResult),
                  static_cast<int>(timerResult));
    Serial.flush();

    if (gpioPinResult != ESP_OK || gpioSourceResult != ESP_OK || timerResult != ESP_OK) {
        tft.fillScreen(TFT_YELLOW);
        Serial.println("[SLEEP-TEST-2] WAKE SOURCE CONFIG FAILED.");
        Serial.flush();
        return;
    }

    Serial.println("[SLEEP-TEST-2] Entering Light Sleep: press BOOT to wake, otherwise timer wakes after 8 seconds.");
    Serial.flush();
    const esp_err_t sleepResult = esp_light_sleep_start();
    const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();

    // Green is the primary wake indicator. USB Serial/JTAG may reconnect
    // after Light Sleep, so allow it time before sending the result line.
    tft.fillScreen(TFT_GREEN);
    delay(1200);
    Serial.printf("[SLEEP-TEST-2] Light Sleep returned=%d, wake_cause=%d, GPIO9 level=%d\n",
                  static_cast<int>(sleepResult),
                  static_cast<int>(wakeCause),
                  digitalRead(static_cast<int>(WAKE_BUTTON_PIN)));
    Serial.flush();
}

void loop() {
    delay(1000);
}
