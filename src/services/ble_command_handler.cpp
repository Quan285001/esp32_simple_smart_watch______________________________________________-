#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <sys/time.h>

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
extern uint8_t requestWifiScan();
extern bool isWifiScanBusy();

/**
 * @brief Parses and executes one BLE command.
 * @param cmd Input or state used by this operation.
 */
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
        if (isWifiScanBusy()) {
            sendBLENotification("ERR_WIFI_SCAN_BUSY");
            return;
        }
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
        Serial.println("[WIFI] BLE asynchronous scan request received.");
        const uint8_t result = requestWifiScan();
        if (result == 0) sendBLENotification("WIFI_SCAN_STARTED");
        else if (result == 1) sendBLENotification("WIFI_SCAN_BUSY");
        else if (result == 2) sendBLENotification("WIFI_SCAN_DEFERRED");
        else sendBLENotification("WIFI_SCAN_ERROR");
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
