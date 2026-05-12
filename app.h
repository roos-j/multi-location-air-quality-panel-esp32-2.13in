#ifndef APP_H

#define APP_H

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_sleep.h>
#include <string.h>
#include <Preferences.h>

#include "ssd1680.h"
#include "util.h"

struct WeatherLocation {
  const char *name;
  float latitude;
  float longitude;
  uint32_t purpleAirSensorIndex;
  const char *purpleAirReadKey;

  NvsProp<TimedValue> pm2_5{nullptr};
  NvsProp<TimedValue> pm2_5max{nullptr}; // Track daily maximum value
  float temperature;
  uint8_t weathercode;  // Sunny/rainy/etc.
  uint8_t humidity;
};

// Pin wiring for menu and exit buttons
#define MENU_KEY GPIO_NUM_2
#define EXIT_KEY GPIO_NUM_1

#define PAD 2 /* Gap in pixels between lines */
#define FONTSIZE 24

#define SLEEP_SECONDS 60*30    /* Sleep time between boots in seconds */
#define PURPLEAIR_READ_INTERVAL 60*30  /* Interval between purple air reads in seconds */

#define PORTAL_SSID   "ESP32 PORTAL"
#define PORTAL_PWD    "test1234"

extern tm timeinfo;
extern bool timeValid;

extern Preferences prefs;
extern NvsProp<uint32_t> bootCount;

extern TsStore tsStore;

enum class BootMode {
  Normal,
  Timer,
  Menu,
  Exit
};

extern BootMode bootMode;

void setupNormalMode();
void setupPortalMode();

#endif