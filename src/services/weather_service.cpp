
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "weather_service.h"

// =====================================================
// SHARED WEATHER DATA
// =====================================================

WeatherData currentWeather = {
    28,
    32,
    24,
    "Waiting for weather",
    {
        {0,28,2},{1,28,2},{2,27,2},{3,27,2},
        {4,27,2},{5,28,2},{6,28,1},{7,29,1},
        {8,30,1},{9,31,1},{10,32,0},{11,32,0},
        {12,33,0},{13,33,0},{14,32,1},{15,31,2},
        {16,30,3},{17,29,3},{18,28,3},{19,28,2},
        {20,27,2},{21,27,2},{22,27,2},{23,27,2}
    },
    false
};

static SemaphoreHandle_t getWeatherMutex() {
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    return mutex;
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

void fetchHourlyWeather() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(
            "[WEATHER] Wi-Fi offline; keeping cached data."
        );
        return;
    }

    String url =
        String("https://api.open-meteo.com/v1/forecast?latitude=") +
        WEATHER_LATITUDE +
        "&longitude=" + WEATHER_LONGITUDE +
        "&current=temperature_2m,weather_code" +
        "&hourly=temperature_2m,weather_code,wind_speed_10m,surface_pressure,precipitation" +
        "&daily=temperature_2m_max,temperature_2m_min,sunshine_duration,daylight_duration,sunrise,sunset" +
        "&forecast_days=2&timezone=Asia%2FHo_Chi_Minh";

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    http.setConnectTimeout(5000);
    http.setTimeout(5000);

    if (!http.begin(client, url)) {
        Serial.println("[WEATHER] HTTP begin failed.");
        return;
    }

    int status = http.GET();

    if (status != HTTP_CODE_OK) {
        Serial.printf(
            "[WEATHER] HTTP error: %d\n",
            status
        );

        http.end();
        return;
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
        return;
    }

    Serial.println("[WEATHER] Response preview:");

    Serial.println(payload.substring(0, 300));

    JsonDocument doc;

    DeserializationError error =
        deserializeJson(doc, payload);

    http.end();

    if (error) {
        Serial.printf("[WEATHER] JSON error: %s\n",error.c_str()
    );
    return;
}

    if (error) {
        Serial.printf(
            "[WEATHER] JSON error: %s\n",
            error.c_str()
        );
        return;
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
        times.size() == 0 ||
        temps.size() != times.size() ||
        codes.size() != times.size() ||
        dailyMax.size() == 0 ||
        dailyMin.size() == 0
    ) {
        Serial.println(
            "[WEATHER] Incomplete response; keeping cache."
        );
        return;
    }

    // -------------------------------------------------
    // PREPARE NEW DATA
    // -------------------------------------------------

    WeatherData next = getWeatherSnapshot();

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

    // Fetch lunar and local sunrise/sunset data. This is cached with the
    // same 30-minute weather refresh, not requested by the display loop.
    struct tm dateInfo = {};
    if (getLocalTime(&dateInfo, 10)) {
        char dateText[11];
        snprintf(dateText, sizeof(dateText), "%04d-%02d-%02d",
                 dateInfo.tm_year + 1900, dateInfo.tm_mon + 1, dateInfo.tm_mday);

        String lunarUrl = String("https://api.sunrisesunset.io/json?lat=") +
            WEATHER_LATITUDE + "&lng=" + WEATHER_LONGITUDE +
            "&date=" + dateText +
            "&timezone=Asia%2FHo_Chi_Minh&time_format=24";

        WiFiClientSecure lunarClient;
        lunarClient.setInsecure();
        HTTPClient lunarHttp;
        lunarHttp.setConnectTimeout(4000);
        lunarHttp.setTimeout(4000);

        if (lunarHttp.begin(lunarClient, lunarUrl)) {
            int lunarStatus = lunarHttp.GET();
            if (lunarStatus == HTTP_CODE_OK) {
                JsonDocument lunarDoc;
                DeserializationError lunarError =
                    deserializeJson(lunarDoc, lunarHttp.getString());
                if (!lunarError && lunarDoc["results"].is<JsonObjectConst>()) {
                    JsonObjectConst result = lunarDoc["results"];
                    next.moonrise = result["moonrise"].isNull() ? "N/A" : result["moonrise"].as<String>();
                    next.moonset = result["moonset"].isNull() ? "N/A" : result["moonset"].as<String>();
                    next.moonPhase = result["moon_phase"] | "N/A";
                    next.sunrise = result["sunrise"].isNull() ? "N/A" : result["sunrise"].as<String>();
                    next.sunset = result["sunset"].isNull() ? "N/A" : result["sunset"].as<String>();
                    next.detailsValid = true;
                }
            }
            lunarHttp.end();
        }
    }

    // -------------------------------------------------
    // COMMIT DATA
    // -------------------------------------------------

    SemaphoreHandle_t mutex = getWeatherMutex();

    if (mutex == nullptr) {
        Serial.println(
            "[WEATHER] Mutex allocation failed."
        );
        return;
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
    } else {
        Serial.println(
            "[WEATHER] Mutex timeout; update skipped."
        );
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
