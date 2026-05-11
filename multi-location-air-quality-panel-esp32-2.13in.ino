#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_sleep.h>
#include <string.h>
#include <Preferences.h>

#include "weather_icons.h"
#include "ssd1680.h"

struct WeatherLocation {
  const char *name;
  float latitude;
  float longitude;
  uint32_t purpleAirSensorIndex;
  const char *purpleAirReadKey;
  uint32_t purpleAirLastRead; // Time stamp of last read attempt
  bool purpleAirLastReadDirty; // Whether last read has changed this boot cycle

  float temperature;
  uint8_t weathercode;  // Sunny/rainy/etc.
  uint8_t humidity;
  float pm2_5;
};

#if defined(__has_include) && __has_include("mypreset.h")
#include "mypreset.h"
#else
#include "preset.h"
#endif

// Pin wiring for menu and exit buttons
#define MENU_KEY GPIO_NUM_2
#define EXIT_KEY GPIO_NUM_1

#define PAD 2 /* Gap in pixels between lines */
#define FONTSIZE 24

#define SLEEP_SECONDS 60*30    /* Sleep time between boots in seconds */
#define PURPLEAIR_READ_INTERVAL 59*30  /* Interval between purple air reads in seconds */

/* TODO:
- if data not available write n/a
- restrict to recently seen PurpleAir sensors
- store preferences in filesystem
- store weather data time series and display 24h max etc.
- WiFi config page
*/

/** Current time */
tm timeinfo;
bool timeValid = false;

Preferences prefs;
uint32_t bootCount;

void setup() {
  Serial0.begin(115200);
  
  printWakeReason();

  readPreferences();

  connectWiFi();
  syncTime();

  displayInit();

  fetchAllWeatherInfo();
  fetchAllAirQualityInfo();

  displayInfo();

  writePreferences();
  goToDeepSleep(SLEEP_SECONDS);
}

/** Read preferences from flash memory */
void readPreferences() {
  prefs.begin("state", true);
  bootCount = prefs.getUInt("bootCount", 0);
  Serial0.printf("Boot count: %u\n", bootCount);
  char buf[32];
  for (uint8_t i = 0; i < N; i++) {
    snprintf(buf, sizeof(buf), "lastRead%u", i);
    locations[i].purpleAirLastRead = prefs.getUInt(buf, 0);
    snprintf(buf, sizeof(buf), "lastReadPM%u", i);
    locations[i].pm2_5 = prefs.getFloat(buf, 0.0);
    locations[i].purpleAirLastReadDirty = false;
    Serial0.print(locations[i].name);
    Serial0.print(" last PurpleAir read: ");
    printTimestamp(locations[i].purpleAirLastRead);
    Serial0.println();
  }
  prefs.end();
}

/** Write preferences to flash */
void writePreferences() {
  prefs.begin("state", false);

  prefs.putUInt("bootCount", bootCount + 1);
  char buf[32];
  for (uint8_t i = 0; i < N; i++) {
    if (locations[i].purpleAirLastReadDirty) {
      snprintf(buf, sizeof(buf), "lastRead%u", i);
      prefs.putUInt(buf, locations[i].purpleAirLastRead);
      snprintf(buf, sizeof(buf), "lastReadPM%u", i);
      prefs.putFloat(buf, locations[i].pm2_5);
    }
  }
  
  prefs.end();
}

/** Print unix timestamp to serial, formatted to current timezone */ 
void printTimestamp(uint32_t ts) {
  time_t raw = (time_t)ts;
  tm timeinfo;

  localtime_r(&raw, &timeinfo);  // fill timeinfo from timestamp as local time

  Serial0.print(&timeinfo, "%Y-%m-%d %H:%M:%S");
}

bool fetchWeatherInfo(WeatherLocation *info) {
  if (info == nullptr) {
    return false;
  }

  char url[256];

  snprintf(
    url,
    sizeof(url),
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=%f"
    "&longitude=%f"
    "&current=temperature_2m,relative_humidity_2m,weather_code"
    "&temperature_unit=celsius"
    "&forecast_days=1",
    info->latitude,
    info->longitude);

  HTTPClient http;
  http.begin(url);

  int status = http.GET();

  if (status != 200) {
    Serial0.print("Weather HTTP error: ");
    Serial0.println(status);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    Serial0.print("Weather JSON parse failed: ");
    Serial0.println(error.c_str());
    return false;
  }

  JsonObject current = doc["current"];

  if (!current.containsKey("temperature_2m") || !current.containsKey("relative_humidity_2m") || !current.containsKey("weather_code")) {
    Serial0.println("Weather fields missing");
    return false;
  }

  info->temperature = current["temperature_2m"].as<float>();
  info->humidity = current["relative_humidity_2m"].as<uint8_t>();
  info->weathercode = current["weather_code"].as<uint8_t>();

  return true;
}


void fetchAllWeatherInfo() {
  for (uint8_t i = 0; i < N; i++) {
    float tempF;

    bool ok = fetchWeatherInfo(&locations[i]);

    if (ok) {
      Serial0.print(locations[i].name);
      Serial0.print(": ");
      Serial0.print(locations[i].temperature);
      Serial0.printf(" C, %s, ", weatherCodeToDescription(locations[i].weathercode));
      Serial0.print(locations[i].humidity);
      Serial0.println("% RH");
    } else {
      Serial0.print(locations[i].name);
      Serial0.println(": fetch failed");
    }
  }
}

bool fetchAirQualityInfo(WeatherLocation *info) {
  if (info == nullptr) {
    return false;
  }

  if (info->purpleAirSensorIndex == 0) {
    return false;
  }

  uint32_t cur_time = (uint32_t)mktime(&timeinfo);
  if (cur_time - info->purpleAirLastRead < PURPLEAIR_READ_INTERVAL) {
    Serial0.print("PM2_5 value for ");
    Serial0.print(info->name);
    Serial0.println("still current, skipping read");
    return false;
  }
  info->purpleAirLastRead = cur_time;
  info->purpleAirLastReadDirty = true;

  char url[256];

  if (info->purpleAirReadKey != nullptr && info->purpleAirReadKey[0] != '\0') {
    snprintf(
      url,
      sizeof(url),
      "https://api.purpleair.com/v1/sensors/%u?fields=pm2.5_cf_1&read_key=%s",
      info->purpleAirSensorIndex,
      info->purpleAirReadKey);
  } else {
    snprintf(
      url,
      sizeof(url),
      "https://api.purpleair.com/v1/sensors/%u?fields=pm2.5_cf_1",
      info->purpleAirSensorIndex);
  }

  HTTPClient http;
  http.begin(url);
  http.addHeader("X-API-Key", purpleAirApiKey);

  int status = http.GET();

  if (status != 200) {
    Serial0.print("PurpleAir HTTP error: ");
    Serial0.println(status);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    Serial0.print("PurpleAir JSON parse failed: ");
    Serial0.println(error.c_str());
    return false;
  }

  JsonObject sensor = doc["sensor"];

  if (!sensor.containsKey("pm2.5_cf_1")) {
    Serial0.println("PurpleAir pm2.5_cf_1 missing");
    return false;
  }

  info->pm2_5 = sensor["pm2.5_cf_1"].as<float>();

  return true;
}

void fetchAllAirQualityInfo() {
  for (uint8_t i = 0; i < N; i++) {
    bool ok = fetchAirQualityInfo(&locations[i]);
    if (ok) {
      Serial0.print(locations[i].name);
      Serial0.print(": PM2.5 ");
      Serial0.print(locations[i].pm2_5, 1);
      Serial0.print(" ug/m3\n");
    } else {
      Serial0.print(locations[i].name);
      Serial0.println(": AQ fetch failed/skipped");
    }
  }
}

/** Connect to WiFi */
void connectWiFi() {
  WiFi.begin(ssid, pwd);
  Serial0.printf("Connecting to WiFi network '%s'", ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial0.print(".");
  }
  Serial0.println("\nWiFi connected");
}

#include <time.h>

/** Fetch current time using WiFi connection */
void syncTime() {
  configTzTime(
    "EST5EDT,M3.2.0/2,M11.1.0/2",
    "pool.ntp.org",
    "time.nist.gov");

  tm newTime;
  if (!getLocalTime(&newTime)) {
    Serial0.println("Failed to get time");
    return;
  }
  timeinfo = newTime;
  timeValid = true;
  Serial0.println(&timeinfo, "Time: %Y-%m-%d %H:%M:%S");
}

/** Display weather and air quality data */
void displayInfo() {
  displayClear();
  uint32_t y = 3 * PAD;

  char linebuf[64];

  const uint16_t CHAR_W = 12;  // FONTSIZE 24 -> about 12 px wide
  const uint16_t nameX = 3 * PAD;
  const uint16_t iconX = nameX + 4 * CHAR_W + PAD + 1;  // 3-char name
  const uint16_t tempX = iconX + 24 + 2*PAD;
  const uint16_t humX = tempX + 4 * CHAR_W + PAD;  // enough for "-12 C"
  const uint16_t pmX = humX + 4 * CHAR_W + PAD;    // enough for "100%"

  // Header line
  displayDrawString(nameX, y, "loc", BLACK, 16);
  displayDrawString(tempX, y, "temp", BLACK, 16);
  displayDrawString(humX, y, "hum", BLACK, 16);
  displayDrawString(pmX, y, "pm2.5", BLACK, 16);

  y += 16 + PAD;

  for (uint8_t i = 0; i < N; i++) {
    displayDrawString(nameX, y, locations[i].name, BLACK, FONTSIZE);

    displayDrawBitmap(iconX, y, weatherCodeToBitmap(locations[i].weathercode), 24, 24, BLACK);

    snprintf(linebuf, sizeof(linebuf), "%.0f", locations[i].temperature);
    displayDrawString(tempX, y, linebuf, BLACK, FONTSIZE);
    displayDrawString(tempX + strlen(linebuf) * CHAR_W + 6, y + 7, "C", BLACK, 16);

    snprintf(linebuf, sizeof(linebuf), "%u%%", locations[i].humidity);
    displayDrawString(humX, y, linebuf, BLACK, FONTSIZE);

    snprintf(linebuf, sizeof(linebuf), "%.0f", locations[i].pm2_5);
    displayDrawString(pmX, y, linebuf, BLACK, FONTSIZE);

    y += FONTSIZE + PAD;
  }


  if (timeValid) {
    strftime(linebuf, sizeof(linebuf), "Last update: %Y-%m-%d %H:%M:%S", &timeinfo);
    displayDrawString(PAD, SCR_HEIGHT - 14, linebuf, BLACK, 12);
  } else {
    displayDrawString(PAD, SCR_HEIGHT - 14, "Last update time unavailable", BLACK, 12);
  }
  snprintf(linebuf, sizeof(linebuf), "boot #%u", bootCount);
  int bootX = SCR_WIDTH - PAD - strlen(linebuf) * 6;
  displayDrawString(bootX, SCR_HEIGHT - 14, linebuf, BLACK, 12);

  displayUpdate();
  displayDeepSleep();
  Serial0.println("AQ display refreshed");
}

/** Deep sleep */
void goToDeepSleep(uint64_t seconds) {
  Serial0.println("Going to deep sleep...");
  Serial0.flush();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();

  pinMode(MENU_KEY, INPUT);

  esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
  esp_sleep_enable_ext0_wakeup(MENU_KEY, 0);
  esp_deep_sleep_start();
}

void printWakeReason() {
  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();

  if (reason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial0.println("Wake reason: timer");
  } else if (reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial0.println("Wake reason: MENU");
  } else {
    Serial0.println("Wake reason: normal boot");
  }
}

void loop() { }
