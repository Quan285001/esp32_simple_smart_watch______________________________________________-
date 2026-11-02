#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "weather_service.h"

namespace {
WeatherData currentWeather;
SemaphoreHandle_t weatherMutex = nullptr;
bool fsReady = false;

SemaphoreHandle_t mutexHandle() {
    if (!weatherMutex) weatherMutex = xSemaphoreCreateMutex();
    return weatherMutex;
}
bool mountFs() {
    if (fsReady) return true;
    fsReady = LittleFS.begin(true);
    if (!fsReady) Serial.println("[CACHE] LittleFS mount failed");
    return fsReady;
}
String prefString(const char* key, const char* fallback) {
    Preferences p;
    if (!p.begin("watch", true)) return String(fallback);
    String v = p.getString(key, fallback);
    p.end();
    return v;
}
String weatherDescription(int c) {
    if (c == 0) return "Clear sky";
    if (c == 1) return "Mainly clear";
    if (c == 2) return "Partly cloudy";
    if (c == 3) return "Overcast";
    if (c == 45 || c == 48) return "Fog";
    if (c >= 51 && c <= 57) return "Drizzle";
    if (c >= 61 && c <= 67) return "Rain";
    if (c >= 71 && c <= 77) return "Snow";
    if (c >= 80 && c <= 82) return "Rain showers";
    if (c >= 85 && c <= 86) return "Snow showers";
    if (c >= 95 && c <= 99) return "Thunderstorm";
    return "Unknown";
}
String localTimePart(JsonVariantConst v) {
    if (v.isNull()) return "N/A";
    String s = v.as<String>();
    return s.length() >= 16 ? s.substring(11, 16) : s;
}
}

WeatherData getWeatherSnapshot() {
    WeatherData copy;
    SemaphoreHandle_t m = mutexHandle();
    if (m && xSemaphoreTake(m, pdMS_TO_TICKS(100)) == pdTRUE) {
        copy = currentWeather;
        xSemaphoreGive(m);
    }
    if (copy.valid && copy.lastUpdatedEpoch && time(nullptr) > 100000) {
        copy.stale = (uint32_t)time(nullptr) - copy.lastUpdatedEpoch > WEATHER_CACHE_MAX_AGE_SEC;
    }
    return copy;
}

bool loadWeatherCache() {
    if (!mountFs()) return false;
    File f = LittleFS.open("/weather.json", "r");
    if (!f) return false;
    JsonDocument d;
    DeserializationError e = deserializeJson(d, f);
    f.close();
    if (e || !d["valid"].as<bool>()) return false;

    WeatherData w;
    w.currentTemp = d["currentTemp"] | 0;
    w.apparentTemp = d["apparentTemp"] | w.currentTemp;
    w.maxTemp = d["maxTemp"] | 0;
    w.minTemp = d["minTemp"] | 0;
    w.weatherCode = d["weatherCode"] | 0;
    w.conditionText = d["conditionText"] | "Cached weather";
    w.humidity = d["humidity"] | 0.0f;
    w.windSpeed10m = d["windSpeed10m"] | 0.0f;
    w.windDirection10m = d["windDirection10m"] | 0.0f;
    w.windGusts10m = d["windGusts10m"] | 0.0f;
    w.uvIndex = d["uvIndex"] | 0.0f;
    w.cloudCover = d["cloudCover"] | 0;
    w.surfacePressure = d["surfacePressure"] | 0.0f;
    w.rain = d["rain"] | 0.0f;
    w.sunshineDuration = d["sunshineDuration"] | 0.0f;
    w.daylightDuration = d["daylightDuration"] | 0.0f;
    w.moonPhase = d["moonPhase"] | "N/A";
    w.moonrise = d["moonrise"] | "N/A";
    w.moonset = d["moonset"] | "N/A";
    w.sunrise = d["sunrise"] | "N/A";
    w.sunset = d["sunset"] | "N/A";
    w.locationName = d["locationName"] | "Saved location";
    w.lastUpdatedEpoch = d["lastUpdatedEpoch"] | 0U;
    w.detailsValid = d["detailsValid"] | false;
    JsonArrayConst hours = d["hourly"].as<JsonArrayConst>();
    for (int i = 0; i < 24; ++i) {
        if (i < (int)hours.size()) {
            JsonObjectConst h = hours[i];
            w.hourly[i].hour = h["hour"] | i;
            w.hourly[i].temp = h["temp"] | w.currentTemp;
            w.hourly[i].weatherCode = h["code"] | w.weatherCode;
            w.hourly[i].precipitationProbability = h["pop"] | 0;
            w.hourly[i].precipitation = h["rain"] | 0.0f;
            w.hourly[i].windSpeed = h["wind"] | 0.0f;
            w.hourly[i].windGusts = h["gust"] | 0.0f;
            w.hourly[i].uvIndex = h["uv"] | 0.0f;
            w.hourly[i].cloudCover = h["cloud"] | 0;
        }
    }
    w.valid = true;
    SemaphoreHandle_t m = mutexHandle();
    if (m && xSemaphoreTake(m, pdMS_TO_TICKS(500)) == pdTRUE) {
        currentWeather = w;
        xSemaphoreGive(m);
    }
    Serial.println("[CACHE] Weather cache loaded");
    return true;
}

bool saveWeatherCache() {
    if (!mountFs()) return false;
    WeatherData w = getWeatherSnapshot();
    if (!w.valid) return false;
    JsonDocument d;
    d["valid"] = true;
    d["currentTemp"] = w.currentTemp;
    d["apparentTemp"] = w.apparentTemp;
    d["maxTemp"] = w.maxTemp;
    d["minTemp"] = w.minTemp;
    d["weatherCode"] = w.weatherCode;
    d["conditionText"] = w.conditionText;
    d["humidity"] = w.humidity;
    d["windSpeed10m"] = w.windSpeed10m;
    d["windDirection10m"] = w.windDirection10m;
    d["windGusts10m"] = w.windGusts10m;
    d["uvIndex"] = w.uvIndex;
    d["cloudCover"] = w.cloudCover;
    d["surfacePressure"] = w.surfacePressure;
    d["rain"] = w.rain;
    d["sunshineDuration"] = w.sunshineDuration;
    d["daylightDuration"] = w.daylightDuration;
    d["moonPhase"] = w.moonPhase;
    d["moonrise"] = w.moonrise;
    d["moonset"] = w.moonset;
    d["sunrise"] = w.sunrise;
    d["sunset"] = w.sunset;
    d["locationName"] = w.locationName;
    d["lastUpdatedEpoch"] = w.lastUpdatedEpoch;
    d["detailsValid"] = w.detailsValid;
    JsonArray a = d["hourly"].to<JsonArray>();
    for (int i = 0; i < 24; ++i) {
        JsonObject h = a.add<JsonObject>();
        h["hour"] = w.hourly[i].hour;
        h["temp"] = w.hourly[i].temp;
        h["code"] = w.hourly[i].weatherCode;
        h["pop"] = w.hourly[i].precipitationProbability;
        h["rain"] = w.hourly[i].precipitation;
        h["wind"] = w.hourly[i].windSpeed;
        h["gust"] = w.hourly[i].windGusts;
        h["uv"] = w.hourly[i].uvIndex;
        h["cloud"] = w.hourly[i].cloudCover;
    }
    File f = LittleFS.open("/weather.tmp", "w");
    if (!f) return false;
    bool ok = serializeJson(d, f) > 0;
    f.close();
    if (!ok) { LittleFS.remove("/weather.tmp"); return false; }
    LittleFS.remove("/weather.json");
    return LittleFS.rename("/weather.tmp", "/weather.json");
}

void setWeatherLocation(float latitude, float longitude) {
    if (latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180) return;
    Preferences p;
    if (p.begin("watch", false)) {
        p.putFloat("latitude", latitude);
        p.putFloat("longitude", longitude);
        p.putString("locationName", "Phone location");
        p.end();
    }
}

void fetchHourlyWeather() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WEATHER] Offline; cache retained");
        return;
    }
    Preferences p;
    float lat = 10.8231f, lon = 106.6297f;
    if (p.begin("watch", true)) {
        lat = p.getFloat("latitude", lat);
        lon = p.getFloat("longitude", lon);
        p.end();
    }
    String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + String(lat, 5) +
        "&longitude=" + String(lon, 5) +
        "&current=temperature_2m,apparent_temperature,relative_humidity_2m,is_day,precipitation,weather_code,cloud_cover,pressure_msl,surface_pressure,wind_speed_10m,wind_direction_10m,wind_gusts_10m" +
        "&hourly=temperature_2m,weather_code,precipitation_probability,precipitation,wind_speed_10m,wind_gusts_10m,uv_index,cloud_cover" +
        "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset,sunshine_duration,daylight_duration,uv_index_max,precipitation_probability_max" +
        "&forecast_days=2&timezone=Asia%2FHo_Chi_Minh";
    WiFiClientSecure client;
    client.setInsecure(); // Prototype only; add CA validation for production.
    HTTPClient http;
    http.setConnectTimeout(7000);
    http.setTimeout(9000);
    if (!http.begin(client, url)) return;
    int status = http.GET();
    if (status != HTTP_CODE_OK) {
        Serial.printf("[WEATHER] HTTP error %d\n", status);
        http.end();
        return;
    }
    JsonDocument d;
    DeserializationError err = deserializeJson(d, http.getStream());
    http.end();
    if (err) {
        Serial.printf("[WEATHER] JSON parse error: %s\n", err.c_str());
        return;
    }
    JsonObjectConst c = d["current"];
    JsonObjectConst h = d["hourly"];
    JsonObjectConst day = d["daily"];
    JsonArrayConst times = h["time"];
    JsonArrayConst temps = h["temperature_2m"];
    JsonArrayConst codes = h["weather_code"];
    if (c["temperature_2m"].isNull() || times.size() == 0 || temps.size() != times.size() || codes.size() != times.size()) {
        Serial.println("[WEATHER] Invalid response; old cache retained");
        return;
    }
    WeatherData next = getWeatherSnapshot();
    next.currentTemp = c["temperature_2m"].as<int>();
    next.apparentTemp = c["apparent_temperature"] | next.currentTemp;
    next.weatherCode = c["weather_code"] | 0;
    next.conditionText = weatherDescription(next.weatherCode);
    next.humidity = c["relative_humidity_2m"] | 0.0f;
    next.windSpeed10m = c["wind_speed_10m"] | 0.0f;
    next.windDirection10m = c["wind_direction_10m"] | 0.0f;
    next.windGusts10m = c["wind_gusts_10m"] | 0.0f;
    next.uvIndex = day["uv_index_max"][0] | 0.0f;
    next.cloudCover = c["cloud_cover"] | 0;
    next.surfacePressure = c["surface_pressure"] | 0.0f;
    next.rain = c["precipitation"] | 0.0f;
    next.maxTemp = day["temperature_2m_max"][0] | next.currentTemp;
    next.minTemp = day["temperature_2m_min"][0] | next.currentTemp;
    next.sunrise = localTimePart(day["sunrise"][0]);
    next.sunset = localTimePart(day["sunset"][0]);
    next.sunshineDuration = day["sunshine_duration"][0] | 0.0f;
    next.daylightDuration = day["daylight_duration"][0] | 0.0f;
    next.locationName = prefString("locationName", "Ho Chi Minh City");
    int first = 0;
    struct tm nowTm = {};
    if (getLocalTime(&nowTm, 10)) {
        char prefix[15];
        snprintf(prefix, sizeof(prefix), "%04d-%02d-%02dT%02d",
            nowTm.tm_year + 1900, nowTm.tm_mon + 1, nowTm.tm_mday, nowTm.tm_hour);
        for (size_t i = 0; i < times.size(); ++i) {
            if (times[i].as<String>().startsWith(prefix)) { first = (int)i; break; }
        }
    }
    for (int i = 0; i < 24; ++i) {
        size_t k = (size_t)first + i;
        if (k >= times.size()) break;
        String stamp = times[k].as<String>();
        next.hourly[i].hour = stamp.length() >= 13 ? stamp.substring(11, 13).toInt() : i;
        next.hourly[i].temp = temps[k] | next.currentTemp;
        next.hourly[i].weatherCode = codes[k] | next.weatherCode;
        next.hourly[i].precipitationProbability = h["precipitation_probability"][k] | 0;
        next.hourly[i].precipitation = h["precipitation"][k] | 0.0f;
        next.hourly[i].windSpeed = h["wind_speed_10m"][k] | 0.0f;
        next.hourly[i].windGusts = h["wind_gusts_10m"][k] | 0.0f;
        next.hourly[i].uvIndex = h["uv_index"][k] | 0.0f;
        next.hourly[i].cloudCover = h["cloud_cover"][k] | 0;
    }
    next.lastUpdatedEpoch = (uint32_t)time(nullptr);
    next.valid = true;
    next.stale = false;
    next.detailsValid = true;

    // Lunar data is optional. Failure must never invalidate the weather forecast.
    struct tm dateTm = {};
    if (getLocalTime(&dateTm, 10)) {
        char date[11];
        snprintf(date, sizeof(date), "%04d-%02d-%02d", dateTm.tm_year + 1900, dateTm.tm_mon + 1, dateTm.tm_mday);
        String lunarUrl = String("https://api.sunrisesunset.io/json?lat=") + String(lat, 5) +
            "&lng=" + String(lon, 5) + "&date=" + date + "&timezone=Asia%2FHo_Chi_Minh&time_format=24";
        WiFiClientSecure lunarClient;
        lunarClient.setInsecure();
        HTTPClient lunar;
        lunar.setConnectTimeout(4000);
        lunar.setTimeout(4000);
        if (lunar.begin(lunarClient, lunarUrl) && lunar.GET() == HTTP_CODE_OK) {
            JsonDocument ld;
            if (!deserializeJson(ld, lunar.getStream()) && ld["results"].is<JsonObjectConst>()) {
                JsonObjectConst r = ld["results"];
                next.moonrise = r["moonrise"] | "N/A";
                next.moonset = r["moonset"] | "N/A";
                next.moonPhase = r["moon_phase"] | "N/A";
            }
            lunar.end();
        } else {
            lunar.end();
        }
    }

    SemaphoreHandle_t m = mutexHandle();
    if (m && xSemaphoreTake(m, pdMS_TO_TICKS(1000)) == pdTRUE) {
        currentWeather = next;
        xSemaphoreGive(m);
    }
    saveWeatherCache();
    Serial.println("[WEATHER] Forecast updated and cached");
}
