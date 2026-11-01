ESP32 Smart Watch - Weather face patch

Files in this patch:
- src/face/face_weather.cpp
- src/app/app_controller.cpp

Install:
1. Extract the original project repository.
2. Copy the src folder from this ZIP into the project folder and overwrite when asked.
3. Open the project folder containing platformio.ini in PlatformIO.
4. Build before uploading to the ESP32-C3.

Behavior:
- Short press on Clock opens Weather.
- Short press on Weather advances the selected forecast by one hour.
- The large temperature/icon and the four hourly cards follow the selected hour.
- Forecast cards wrap around after hourly[23].
- Long press behavior for opening/using the existing menu is retained.

Important:
- This patch expects the current project data layout in src/config.h:
  WeatherData.hourly[24] with hour/temp/weatherCode.
- weather_service.cpp currently rotates hourly[0] to the current local hour
  when it receives a valid API response. The UI therefore begins at hourly[0].
- The patch uses existing drawing helpers in src/icons.h.
- Not compiled or tested on physical hardware in this environment; build and
  verify in PlatformIO before flashing.
