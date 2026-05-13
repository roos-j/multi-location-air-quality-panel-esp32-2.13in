#include <Arduino.h>

char portal_ssid[PORTAL_SSID_LEN] = "ESP32 PORTAL";
char portal_pwd[PORTAL_PWD_LEN] = "test1234";
char ssid[WIFI_SSID_LEN] = "YOUR_WIFI_SSID";
char pwd[WIFI_PWD_LEN] = "YOUR_WIFI_PASSWORD";
char purpleAirApiKey[PURPLEAIR_API_KEY_LEN] = "YOUR_PURPLEAIR_API_KEY";
char purpleAirTargetField[PURPLEAIR_TARGET_FIELD_LEN] = "pm2.5_30minute";

uint32_t sleepSeconds = 60 * 30;
uint32_t pm25ReadInterval = 60 * 30;
uint8_t locationCount = 3;

WeatherLocation locations[MAX_LOCATIONS] = {
  {
    .name = "NYC",
    .latitude = 40.712772,
    .longitude = -74.006058,
    .purpleAirSensorIndex = 0,
    .purpleAirReadKey = "",
    .temperature = 0,
    .weathercode = 0,
    .isDay = 1,
    .humidity = 0
  },
  {
    .name = "BER",
    .latitude = 52.520008,
    .longitude = 13.404954,
    .purpleAirSensorIndex = 0,
    .purpleAirReadKey = "",
    .temperature = 0,
    .weathercode = 0,
    .isDay = 1,
    .humidity = 0
  },
  {
    .name = "DEL",
    .latitude = 28.704100,
    .longitude = 77.102500,
    .purpleAirSensorIndex = 0,
    .purpleAirReadKey = "",
    .temperature = 0,
    .weathercode = 0,
    .isDay = 1,
    .humidity = 0
  }
};
