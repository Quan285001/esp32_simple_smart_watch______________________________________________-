#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <sys/time.h>
#include <ArduinoJson.h>

#include "ble_service.h"
#include "weather_service.h"

extern bool wifiEnabled;
/**
 * @brief Implements the request light sleep operation.
 * @param cmd Input or state used by this operation.
 */
extern void requestLightSleep();
extern uint8_t requestWeatherFetch();
extern uint8_t requestWeatherFetchForLocation();
extern void setAutoSleepTimeoutSeconds(uint8_t seconds);
extern uint8_t getAutoSleepTimeoutSeconds();

/**
 * @brief Parses and executes one BLE command.
 * @param cmd Input or state used by this operation.
 */
static void handleCommand(const String &cmd) {
    if (cmd == "GET_AUTO_SLEEP") {
        sendBLENotification(String("AUTO_SLEEP:") + String(getAutoSleepTimeoutSeconds()));
        return;
    }

    if (cmd.startsWith("AUTO_SLEEP:")) {
        const String value = cmd.substring(11);
        const int seconds = value.toInt();
        const bool validValue = value == String(seconds) &&
            (seconds == 0 || seconds == 10 || seconds == 12 ||
             seconds == 15 || seconds == 18 || seconds == 20);
        if (!validValue) {
            sendBLENotification("ERR_AUTO_SLEEP");
            return;
        }

        Preferences prefs;
        if (!prefs.begin("watch", false)) {
            sendBLENotification("ERR_AUTO_SLEEP_STORAGE");
            return;
        }

        // Acknowledge the setting only after NVS confirms the stored value.
        const size_t written = prefs.putUChar(
            "autoSleepSec", static_cast<uint8_t>(seconds));
        const uint8_t savedValue = prefs.getUChar("autoSleepSec", 0xFF);
        prefs.end();

        if (written != sizeof(uint8_t) ||
            savedValue != static_cast<uint8_t>(seconds)) {
            Serial.println("[POWER] Failed to persist Auto Sleep setting.");
            sendBLENotification("ERR_AUTO_SLEEP_STORAGE");
            return;
        }

        setAutoSleepTimeoutSeconds(static_cast<uint8_t>(seconds));
        Serial.printf("[POWER] Auto Sleep setting saved to NVS: %d seconds.\n",
                      seconds);
        sendBLENotification(String("AUTO_SLEEP_OK:") + String(seconds));
        return;
    }
    if (cmd.startsWith("SYNC_TIME:")) {
        time_t epoch = static_cast<time_t>(cmd.substring(10).toInt());
        if (epoch > 1700000000) {
            timeval tv = { epoch, 0 };
            settimeofday(&tv, nullptr);
            sendBLENotification("TIME_OK");
        } else sendBLENotification("ERR_TIME");
        return;
    }

    if (cmd == "SYNC_WEATHER") {
        const uint8_t result = requestWeatherFetch();
        if (result == 0) sendBLENotification("WEATHER_STARTED");
        /**
         * @brief Implements the if operation.
         * @param (cmd.startsWith("GPS:") Input or state used by this operation.
         */
        else if (result == 1) sendBLENotification("WEATHER_BUSY");
        else if (result == 2) sendBLENotification("ERR_WIFI");
        else sendBLENotification("WEATHER_COOLDOWN");
        return;
    }

    if (cmd.startsWith("GPS:")) {
        String value = cmd.substring(4);
        int comma = value.indexOf(',');
        if (comma < 1) { sendBLENotification("ERR_GPS"); return; }
        float lat = value.substring(0, comma).toFloat();
        float lon = value.substring(comma + 1).toFloat();
        if (lat < -90 || lat > 90 || lon < -180 || lon > 180) {
            sendBLENotification("ERR_GPS"); return;
        }
        Preferences prefs;
        if (!prefs.begin("watch", false)) {
            sendBLENotification("ERR_GPS_STORAGE");
            return;
        }
        prefs.putFloat("lat", lat);
        prefs.putFloat("lon", lon);
        prefs.putString("locationSource", "gps");
        prefs.end();

        // A phone GPS fix is the primary location source and also drives the
        // next weather request, which resolves the destination timezone.
        setWeatherCoordinates(lat, lon);
        const uint8_t refreshResult = requestWeatherFetchForLocation();
        Serial.printf("[GPS] Phone location saved; weather refresh result=%u.\n",
                      static_cast<unsigned int>(refreshResult));
        sendBLENotification("GPS_OK");
        return;
    }

    if (cmd.startsWith("WIFI:")) {
        String value = cmd.substring(5);
        int comma = value.indexOf(',');
        if (comma < 1) { sendBLENotification("ERR_WIFI_CONFIG"); return; }
        String ssid = value.substring(0, comma);
        String password = value.substring(comma + 1);
        Preferences prefs;
        prefs.begin("watch", false);
        prefs.putString("ssid", ssid);
        prefs.putString("pass", password);
        prefs.end();
        WiFi.mode(WIFI_STA);
        wifiEnabled = true;
        WiFi.setAutoReconnect(true);
        WiFi.begin(ssid.c_str(), password.c_str());
        sendBLENotification("WIFI_SAVED");
        return;
    }

    if (cmd == "SCAN_WIFI") {
        Serial.println("[WIFI] BLE scan request received.");
        sendBLENotification("WIFI_SCAN_STARTED");

        const bool restoreWifiOff = !wifiEnabled;
        Serial.printf("[WIFI] Starting scan (restore_off=%s, mode=%d, status=%d).\n",
                      restoreWifiOff ? "true" : "false",
                      static_cast<int>(WiFi.getMode()),
                      static_cast<int>(WiFi.status()));

        if (restoreWifiOff) WiFi.mode(WIFI_STA);
        int count = WiFi.scanNetworks(false, true);
        Serial.printf("[WIFI] Scan finished; result=%d.\n", count);

        if (count < 0) {
            WiFi.scanDelete();
            if (restoreWifiOff) WiFi.mode(WIFI_OFF);
            sendBLENotification("WIFI_SCAN_ERROR");
            return;
        }

        JsonDocument doc;
        JsonArray list = doc.to<JsonArray>();
        for (int i = 0; i < count; ++i) list.add(WiFi.SSID(i));

        String json;
        serializeJson(doc, json);
        Serial.printf("[WIFI] Sending %d networks (%u JSON bytes).\n",
                      count, static_cast<unsigned int>(json.length()));

        WiFi.scanDelete();
        if (restoreWifiOff) WiFi.mode(WIFI_OFF);
        sendBLENotification(json);
        return;
    }

    if (cmd == "SLEEP_ON") {
        sendBLENotification("SLEEPING");
        requestLightSleep();
        return;
    }

    if (cmd == "SLEEP_OFF") {
        sendBLENotification("AWAKE");
        return;
    }
}

/**
 * @brief Drains the BLE command queue and dispatches each received command.

 */
void handleBLECommands() {
    String command;
    while (readBLECommand(command)) {
        Serial.printf("[BLE] Received command (%u bytes).\n",
                      static_cast<unsigned int>(command.length()));
        handleCommand(command);
    }
}
