#include "wifi_service.h"

#include <Preferences.h>
#include <WiFi.h>
#include <string.h>

extern bool wifiEnabled;

namespace WifiService {
namespace {
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

bool isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

} // namespace WifiService
