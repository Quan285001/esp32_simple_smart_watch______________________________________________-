#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "weather_service.h"

WeatherData currentWeather = {
    28, 32, 24, "Waiting for weather",
    {
        {0,28,2},{1,28,2},{2,27,2},{3,27,2},{4,27,2},{5,27,2},
        {6,28,1},{7,29,1},{8,30,1},{9,31,0},{10,32,0},{11,32,0},
        {12,33,0},{13,33,0},{14,32,1},{15,31,2},{16,30,3},{17,29,3},
        {18,28,3},{19,28,2},{20,27,2},{21,27,2},{22,27,2},{23,27,2}
    },
    false
};

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
    if (code >= 95) return "Thunderstorm";
    return "Unknown";
}

void fetchHourlyWeather() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WEATHER] Skip: Wi-Fi is offline; keeping cached data.");
        return;
    }

    String url = String("https://api.open-meteo.com/v1/forecast?latitude=") +
        WEATHER_LATITUDE + "&longitude=" + WEATHER_LONGITUDE +
        "&current=temperature_2m,weather_code" +
        "&hourly=temperature_2m,weather_code" +
        "&daily=temperature_2m_max,temperature_2m_min" +
        "&forecast_days=2&timezone=Asia%2FHo_Chi_Minh";

    WiFiClientSecure client;
    client.setInsecure(); // Open-Meteo dùng HTTPS; xác thực chứng chỉ sẽ bổ sung sau.
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);

    if (!http.begin(client, url)) {
        Serial.println("[WEATHER] HTTP begin failed.");
        return;
    }

    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
        Serial.printf("[WEATHER] HTTP error: %d\n", status);
        http.end();
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, http.getStream());
    http.end();
    if (error) {
        Serial.printf("[WEATHER] JSON error: %s\n", error.c_str());
        return;
    }

    JsonVariantConst currentTemp = doc["current"]["temperature_2m"];
    JsonVariantConst currentCode = doc["current"]["weather_code"];
    JsonArrayConst times = doc["hourly"]["time"];
    JsonArrayConst temps = doc["hourly"]["temperature_2m"];
    JsonArrayConst codes = doc["hourly"]["weather_code"];
    JsonArrayConst dailyMax = doc["daily"]["temperature_2m_max"];
    JsonArrayConst dailyMin = doc["daily"]["temperature_2m_min"];

    if (currentTemp.isNull() || currentCode.isNull() || times.size() == 0 ||
        temps.size() != times.size() || codes.size() != times.size() ||
        dailyMax.size() == 0 || dailyMin.size() == 0) {
        Serial.println("[WEATHER] Response incomplete; keeping cached data.");
        return;
    }

    // Build a temporary result first: invalid responses never overwrite good data.
    WeatherData next = currentWeather;
    next.currentTemp = currentTemp.as<int>();
    next.maxTemp = dailyMax[0].as<int>();
    next.minTemp = dailyMin[0].as<int>();
    next.conditionText = weatherDescription(currentCode.as<int>());

    int firstIndex = -1;
    struct tm now;
    if (getLocalTime(&now, 10)) {
        for (size_t i = 0; i < times.size(); ++i) {
            String stamp = times[i].as<String>();
            if (stamp.length() >= 13 && stamp.substring(11, 13).toInt() == now.tm_hour) {
                firstIndex = static_cast<int>(i);
                break;
            }
        }
    }
    if (firstIndex < 0) firstIndex = 0;

    for (int i = 0; i < 24; ++i) {
        const size_t index = static_cast<size_t>((firstIndex + i) % times.size());
        String stamp = times[index].as<String>();
        next.hourly[i].hour = stamp.length() >= 13 ? stamp.substring(11, 13).toInt() : i;
        next.hourly[i].temp = temps[index].as<int>();
        next.hourly[i].weatherCode = codes[index].as<int>();
    }

    next.valid = true;
    currentWeather = next;
    Serial.println("[WEATHER] TP.HCM forecast updated.");
}
