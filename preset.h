#include <Arduino.h>

// Number of locations
constexpr uint8_t N = 3;

const char *ssid = "YOUR_WIFI_SSID";
const char *pwd = "YOUR_WIFI_PASSWORD";
const char *purpleAirApiKey = "YOUR_PURPLEAIR_API_KEY";

WeatherLocation locations[N] = {
  {
    .name = "NYC",
    .latitude = 40.712772,
    .longitude = -74.006058,
    .purpleAirSensorIndex = 0,
    .purpleAirReadKey = ""
  },
  {
    .name = "BER",
    .latitude = 52.520008,
    .longitude = 13.404954,
    .purpleAirSensorIndex = 0,
    .purpleAirReadKey = ""
  },
  {
    .name = "DEL",
    .latitude = 28.704100,
    .longitude = 77.102500,
    .purpleAirSensorIndex = 0,
    .purpleAirReadKey = ""
  }
};
