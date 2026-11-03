
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "weather_service.h"

// =====================================================
// SHARED WEATHER DATA
// =====================================================

// Start with no fabricated weather values. Data becomes valid only after a
// complete, validated API response has been received.
WeatherData currentWeather = {};

static String weatherLatitude = WEATHER_LATITUDE;
static String weatherLongitude = WEATHER_LONGITUDE;

void setWeatherCoordinates(float latitude, float longitude) {
    if (!isfinite(latitude) || !isfinite(longitude) ||
        latitude < -90.0f || latitude > 90.0f ||
        longitude < -180.0f || longitude > 180.0f) return;
    weatherLatitude = String(latitude, 4);
    weatherLongitude = String(longitude, 4);
    Serial.printf("[GPS] Weather coordinates updated: %s, %s\n",
                  weatherLatitude.c_str(), weatherLongitude.c_str());
}

static SemaphoreHandle_t getWeatherMutex() {
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    return mutex;
}

// Clear the published snapshot before a new request. Any failed request leaves
// the UI in an explicit unavailable state instead of showing stale/demo values.
// =====================================================
// PERSISTENT WEATHER CACHE
// =====================================================

static void saveWeatherCache(const WeatherData& data) {
    JsonDocument doc;
    doc["currentTemp"] = data.currentTemp;
    doc["maxTemp"] = data.maxTemp;
    doc["minTemp"] = data.minTemp;
    doc["conditionText"] = data.conditionText;
    doc["windSpeed10m"] = data.windSpeed10m;
    doc["surfacePressure"] = data.surfacePressure;
    doc["rain"] = data.rain;
    doc["sunshineDuration"] = data.sunshineDuration;
    doc["daylightDuration"] = data.daylightDuration;
    doc["moonPhase"] = data.moonPhase;
    doc["moonrise"] = data.moonrise;
    doc["moonset"] = data.moonset;
    doc["sunrise"] = data.sunrise;
    doc["sunset"] = data.sunset;
    doc["detailsValid"] = data.detailsValid;

    JsonArray hourly = doc["hourly"].to<JsonArray>();
    for (int i = 0; i < 24; ++i) {
        JsonObject item = hourly.add<JsonObject>();
        item["hour"] = data.hourly[i].hour;
        item["temp"] = data.hourly[i].temp;
        item["weatherCode"] = data.hourly[i].weatherCode;
    }

    String payload;
    serializeJson(doc, payload);
    Preferences prefs;
    if (prefs.begin("weather", false)) {
        prefs.putString("cache", payload);
        prefs.putULong("savedAt", static_cast<uint32_t>(time(nullptr)));
        prefs.end();
    }
}

bool loadWeatherCache() {
    Preferences prefs;
    if (!prefs.begin("weather", true)) return false;
    String payload = prefs.getString("cache", "");
    prefs.end();
    if (payload.isEmpty()) return false;

    JsonDocument doc;
    if (deserializeJson(doc, payload)) return false;
    JsonArrayConst hourly = doc["hourly"].as<JsonArrayConst>();
    if (hourly.size() != 24 || doc["conditionText"].isNull()) return false;

    WeatherData cached = {};
    cached.currentTemp = doc["currentTemp"] | 0;
    cached.maxTemp = doc["maxTemp"] | 0;
    cached.minTemp = doc["minTemp"] | 0;
    cached.conditionText = doc["conditionText"].as<String>();
    cached.windSpeed10m = doc["windSpeed10m"] | 0.0f;
    cached.surfacePressure = doc["surfacePressure"] | 0.0f;
    cached.rain = doc["rain"] | 0.0f;
    cached.sunshineDuration = doc["sunshineDuration"] | 0.0f;
    cached.daylightDuration = doc["daylightDuration"] | 0.0f;
    cached.moonPhase = doc["moonPhase"] | "N/A";
    cached.moonrise = doc["moonrise"] | "N/A";
    cached.moonset = doc["moonset"] | "N/A";
    cached.sunrise = doc["sunrise"] | "N/A";
    cached.sunset = doc["sunset"] | "N/A";
    cached.detailsValid = doc["detailsValid"] | false;

    for (int i = 0; i < 24; ++i) {
        cached.hourly[i].hour = hourly[i]["hour"] | i;
        cached.hourly[i].temp = hourly[i]["temp"] | 0;
        cached.hourly[i].weatherCode = hourly[i]["weatherCode"] | 0;
    }
    cached.valid = true;

    SemaphoreHandle_t mutex = getWeatherMutex();
    if (mutex == nullptr ||
        xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    currentWeather = cached;
    xSemaphoreGive(mutex);
    Serial.println("[WEATHER] Persistent cache restored.");
    return true;
}

// =====================================================
// WEATHER DESCRIPTION
// =====================================================

static String weatherDescription(int code) {
    if (code == 0) return "Clear sky";
    if (code == 1) return "Mainly clear";
    if (code == 2) return "Partly cloudy";
    if (code == 3) return "Overcast";

    if (code == 45 || code == 48) return "Fog";

    if (code >= 51 && code <= 57) return "Drizzle";
    if (code >= 61 && code <= 67) return "Rain";
    if (code >= 71 && code <= 77) return "Snow";

    if (code >= 80 && code <= 82) return "Rain showers";
    if (code >= 85 && code <= 86) return "Snow showers";

    if (code >= 95 && code <= 99) return "Thunderstorm";

    return "Unknown";
}

// =====================================================
// FETCH WEATHER
// =====================================================

bool fetchHourlyWeather() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(
            "[WEATHER] Wi-Fi offline; weather data remains invalid."
        );
        return false;
    }

    String url =
        String("https://api.open-meteo.com/v1/forecast?latitude=") +
        weatherLatitude +
        "&longitude=" + weatherLongitude +
        "&current=temperature_2m,weather_code" +
        "&hourly=temperature_2m,weather_code,wind_speed_10m,surface_pressure,precipitation" +
        "&daily=temperature_2m_max,temperature_2m_min,sunshine_duration,daylight_duration,sunrise,sunset" +
        "&forecast_days=2&timezone=Asia%2FHo_Chi_Minh";

    WeatherData next = {};
    next.detailsValid = false;

    // Keep the first HTTPS client and response/JSON allocations in a short
    // scope so their destructors release resources before the Lunar TLS request.
    {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    http.setConnectTimeout(5000);
    http.setTimeout(5000);

    if (!http.begin(client, url)) {
        Serial.println("[WEATHER] HTTP begin failed.");
        return false;
    }

    int status = http.GET();

    if (status != HTTP_CODE_OK) {
        Serial.printf(
            "[WEATHER] HTTP error: %d\n",
            status
        );

        http.end();
        return false;
    }


    String payload = http.getString();

    Serial.printf(
        "[WEATHER] HTTP status: %d\n",
        status
    );

    Serial.printf(
        "[WEATHER] Response length: %u bytes\n",
        (unsigned int)payload.length()
    );

    if (payload.length() == 0) {
        Serial.println("[WEATHER] Empty response.");
        http.end();
        return false;
    }

    Serial.println("[WEATHER] Response preview:");

    Serial.println(payload.substring(0, 300));

    JsonDocument doc;

    DeserializationError error =
        deserializeJson(doc, payload);

    http.end();

    if (error) {
        Serial.printf("[WEATHER] JSON error: %s\n", error.c_str());
        return false;
    }

    // -------------------------------------------------
    // VALIDATE RESPONSE
    // -------------------------------------------------

    JsonVariantConst currentTemp =
        doc["current"]["temperature_2m"];

    JsonVariantConst currentCode =
        doc["current"]["weather_code"];

    JsonArrayConst times =
        doc["hourly"]["time"];

    JsonArrayConst temps =
        doc["hourly"]["temperature_2m"];

    JsonArrayConst codes =
        doc["hourly"]["weather_code"];

    JsonArrayConst windSpeeds = doc["hourly"]["wind_speed_10m"];
    JsonArrayConst pressures = doc["hourly"]["surface_pressure"];
    JsonArrayConst rainValues = doc["hourly"]["precipitation"];
    JsonArrayConst sunshine = doc["daily"]["sunshine_duration"];
    JsonArrayConst daylight = doc["daily"]["daylight_duration"];
    JsonArrayConst sunrise = doc["daily"]["sunrise"];
    JsonArrayConst sunset = doc["daily"]["sunset"];

    JsonArrayConst dailyMax =
        doc["daily"]["temperature_2m_max"];

    JsonArrayConst dailyMin =
        doc["daily"]["temperature_2m_min"];

    if (
        currentTemp.isNull() ||
        currentCode.isNull() ||
        times.size() < 24 ||
        temps.size() != times.size() ||
        codes.size() != times.size() ||
        windSpeeds.size() != times.size() ||
        pressures.size() != times.size() ||
        rainValues.size() != times.size() ||
        dailyMax.size() == 0 ||
        dailyMin.size() == 0 ||
        sunshine.size() == 0 ||
        daylight.size() == 0 ||
        sunrise.size() == 0 ||
        sunset.size() == 0
    ) {
        Serial.println(
            "[WEATHER] Incomplete response; weather data remains invalid."
        );
        return false;
    }

    // -------------------------------------------------
    // PREPARE NEW DATA
    // -------------------------------------------------

    // Build from an empty structure so failed/omitted optional fields cannot
    // inherit values from an earlier successful response.
    next.currentTemp = currentTemp.as<int>();
    next.maxTemp = dailyMax[0].as<int>();
    next.minTemp = dailyMin[0].as<int>();

    next.conditionText =
        weatherDescription(currentCode.as<int>());

    // -------------------------------------------------
    // FIND CURRENT LOCAL HOUR
    // -------------------------------------------------

    int firstIndex = -1;

    struct tm localTime = {};

    if (getLocalTime(&localTime, 10)) {
        char target[20];

        snprintf(
            target,
            sizeof(target),
            "%04d-%02d-%02dT%02d",
            localTime.tm_year + 1900,
            localTime.tm_mon + 1,
            localTime.tm_mday,
            localTime.tm_hour
        );

        for (size_t i = 0; i < times.size(); i++) {
            String stamp = times[i].as<String>();

            if (stamp.startsWith(target)) {
                firstIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (firstIndex < 0) {
        firstIndex = 0;

        Serial.println(
            "[WEATHER] Local time unavailable; using first forecast hour."
        );
    }

    if (static_cast<size_t>(firstIndex) + 24 > times.size()) {
        Serial.println("[WEATHER] Not enough hourly entries for a 24-hour forecast.");
        return false;
    }

    // -------------------------------------------------
    // LOAD 24 HOURS
    // -------------------------------------------------

    for (int i = 0; i < 24; i++) {
        size_t index =
            static_cast<size_t>(firstIndex + i);

        if (index >= times.size()) {
            break;
        }

        String stamp = times[index].as<String>();

        next.hourly[i].hour =
            stamp.length() >= 13
                ? stamp.substring(11, 13).toInt()
                : i;

        next.hourly[i].temp =
            temps[index].as<int>();

        next.hourly[i].weatherCode =
            codes[index].as<int>();

    }

    if (static_cast<size_t>(firstIndex) < windSpeeds.size()) next.windSpeed10m = windSpeeds[firstIndex].as<float>();
    if (static_cast<size_t>(firstIndex) < pressures.size()) next.surfacePressure = pressures[firstIndex].as<float>();
    if (static_cast<size_t>(firstIndex) < rainValues.size()) next.rain = rainValues[firstIndex].as<float>();

    if (sunshine.size() > 0) next.sunshineDuration = sunshine[0].as<float>();
    if (daylight.size() > 0) next.daylightDuration = daylight[0].as<float>();
    if (sunrise.size() > 0) next.sunrise = sunrise[0].as<String>().substring(11, 16);
    if (sunset.size() > 0) next.sunset = sunset[0].as<String>().substring(11, 16);

    // The Open-Meteo detail fields above are already validated and usable.
    // Lunar data is optional; its endpoint must not block the expanded panel.
    next.detailsValid = true;
    }

    Serial.printf(
        "[MEM] Before Lunar TLS: free=%u, largest_block=%u, min_free=%u bytes\n",
        (unsigned int)ESP.getFreeHeap(),
        (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned int)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)
    );

    // SunriseSunset.io resolves today's date from the supplied coordinates.
    // This avoids making lunar data depend on a successful NTP sync.
    String lunarUrl = String("https://api.sunrisesunset.io/json?lat=") +
        weatherLatitude + "&lng=" + weatherLongitude +
        "&timezone=Asia%2FHo_Chi_Minh&time_format=24";

    WiFiClientSecure lunarClient;
    lunarClient.setInsecure();

    // The default TLS record buffers are relatively large for this optional
    // request. Smaller buffers reduce peak internal-RAM demand on ESP32-C3.
    lunarClient.setBufferSizes(4096, 4096);

    HTTPClient lunarHttp;
    lunarHttp.setConnectTimeout(5000);
    lunarHttp.setTimeout(5000);

    if (lunarHttp.begin(lunarClient, lunarUrl)) {
        int lunarStatus = lunarHttp.GET();
        Serial.printf("[WEATHER] Lunar API HTTP status: %d\n", lunarStatus);

        if (lunarStatus == HTTP_CODE_OK) {
            String lunarPayload = lunarHttp.getString();
            JsonDocument lunarDoc;
            DeserializationError lunarError =
                deserializeJson(lunarDoc, lunarPayload);

            if (lunarError) {
                Serial.printf("[WEATHER] Lunar JSON error: %s\n", lunarError.c_str());
            } else if (!lunarDoc["results"].is<JsonObjectConst>()) {
                Serial.printf("[WEATHER] Lunar API response has no results object: %s\n",
                              lunarPayload.substring(0, 180).c_str());
            } else {
                JsonObjectConst result = lunarDoc["results"];
                next.moonrise = result["moonrise"].isNull()
                    ? "No rise" : result["moonrise"].as<String>();
                next.moonset = result["moonset"].isNull()
                    ? "No set" : result["moonset"].as<String>();
                next.moonPhase = result["moon_phase"] | "N/A";

                // Prefer the API's local sunrise/sunset values for the same date.
                if (!result["sunrise"].isNull()) next.sunrise = result["sunrise"].as<String>();
                if (!result["sunset"].isNull()) next.sunset = result["sunset"].as<String>();

                Serial.printf("[WEATHER] Lunar data: moonrise=%s, moonset=%s, phase=%s\n",
                              next.moonrise.c_str(), next.moonset.c_str(),
                              next.moonPhase.c_str());
            }
        } else {
            Serial.printf("[WEATHER] Lunar API request failed: %s\n",
                          lunarHttp.errorToString(lunarStatus).c_str());
        }
        lunarHttp.end();
    } else {
        Serial.println("[WEATHER] Lunar API HTTP begin failed.");
    }

    // -------------------------------------------------
    // COMMIT DATA
    // -------------------------------------------------

    SemaphoreHandle_t mutex = getWeatherMutex();

    if (mutex == nullptr) {
        Serial.println(
            "[WEATHER] Mutex allocation failed."
        );
        return false;
    }

    if (
        xSemaphoreTake(
            mutex,
            pdMS_TO_TICKS(1000)
        ) == pdTRUE
    ) {
        next.valid = true;
        currentWeather = next;

        xSemaphoreGive(mutex);

        Serial.println(
            "[WEATHER] Forecast updated successfully."
        );
        saveWeatherCache(next);
        return true;
    } else {
        Serial.println(
            "[WEATHER] Mutex timeout; update skipped."
        );
        return false;
    }
}

// =====================================================
// SAFE SNAPSHOT
// =====================================================

WeatherData getWeatherSnapshot() {
    WeatherData snapshot = {};

    SemaphoreHandle_t mutex = getWeatherMutex();

    if (mutex == nullptr) {
        Serial.println(
            "[WEATHER] Snapshot mutex allocation failed."
        );
        return snapshot;
    }

    if (
        xSemaphoreTake(
            mutex,
            pdMS_TO_TICKS(100)
        ) == pdTRUE
    ) {
        snapshot = currentWeather;

        xSemaphoreGive(mutex);
    } else {
        Serial.println(
            "[WEATHER] Snapshot mutex timeout."
        );
    }

    return snapshot;
}
