#include "command_handler.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <sys/time.h>
#include <string.h>
#include <math.h>

#include "../services/weather_service.h"

extern bool wifiEnabled;
extern void requestLightSleep();
extern uint8_t requestWeatherFetch();
extern uint8_t requestWeatherFetchForLocation();
extern uint8_t requestWifiScan();
extern bool isWifiScanBusy();
extern void setAutoSleepTimeoutSeconds(uint8_t seconds);
extern uint8_t getAutoSleepTimeoutSeconds();

namespace CommandHandler {
namespace {
void setResult(Result& result, bool ok, uint16_t code, const char* message) {
    result.ok = ok;
    result.errorCode = code;
    if (message == nullptr) message = "";
    strncpy(result.message, message, sizeof(result.message) - 1);
}

bool validRequestId(const char* value) {
    if (value == nullptr || value[0] == '\0') return true;
    if (strlen(value) > 32) return false;
    for (const char* p = value; *p; ++p) {
        const char ch = *p;
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) return false;
    }
    return true;
}

bool validText(const char* value, size_t maxLength, bool allowEmpty = false) {
    if (value == nullptr) return allowEmpty;
    const size_t length = strlen(value);
    if ((!allowEmpty && length == 0) || length > maxLength) return false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

bool persistWifi(const char* ssid, const char* password) {
    Preferences prefs;
    if (!prefs.begin("watch", false)) return false;
    const size_t ssidWritten = prefs.putString("ssid", ssid);
    const size_t passWritten = prefs.putString("pass", password);
    const String savedSsid = prefs.getString("ssid", "");
    const String savedPass = prefs.getString("pass", "");
    prefs.end();
    return ssidWritten > 0 && passWritten > 0 &&
           savedSsid == ssid && savedPass == password;
}
}

bool dispatch(const uint8_t* payload, size_t length, Result& result) {
    result = {};
    if (payload == nullptr || length == 0 || length > 220) {
        setResult(result, false, 0x0010, "invalid_command_payload");
        return false;
    }

    JsonDocument request;
    if (deserializeJson(request, payload, length, DeserializationOption::NestingLimit(4))) {
        setResult(result, false, 0x0010, "invalid_command_json");
        return false;
    }

    const char* requestId = request["requestId"] | "";
    if (!validRequestId(requestId)) {
        setResult(result, false, 0x0010, "invalid_request_id");
        return false;
    }
    if (requestId != nullptr && strlen(requestId) < sizeof(result.requestId)) {
        strncpy(result.requestId, requestId, sizeof(result.requestId) - 1);
    }
    const char* name = request["name"] | "";
    if (name == nullptr || name[0] == '\0') {
        setResult(result, false, 0x0010, "missing_command_name");
        return false;
    }

    if (strcmp(name, "TIME_SET") == 0) {
        const int64_t epoch = request["unixEpoch"] | 0LL;
        if (epoch < 1700000000LL || epoch > 4102444800LL) {
            setResult(result, false, 0x0010, "invalid_unix_epoch");
            return false;
        }
        timeval tv = {static_cast<time_t>(epoch), 0};
        if (settimeofday(&tv, nullptr) != 0) {
            setResult(result, false, 0x0010, "time_update_failed");
            return false;
        }
        setResult(result, true, 0, "time_updated");
        return true;
    }

    if (strcmp(name, "WEATHER_REFRESH") == 0) {
        const uint8_t code = requestWeatherFetch();
        if (code == 0) setResult(result, true, 0, "weather_refresh_started");
        else if (code == 1) setResult(result, false, 0x0050, "weather_service_busy");
        else if (code == 2) setResult(result, false, 0x0010, "wifi_not_connected");
        else setResult(result, false, 0x0010, "weather_cooldown");
        return result.ok;
    }

    if (strcmp(name, "LOCATION_SET") == 0) {
        const double latitude = request["latitude"] | 999.0;
        const double longitude = request["longitude"] | 999.0;
        if (!isfinite(latitude) || !isfinite(longitude) ||
            latitude < -90.0 || latitude > 90.0 ||
            longitude < -180.0 || longitude > 180.0) {
            setResult(result, false, 0x0010, "invalid_coordinates");
            return false;
        }
        Preferences prefs;
        if (!prefs.begin("watch", false)) {
            setResult(result, false, 0x0023, "location_storage_unavailable");
            return false;
        }
        prefs.putFloat("lat", static_cast<float>(latitude));
        prefs.putFloat("lon", static_cast<float>(longitude));
        prefs.putString("locationSource", "gps");
        prefs.end();
        setWeatherCoordinates(static_cast<float>(latitude), static_cast<float>(longitude));
        const uint8_t refresh = requestWeatherFetchForLocation();
        setResult(result, true, 0, refresh == 0 ? "location_saved_weather_started" :
                                                "location_saved_weather_deferred");
        result.value = refresh;
        return true;
    }

    if (strcmp(name, "WIFI_SCAN") == 0) {
        const uint8_t code = requestWifiScan();
        if (code == 0) setResult(result, true, 0, "wifi_scan_started");
        else if (code == 1) setResult(result, false, 0x0050, "wifi_scan_busy");
        else if (code == 2) setResult(result, false, 0x0050, "wifi_scan_deferred");
        else setResult(result, false, 0x0010, "wifi_scan_failed");
        return result.ok;
    }

    if (strcmp(name, "WIFI_CONFIG") == 0) {
        const char* ssid = request["ssid"] | "";
        const char* password = request["password"] | "";
        if (!validText(ssid, 32) || !validText(password, 63, true)) {
            setResult(result, false, 0x0010, "invalid_wifi_credentials");
            return false;
        }
        if (isWifiScanBusy()) {
            setResult(result, false, 0x0050, "wifi_scan_busy");
            return false;
        }
        if (!persistWifi(ssid, password)) {
            setResult(result, false, 0x0023, "wifi_credentials_storage_failed");
            return false;
        }
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        wifiEnabled = true;
        WiFi.begin(ssid, password);
        setResult(result, true, 0, "wifi_credentials_saved_connecting");
        return true;
    }

    if (strcmp(name, "AUTO_SLEEP_GET") == 0) {
        setResult(result, true, 0, "auto_sleep_read");
        result.value = getAutoSleepTimeoutSeconds();
        return true;
    }

    if (strcmp(name, "AUTO_SLEEP_SET") == 0) {
        const int seconds = request["seconds"] | -1;
        if (!(seconds == 0 || seconds == 10 || seconds == 12 ||
              seconds == 15 || seconds == 18 || seconds == 20)) {
            setResult(result, false, 0x0010, "invalid_auto_sleep_value");
            return false;
        }
        Preferences prefs;
        if (!prefs.begin("watch", false)) {
            setResult(result, false, 0x0023, "auto_sleep_storage_unavailable");
            return false;
        }
        const size_t written = prefs.putUChar("autoSleepSec", static_cast<uint8_t>(seconds));
        const uint8_t saved = prefs.getUChar("autoSleepSec", 0xFF);
        prefs.end();
        if (written != sizeof(uint8_t) || saved != static_cast<uint8_t>(seconds)) {
            setResult(result, false, 0x0023, "auto_sleep_storage_failed");
            return false;
        }
        setAutoSleepTimeoutSeconds(static_cast<uint8_t>(seconds));
        setResult(result, true, 0, "auto_sleep_updated");
        result.value = seconds;
        return true;
    }

    if (strcmp(name, "POWER_SLEEP") == 0) {
        setResult(result, true, 0, "sleep_requested");
        result.requestSleep = true;
        return true;
    }

    setResult(result, false, 0x0010, "unknown_command");
    return false;
}

void executeDeferred(const Result& result) {
    if (result.requestSleep) requestLightSleep();
}

} // namespace CommandHandler
