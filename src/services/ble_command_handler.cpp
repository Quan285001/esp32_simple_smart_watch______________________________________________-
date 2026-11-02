#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <sys/time.h>
#include <ArduinoJson.h>
#include <sys/time.h>

#include "ble_service.h"
#include "weather_service.h"

extern bool wifiEnabled;
extern void toggleWiFi();
extern void requestLightSleep();

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
        if (wifiEnabled && WiFi.status() == WL_CONNECTED) {
            fetchHourlyWeather();
            sendBLENotification("WEATHER_OK");
        } else sendBLENotification("ERR_WIFI");
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
        const bool restoreWifiOff = !wifiEnabled;
        if (restoreWifiOff) WiFi.mode(WIFI_STA);
        int count = WiFi.scanNetworks(false, true);
        JsonDocument doc;
        JsonArray list = doc.to<JsonArray>();
        for (int i = 0; i < count; ++i) list.add(WiFi.SSID(i));
        String json;
        serializeJson(doc, json);
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

void handleBLECommands() {
    String command;
    while (readBLECommand(command)) {
        Serial.printf("[BLE] Received command (%u bytes).\n",
                      static_cast<unsigned int>(command.length()));
        handleCommand(command);
    }
}
