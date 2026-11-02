#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <sys/time.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "config.h"
#include "app/app_state.h"
#include "app/app_controller.h"
#include "input/button_manager.h"
#include "services/time_service.h"
#include "services/weather_service.h"

TFT_eSPI tft = TFT_eSPI();
ScreenState currentScreen = SCREEN_CLOCK;
ScreenState screenBeforeMenu = SCREEN_CLOCK;
uint8_t menuIndex = 0;
MenuLevel currentMenuLevel = MENU_MAIN;
uint8_t forecastIndex = 0;
bool weatherDataValid = false;
bool wifiEnabled = false;

extern void drawAnalogClockFace(struct tm timeinfo, int millisValue, int heartRate, int steps);
extern void showBootTerminal();
extern void drawMenuScreen(int selectedIndex, MenuLevel level, bool wifiStatus, bool wifiConnected);
extern void drawWeatherFaceData(const WeatherData& w);
extern void updateWeatherFaceAnimation(const WeatherData& w);

static constexpr char BLE_NAME[] = "ESP32_Watch";
static constexpr char BLE_SERVICE_UUID[] = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static constexpr char BLE_RX_UUID[] = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
static constexpr char BLE_TX_UUID[] = "beb5483e-36e1-4688-b7f5-ea07361b27a8";

struct BleCommand { char value[192]; };
static QueueHandle_t commandQueue = nullptr;
static BLECharacteristic* txCharacteristic = nullptr;
static bool bluetoothEnabled = true;
static volatile bool weatherFetchRunning = false;
static volatile bool weatherFetchFinished = false;
static uint32_t lastWeatherFetchMs = 0;
static uint32_t lastWeatherRetryMs = 0;
static uint32_t lastClockDrawMs = 0;
static uint32_t lastMenuFrameMs = 0;
static uint32_t lastWeatherFrameMs = 0;
static bool weatherFaceInitialized = false;
static bool previousWifiConnected = false;
static constexpr uint32_t WEATHER_RETRY_MS = 15000;

class WatchBleServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override { Serial.println("[BLE] Phone connected"); }
    void onDisconnect(BLEServer*) override {
        Serial.println("[BLE] Phone disconnected");
        if (bluetoothEnabled) BLEDevice::startAdvertising();
    }
};
class WatchBleRxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        std::string value = characteristic->getValue();
        if (!commandQueue || value.empty()) return;
        BleCommand cmd = {};
        size_t n = min(value.size(), sizeof(cmd.value)-1);
        memcpy(cmd.value, value.data(), n);
        xQueueSend(commandQueue, &cmd, 0);
    }
};

static void startBluetooth() {
    if (bluetoothEnabled && txCharacteristic) return;
    BLEDevice::init(BLE_NAME);
    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new WatchBleServerCallbacks());
    BLEService* service = server->createService(BLE_SERVICE_UUID);
    BLECharacteristic* rx = service->createCharacteristic(BLE_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(new WatchBleRxCallbacks());
    txCharacteristic = service->createCharacteristic(BLE_TX_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    txCharacteristic->addDescriptor(new BLE2902());
    txCharacteristic->setValue("ESP32 watch ready");
    service->start();
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->setScanResponse(true);
    BLEDevice::startAdvertising();
    bluetoothEnabled = true;
    Serial.println("[BLE] Advertising as ESP32_Watch");
}

void toggleBluetooth() {
    if (bluetoothEnabled) {
        BLEDevice::getAdvertising()->stop();
        bluetoothEnabled = false;
        Serial.println("[BLE] Advertising stopped");
    } else {
        bluetoothEnabled = true;
        if (txCharacteristic) BLEDevice::startAdvertising();
        else startBluetooth();
        Serial.println("[BLE] Advertising started");
    }
}
static void bleNotify(const String& message) {
    if (!bluetoothEnabled || !txCharacteristic) return;
    txCharacteristic->setValue(message.c_str());
    txCharacteristic->notify();
}
static bool connectSavedWiFi() {
    Preferences p;
    if (!p.begin("watch", true)) return false;
    String ssid = p.getString("ssid", "");
    String password = p.getString("password", "");
    p.end();
    if (ssid.isEmpty()) {
        Serial.println("[WIFI] No saved credentials; waiting for phone setup");
        return false;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), password.c_str());
    wifiEnabled = true;
    Serial.printf("[WIFI] Connecting to saved network: %s\n", ssid.c_str());
    return true;
}
void toggleWiFi() {
    if (wifiEnabled) {
        WiFi.disconnect(false, false);
        WiFi.mode(WIFI_OFF);
        wifiEnabled = false;
        previousWifiConnected = false;
        Serial.println("[WIFI] Disabled; cached weather retained");
    } else {
        // Enable the Wi-Fi radio even when no network credentials are saved.
        // This allows the user to scan for networks or provision credentials later.
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        wifiEnabled = true;

        if (!connectSavedWiFi()) {
            Serial.println("[WIFI] Enabled; no saved credentials. Ready for network scan/phone setup");
        }
    }
}
void scanWifiNetworks() {
    if (!wifiEnabled) {
        WiFi.mode(WIFI_STA);
        wifiEnabled = true;
    }
    int count = WiFi.scanNetworks(false, true);
    if (count < 0) {
        bleNotify("SCAN:ERROR");
        return;
    }
    bleNotify("SCAN:BEGIN");
    for (int i = 0; i < count; ++i) {
        String line = "AP:" + WiFi.SSID(i) + "," + String(WiFi.RSSI(i)) + "," + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "SECURE");
        bleNotify(line);
        delay(25);
    }
    bleNotify("SCAN:END");
    WiFi.scanDelete();
}
static void weatherFetchTask(void*) {
    fetchHourlyWeather();
    weatherFetchFinished = true;
    weatherFetchRunning = false;
    vTaskDelete(nullptr);
}
static bool scheduleWeatherFetch() {
    if (!wifiEnabled || WiFi.status() != WL_CONNECTED || weatherFetchRunning) return false;
    weatherFetchRunning = true;
    weatherFetchFinished = false;
    BaseType_t ok = xTaskCreate(weatherFetchTask, "weatherFetch", 8192, nullptr, 1, nullptr);
    if (ok != pdPASS) {
        weatherFetchRunning = false;
        return false;
    }
    lastWeatherFetchMs = millis();
    return true;
}
bool requestWeatherRefresh() { return scheduleWeatherFetch(); }

static void handleBleCommand(const char* raw) {
    String command(raw);
    Serial.printf("[BLE] Command: %s\n", command.c_str());
    if (command == "SYNC_WEATHER") {
        if (!scheduleWeatherFetch()) bleNotify("WEATHER:BUSY_OR_OFFLINE");
    } else if (command.startsWith("GPS:")) {
        String coords = command.substring(4);
        int comma = coords.indexOf(',');
        if (comma > 0) {
            float lat = coords.substring(0, comma).toFloat();
            float lon = coords.substring(comma + 1).toFloat();
            if (lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180) {
                setWeatherLocation(lat, lon);
                bleNotify("GPS:OK");
                if (WiFi.status() == WL_CONNECTED) scheduleWeatherFetch();
            } else bleNotify("GPS:INVALID");
        }
    } else if (command.startsWith("WIFI:")) {
        String credentials = command.substring(5);
        int comma = credentials.indexOf(',');
        if (comma <= 0) { bleNotify("WIFI:INVALID"); return; }
        String ssid = credentials.substring(0, comma);
        String password = credentials.substring(comma + 1);
        Preferences p;
        if (p.begin("watch", false)) {
            p.putString("ssid", ssid);
            p.putString("password", password);
            p.end();
        }
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.begin(ssid.c_str(), password.c_str());
        wifiEnabled = true;
        bleNotify("WIFI:CONNECTING");
    } else if (command.startsWith("SYNC_TIME:")) {
        time_t epoch = (time_t)strtoll(command.substring(10).c_str(), nullptr, 10);
        if (epoch > 1700000000) {
            timeval tv = { epoch, 0 };
            settimeofday(&tv, nullptr);
            bleNotify("TIME:OK");
        } else bleNotify("TIME:INVALID");
    } else if (command == "SLEEP_ON" || command == "SLEEP_OFF") {
        bleNotify(command == "SLEEP_ON" ? "SLEEP:ACK_ON" : "SLEEP:ACK_OFF");
    } else if (command == "SCAN_WIFI") {
        scanWifiNetworks();
    } else {
        bleNotify("ERROR:UNKNOWN_COMMAND");
    }
}

void setup() {
    Serial.begin(115200);
    delay(250);
    Serial.println("\n[BOOT] ESP32-C3 Hybrid Smart Watch");
    commandQueue = xQueueCreate(8, sizeof(BleCommand));
    beginButtonManager();
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    showBootTerminal();

    loadWeatherCache();
    drawCurrentScreen(true);

    WiFi.mode(WIFI_STA);
    connectSavedWiFi();
    beginTimeService();
    startBluetooth();
}

void loop() {
    const uint32_t now = millis();
    updateButtonManager();

    BleCommand cmd;
    while (commandQueue && xQueueReceive(commandQueue, &cmd, 0) == pdTRUE) {
        handleBleCommand(cmd.value);
    }

    const bool wifiConnected = wifiEnabled && WiFi.status() == WL_CONNECTED;
    if (wifiConnected && !previousWifiConnected) {
        Serial.println("[WIFI] Connected; starting NTP and weather refresh");
        beginTimeService();
        lastWeatherFetchMs = 0;
        lastWeatherRetryMs = 0;
        scheduleWeatherFetch();
    }
    if (!wifiConnected && previousWifiConnected) Serial.println("[WIFI] Connection lost");
    previousWifiConnected = wifiConnected;

    WeatherData snapshot = getWeatherSnapshot();
    weatherDataValid = snapshot.valid;
    if (wifiConnected && !snapshot.valid && !weatherFetchRunning &&
        (lastWeatherRetryMs == 0 || now - lastWeatherRetryMs >= WEATHER_RETRY_MS)) {
        if (scheduleWeatherFetch()) lastWeatherRetryMs = now;
    }
    if (wifiConnected && snapshot.valid && !weatherFetchRunning &&
        now - lastWeatherFetchMs >= WEATHER_REFRESH_MS) scheduleWeatherFetch();

    if (weatherFetchFinished) {
        weatherFetchFinished = false;
        if (currentScreen == SCREEN_WEATHER) {
            drawWeatherFaceData(getWeatherSnapshot());
            weatherFaceInitialized = true;
        }
    }

    if (currentScreen == SCREEN_MENU && now - lastMenuFrameMs >= 40) {
        drawMenuScreen(menuIndex, currentMenuLevel, wifiEnabled, wifiConnected);
        lastMenuFrameMs = now;
    }

    if (currentScreen == SCREEN_CLOCK && now - lastClockDrawMs >= 40) {
        static int cachedSecond = -1;
        static struct tm cachedTime = {};
        static uint32_t secondStartedAt = 0;
        struct tm currentTime = {};
        if (getLocalTime(&currentTime, 0)) {
            if (currentTime.tm_sec != cachedSecond) {
                cachedSecond = currentTime.tm_sec;
                cachedTime = currentTime;
                secondStartedAt = now;
            }
        } else {
            cachedTime.tm_hour = 12;
            cachedTime.tm_min = 0;
            cachedTime.tm_sec = (now / 1000) % 60;
            cachedTime.tm_wday = 0;
            cachedTime.tm_mday = 1;
            secondStartedAt = now - (now % 1000);
        }
        uint32_t elapsed = min((uint32_t)999, now - secondStartedAt);
        drawAnalogClockFace(cachedTime, (int)elapsed, 0, 0);
        lastClockDrawMs = now;
    }

    if (currentScreen != SCREEN_WEATHER) {
        weatherFaceInitialized = false;
    } else if (now - lastWeatherFrameMs >= 80) {
        WeatherData weather = getWeatherSnapshot();
        if (!weatherFaceInitialized) {
            drawWeatherFaceData(weather);
            weatherFaceInitialized = true;
        }
        updateWeatherFaceAnimation(weather);
        lastWeatherFrameMs = now;
    }
    delay(2);
}
