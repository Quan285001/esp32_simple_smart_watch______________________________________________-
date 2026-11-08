#include "wifi_service.h"

#include <Preferences.h>
#include <WiFi.h>
#include <string.h>
#include <ArduinoJson.h>
#include "ble_service.h"

extern bool wifiEnabled;

namespace WifiService {
namespace {
bool scanRunning = false;
bool scanRestoreOff = false;

bool validCredential(const char* value, size_t maxLength, bool allowEmpty) {
    if (value == nullptr) return allowEmpty;
    const size_t length = strlen(value);
    if ((!allowEmpty && length == 0) || length > maxLength) return false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}
}

bool configureCredentials(const char* ssid, const char* password) {
    if (!validCredential(ssid, 32, false) ||
        !validCredential(password, 63, true)) return false;

    Preferences prefs;
    if (!prefs.begin("watch", false)) return false;
    const size_t ssidWritten = prefs.putString("ssid", ssid);
    const size_t passwordWritten = prefs.putString("pass", password);
    const String savedSsid = prefs.getString("ssid", "");
    const String savedPassword = prefs.getString("pass", "");
    prefs.end();

    if (ssidWritten == 0 || passwordWritten == 0 ||
        savedSsid != ssid || savedPassword != password) return false;

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    wifiEnabled = true;
    WiFi.begin(ssid, password);
    return true;
}

uint8_t requestScan(bool weatherBusy, bool bootNetworkBusy, bool transferBusy) {
    if (scanRunning) return 1;
    if (weatherBusy || bootNetworkBusy || transferBusy) return 2;

    scanRestoreOff = !wifiEnabled;
    if (scanRestoreOff) WiFi.mode(WIFI_STA);

    const int result = WiFi.scanNetworks(true, true);
    if (result == WIFI_SCAN_FAILED) {
        WiFi.scanDelete();
        if (scanRestoreOff) WiFi.mode(WIFI_OFF);
        scanRestoreOff = false;
        Serial.println("[WIFI] Could not start asynchronous scan.");
        return 3;
    }

    scanRunning = true;
    Serial.println("[WIFI] Asynchronous scan started.");
    return 0;
}

bool isScanBusy() {
    return scanRunning;
}

void serviceScan() {
    if (!scanRunning) return;
    const int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) return;

    if (count == WIFI_SCAN_FAILED) {
        Serial.println("[WIFI] Asynchronous scan failed.");
        WiFi.scanDelete();
        if (scanRestoreOff) WiFi.mode(WIFI_OFF);
        scanRestoreOff = false;
        scanRunning = false;
        sendBLENotification("WIFI_SCAN_ERROR");
        return;
    }

    JsonDocument doc;
    JsonArray list = doc.to<JsonArray>();
    const int returned = count < 12 ? count : 12;
    for (int i = 0; i < returned; ++i) list.add(WiFi.SSID(i));

    String json;
    serializeJson(doc, json);
    Serial.printf("[WIFI] Scan complete: %d found, %d returned, %u JSON bytes.\n",
                  count, returned, static_cast<unsigned int>(json.length()));

    WiFi.scanDelete();
    if (scanRestoreOff) WiFi.mode(WIFI_OFF);
    scanRestoreOff = false;
    scanRunning = false;
    sendBLENotification(json);
}

bool isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

} // namespace WifiService
