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
  char name[16];
  float latitude;
  float longitude;
  uint32_t purpleAirSensorIndex;
  char purpleAirReadKey[64];

  NvsProp<TimedValue> pm2_5{nullptr};
  NvsProp<TimedValue> pm2_5max{nullptr}; // Track daily maximum value
  float temperature;
  uint8_t weathercode;  // Sunny/rainy/etc.
  uint8_t humidity;
};

constexpr uint8_t MAX_LOCATIONS = 3;
constexpr size_t WIFI_SSID_LEN = 32;
constexpr size_t WIFI_PWD_LEN = 64;
constexpr size_t PORTAL_SSID_LEN = 32;
constexpr size_t PORTAL_PWD_LEN = 64;
constexpr size_t PURPLEAIR_API_KEY_LEN = 64;
constexpr size_t PURPLEAIR_TARGET_FIELD_LEN = 32;
constexpr size_t PURPLEAIR_READ_KEY_LEN = 64;

// Pin wiring for menu and exit buttons
#define MENU_KEY GPIO_NUM_2
#define EXIT_KEY GPIO_NUM_1

#define PAD 2 /* Gap in pixels between lines */
#define FONTSIZE 24

extern tm timeinfo;
extern bool timeValid;

extern Preferences prefs;
extern NvsProp<uint32_t> bootCount;

extern TsStore tsStore;

extern char portal_ssid[PORTAL_SSID_LEN];
extern char portal_pwd[PORTAL_PWD_LEN];
extern char ssid[WIFI_SSID_LEN];
extern char pwd[WIFI_PWD_LEN];
extern char purpleAirApiKey[PURPLEAIR_API_KEY_LEN];
extern char purpleAirTargetField[PURPLEAIR_TARGET_FIELD_LEN];
extern uint32_t sleepSeconds;
extern uint32_t pm25ReadInterval;
extern uint8_t locationCount;
extern WeatherLocation locations[MAX_LOCATIONS];

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
