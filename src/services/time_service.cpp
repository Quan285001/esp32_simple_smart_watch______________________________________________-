// time_service.cpp
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

#include "time_service.h"
#include "../config.h"
#include "weather_service.h"

static constexpr long DEFAULT_UTC_OFFSET_SEC = 7L * 3600L;
static constexpr uint32_t GEO_HTTP_TIMEOUT_MS = 5000;
static constexpr uint64_t MIN_VALID_EPOCH = 1704067200ULL; // 2024-01-01 UTC
static constexpr uint32_t TIME_CHECKPOINT_INTERVAL_MS = 30UL * 60UL * 1000UL;
static uint32_t lastTimeCheckpointMs = 0;

/**
 * Restore the last valid Unix timestamp and timezone from NVS.
 * A still-valid system clock is never overwritten (important for software resets).
 */
bool restoreTimeFromNvs() {
    long utcOffset = DEFAULT_UTC_OFFSET_SEC;
    uint64_t savedEpoch = 0;
    String timezoneName;

    Preferences prefs;
    if (prefs.begin("watch", true)) {
        utcOffset = prefs.getLong("utcOffset", DEFAULT_UTC_OFFSET_SEC);
        savedEpoch = prefs.getULong64("lastEpoch", 0);
        timezoneName = prefs.getString("timezone", "");
        prefs.end();
    }

    if (utcOffset < -43200L || utcOffset > 50400L) {
        utcOffset = DEFAULT_UTC_OFFSET_SEC;
    }

    // Apply the saved local offset even while offline.
    configTime(utcOffset, 0, NTP_SERVER, "time.google.com");

    const time_t currentEpoch = time(nullptr);
    if (static_cast<uint64_t>(currentEpoch) >= MIN_VALID_EPOCH) {
        lastTimeCheckpointMs = millis();
        Serial.println("[TIME] System clock already valid; preserving it.");
        return true;
    }

    if (savedEpoch < MIN_VALID_EPOCH) {
        Serial.println("[TIME] No valid saved epoch in NVS.");
        return false;
    }

    struct timeval restoredTime;
    restoredTime.tv_sec = static_cast<time_t>(savedEpoch);
    restoredTime.tv_usec = 0;
    if (settimeofday(&restoredTime, nullptr) != 0) {
        Serial.println("[TIME] Failed to restore saved epoch.");
        return false;
    }

    lastTimeCheckpointMs = millis();
    Serial.printf("[TIME] Restored checkpoint: %llu (%s, UTC offset %ld sec).\\n",
                  static_cast<unsigned long long>(savedEpoch),
                  timezoneName.c_str(), utcOffset);
    return true;
}

/** Save a valid current Unix timestamp to NVS. Call after successful NTP sync. */
bool saveTimeCheckpoint() {
    const time_t epoch = time(nullptr);
    if (static_cast<uint64_t>(epoch) < MIN_VALID_EPOCH) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin("watch", false)) {
        Serial.println("[TIME] Cannot open NVS for time checkpoint.");
        return false;
    }
    const size_t written = prefs.putULong64("lastEpoch", static_cast<uint64_t>(epoch));
    prefs.end();

    if (written == 0) {
        Serial.println("[TIME] NVS time checkpoint write failed.");
        return false;
    }

    lastTimeCheckpointMs = millis();
    return true;
}

/** Checkpoint at a bounded interval; never write flash on every clock frame. */
void serviceTimePersistence() {
    if (lastTimeCheckpointMs == 0 ||
        millis() - lastTimeCheckpointMs >= TIME_CHECKPOINT_INTERVAL_MS) {
        saveTimeCheckpoint();
    }
}

/**
 * Resolve an approximate location and current UTC offset from the public IP.
 * This is a fallback only: a saved phone/browser GPS location always wins.
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
 * Configure NTP using the last known offset. GPS coordinates are authoritative;
 * IP geolocation is used only when no phone GPS location has been saved.
 */
void beginTimeService() {
    long utcOffset = DEFAULT_UTC_OFFSET_SEC;
    float savedLatitude = NAN;
    float savedLongitude = NAN;
    String timezoneName;
    bool hasSavedGps = false;

    Preferences prefs;
    const bool prefsReady = prefs.begin("watch", false);
    if (prefsReady) {
        utcOffset = prefs.getLong("utcOffset", DEFAULT_UTC_OFFSET_SEC);
        timezoneName = prefs.getString("timezone", "");
        const String locationSource = prefs.getString("locationSource", "");
        savedLatitude = prefs.getFloat("lat", NAN);
        savedLongitude = prefs.getFloat("lon", NAN);
        hasSavedGps = locationSource == "gps" &&
                      isfinite(savedLatitude) && isfinite(savedLongitude) &&
                      savedLatitude >= -90.0f && savedLatitude <= 90.0f &&
                      savedLongitude >= -180.0f && savedLongitude <= 180.0f;
    }

    if (hasSavedGps) {
        // Keep the last explicit phone location across reboot/offline periods.
        setWeatherCoordinates(savedLatitude, savedLongitude);
        Serial.printf("[TIME] Using saved phone GPS location; timezone=%s, offset=%ld sec\n",
                      timezoneName.c_str(), utcOffset);
    } else {
        long detectedOffset = utcOffset;
        float latitude = NAN;
        float longitude = NAN;
        String detectedTimezone;

        if (updateTimezoneFromIp(detectedOffset, latitude, longitude, detectedTimezone)) {
            utcOffset = detectedOffset;
            timezoneName = detectedTimezone;
            Serial.printf("[TIME] IP fallback timezone: %s (UTC offset %ld sec)\n",
                          timezoneName.c_str(), utcOffset);

            if (prefsReady) {
                prefs.putLong("utcOffset", utcOffset);
                prefs.putString("timezone", timezoneName);
                prefs.putFloat("lat", latitude);
                prefs.putFloat("lon", longitude);
                prefs.putString("locationSource", "ip");
            }

            setWeatherCoordinates(latitude, longitude);
        } else {
            Serial.printf("[TIME] IP fallback unavailable; using saved/default offset %ld sec\n",
                          utcOffset);
        }
    }

    if (prefsReady) prefs.end();

    // The offset is refreshed from Open-Meteo after a GPS-based weather request.
    configTime(utcOffset, 0, NTP_SERVER, "time.google.com");
}

/**
 * Apply the timezone resolved by Open-Meteo for the current weather coordinates.
 * Open-Meteo returns the IANA timezone name and the current UTC offset.
 */
void applyTimezoneFromWeather(const String &timezoneName, long utcOffset) {
    if (timezoneName.length() == 0 || utcOffset < -43200L || utcOffset > 50400L) {
        Serial.println("[TIME] Ignoring invalid weather timezone.");
        return;
    }

    Preferences prefs;
    if (prefs.begin("watch", false)) {
        prefs.putLong("utcOffset", utcOffset);
        prefs.putString("timezone", timezoneName);
        prefs.end();
    }

    Serial.printf("[TIME] Coordinate timezone: %s (UTC offset %ld sec)\n",
                  timezoneName.c_str(), utcOffset);

    // NTP maintains UTC; this offset controls local-time conversion.
    configTime(utcOffset, 0, NTP_SERVER, "time.google.com");
}

/** Read the current local time using the configured system clock. */
bool readLocalTime(struct tm &timeinfo) {
    return getLocalTime(&timeinfo, 10);
}
