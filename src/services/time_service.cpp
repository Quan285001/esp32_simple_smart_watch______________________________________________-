// time_service.cpp
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

#include "time_service.h"
#include "../config.h"
#include "weather_service.h"

static constexpr long DEFAULT_UTC_OFFSET_SEC = 7L * 3600L;
static constexpr uint32_t GEO_HTTP_TIMEOUT_MS = 5000;

/**
 * Resolve approximate location and current UTC offset from the public IP.
 * This is IP geolocation, NOT satellite GPS. It is refreshed whenever the
 * device synchronizes time after connecting to Wi-Fi.
 */
static bool updateTimezoneFromIp(long &utcOffset, float &latitude, float &longitude,
                                 String &timezoneName) {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(GEO_HTTP_TIMEOUT_MS);
    http.setTimeout(GEO_HTTP_TIMEOUT_MS);

    const char *url =
        "http://ip-api.com/json/?fields=status,message,lat,lon,timezone,offset";

    if (!http.begin(client, url)) {
        Serial.println("[TIME] IP geolocation HTTP begin failed.");
        return false;
    }

    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
        Serial.printf("[TIME] IP geolocation HTTP error: %d\n", status);
        http.end();
        return false;
    }

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, http.getStream());
    http.end();

    if (error || doc["status"] != "success" ||
        doc["offset"].isNull() || doc["timezone"].isNull() ||
        doc["lat"].isNull() || doc["lon"].isNull()) {
        Serial.println("[TIME] Invalid IP geolocation response.");
        return false;
    }

    utcOffset = doc["offset"].as<long>();
    latitude = doc["lat"].as<float>();
    longitude = doc["lon"].as<float>();
    timezoneName = doc["timezone"].as<String>();

    if (utcOffset < -43200L || utcOffset > 50400L ||
        latitude < -90.0f || latitude > 90.0f ||
        longitude < -180.0f || longitude > 180.0f ||
        timezoneName.length() == 0) {
        Serial.println("[TIME] IP geolocation values out of range.");
        return false;
    }

    return true;
}

/**
 * Configure local time from the IP-derived UTC offset, then synchronize the
 * underlying clock from NTP. If geolocation is unavailable, reuse the last
 * saved offset; UTC+7 is only the first-boot fallback.
 */
void beginTimeService() {
    long utcOffset = DEFAULT_UTC_OFFSET_SEC;
    float latitude = NAN;
    float longitude = NAN;
    String timezoneName;

    Preferences prefs;
    bool prefsReady = prefs.begin("watch", false);
    if (prefsReady) {
        utcOffset = prefs.getLong("utcOffset", DEFAULT_UTC_OFFSET_SEC);
        timezoneName = prefs.getString("timezone", "");
    }

    long detectedOffset = utcOffset;
    if (updateTimezoneFromIp(detectedOffset, latitude, longitude, timezoneName)) {
        utcOffset = detectedOffset;
        Serial.printf("[TIME] IP timezone: %s (UTC offset %ld sec)\n",
                      timezoneName.c_str(), utcOffset);

        if (prefsReady) {
            prefs.putLong("utcOffset", utcOffset);
            prefs.putString("timezone", timezoneName);
            prefs.putFloat("lat", latitude);
            prefs.putFloat("lon", longitude);
        }

        // Use IP-derived coordinates for the weather forecast as well.
        setWeatherCoordinates(latitude, longitude);
    } else {
        Serial.printf("[TIME] Geolocation unavailable; using saved/default UTC offset %ld sec\n",
                      utcOffset);
    }

    if (prefsReady) prefs.end();

    // ESP32 Arduino configTime expects an offset in seconds, not an IANA name.
    // The offset is refreshed on each Wi-Fi time-sync event, including travel.
    configTime(utcOffset, 0, NTP_SERVER, "time.google.com");
}

/** Read the current local time using the configured system clock. */
bool readLocalTime(struct tm &timeinfo) {
    return getLocalTime(&timeinfo, 10);
}
