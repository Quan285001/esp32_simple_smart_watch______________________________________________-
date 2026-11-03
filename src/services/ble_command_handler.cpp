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

static void handleCommand(const String &cmd) {
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
        setWeatherCoordinates(lat, lon);
        Preferences prefs;
        prefs.begin("watch", false);
        prefs.putFloat("lat", lat);
        prefs.putFloat("lon", lon);
        prefs.end();
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
