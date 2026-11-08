#include "weather_coordinator.h"
#include <WiFi.h>
#include "config.h"
#include "../app/app_state.h"
#include "weather_service.h"
#include "ble_service.h"
namespace WeatherCoordinator {
namespace {
constexpr uint32_t RETRY_MS=15000UL, MANUAL_COOLDOWN_MS=10000UL;
volatile bool running=false,faceDirty=false,locationPending=false;
volatile int8_t notifyPending=-1;
uint32_t lastSuccess=0,lastAttempt=0,lastRetry=0,lastManual=0,lastLocationRetry=0;
void fetchTask(void*) {
 Serial.println("[WEATHER] Background fetch started.");
 bool ok=fetchHourlyWeather();
 if(ok){lastSuccess=millis();weatherDataValid=true;faceDirty=true;}
 notifyPending=ok?1:0;
 Serial.println(ok?"[WEATHER] Weather stage finished.":"[WEATHER] Weather stage failed.");
 if(ok&&wifiEnabled&&WiFi.status()==WL_CONNECTED){vTaskDelay(pdMS_TO_TICKS(5000));Serial.println("[WEATHER] Starting deferred lunar stage.");fetchLunarDetails();}
 running=false;vTaskDelete(nullptr);
}
bool schedule(bool enabled,bool connected,bool scan,bool boot,bool transfer){
 if(!enabled){Serial.println("[WEATHER] Fetch skipped: Wi-Fi is OFF.");return false;}
 if(!connected||running||scan||boot||transfer)return false;
 running=true;
 if(xTaskCreate(fetchTask,"weatherFetch",8192,nullptr,1,nullptr)!=pdPASS){running=false;Serial.println("[WEATHER] Could not create background task.");return false;}
 lastAttempt=millis();Serial.println("[WEATHER] Fetch scheduled.");return true;
}
}
void begin(){running=false;faceDirty=false;locationPending=false;notifyPending=-1;lastSuccess=lastAttempt=lastRetry=lastManual=lastLocationRetry=0;}
bool isFetchRunning(){return running;}
bool canStartTransfer(bool scan,bool boot){return !running&&!scan&&!boot;}
int32_t refreshCountdownSeconds(bool enabled,bool connected){
 if(!enabled)return -1;if(!connected)return -2;if(running)return -3;
 uint32_t elapsed=millis()-lastSuccess;if(elapsed>=WEATHER_REFRESH_MS)return 0;
 return static_cast<int32_t>((WEATHER_REFRESH_MS-elapsed+999UL)/1000UL);
}
uint8_t requestManual(bool enabled,bool connected,bool scan,bool boot,bool transfer){
 if(!enabled||!connected)return 2;if(running||scan||boot||transfer)return 1;
 uint32_t now=millis();if(lastManual&&now-lastManual<MANUAL_COOLDOWN_MS)return 3;
 if(!schedule(enabled,connected,scan,boot,transfer))return running?1:2;lastManual=now;return 0;
}
uint8_t requestForLocation(bool enabled,bool connected,bool scan,bool boot,bool transfer){
 locationPending=true;lastLocationRetry=0;if(!enabled||!connected)return 2;
 if(running||scan||transfer)return 1;
 if(schedule(enabled,connected,scan,boot,transfer)){locationPending=false;return 0;}return 2;
}
void service(uint32_t now,bool enabled,bool connected,bool hasData,bool scan,bool boot,bool transfer){
 if(locationPending&&enabled&&connected&&!running&&!scan&&!boot&&!transfer&&(lastLocationRetry==0||now-lastLocationRetry>=RETRY_MS)){
  lastLocationRetry=now;if(schedule(enabled,connected,scan,boot,transfer)){locationPending=false;return;}
 }
 if(!enabled||!connected||boot||running||scan||transfer)return;
 if(!hasData){if(lastRetry==0||now-lastRetry>=RETRY_MS)if(schedule(enabled,connected,scan,boot,transfer)){lastRetry=now;Serial.println("[WEATHER] Retrying weather fetch...");}return;}
 if(now-lastSuccess>=WEATHER_REFRESH_MS&&(lastAttempt==0||now-lastAttempt>=RETRY_MS))schedule(enabled,connected,scan,boot,transfer);
}
void serviceNotification(){int8_t result=notifyPending;if(result<0)return;notifyPending=-1;sendBLENotification(result?"WEATHER_OK":"WEATHER_FAILED");}
bool takeFaceInvalidation(){bool value=faceDirty;faceDirty=false;return value;}
void onWifiConnected(uint32_t now){lastSuccess=now-WEATHER_REFRESH_MS;lastAttempt=lastRetry=0;}
void onWifiEnabled(){lastSuccess=lastAttempt=lastRetry=0;}
void beginBootFetch(){running=true;}
void finishBootFetch(bool ok){if(ok){lastSuccess=millis();weatherDataValid=true;faceDirty=true;}running=false;}
}
