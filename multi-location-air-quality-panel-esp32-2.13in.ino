#include "app.h"
#include "portal.h"
#include "weather_icons.h"

/* TODO:
- if data not available write n/a
- restrict to recently seen PurpleAir sensors
- night time weather icons
- wifi connectivity symbol, failure handling
- config page
*/

/** Current time */
tm timeinfo;
bool timeValid = false;

Preferences prefs;
NvsProp<uint32_t> bootCount{"bootCount"};

/** Time series storage */
TsStore tsStore;

BootMode bootMode;

#if defined(__has_include) && __has_include("mypreset.h")
#include "mypreset.h"
#else
#include "preset.h"
#endif

void setup() {
  Serial0.begin(115200);
  
  if (!tsStore.begin()) {
    Serial0.println("LittleFS failed");
    return;
  }

  bootMode = printWakeReason();

  if (bootMode == BootMode::Menu) {
    captureDefaultConfig();
  }
  readPreferences();

  connectWiFi(bootMode == BootMode::Menu);
  syncTime();

  displayInit();

  if (bootMode == BootMode::Menu) {
    setupPortalMode();
  } else {
    setupNormalMode();  
  }
}

/** Normal mode displays info, then goes to deep sleep */
void setupNormalMode() {
  Serial0.println("Starting normal mode...");

  fetchAllWeatherInfo();
  fetchAllAirQualityInfo();

  displayInfo();

  uint64_t bytes = tsStore.totalSize();
  Serial0.printf("Time series storage: %llu bytes\n", bytes);

  writePreferences();
  goToDeepSleep(sleepSeconds);
}


/** Read preferences from NVS */
void readPreferences() {
  loadConfigPreferences();
  loadStatePreferences();
}

char pm2_5Keys[MAX_LOCATIONS][16];
char pm2_5MaxKeys[MAX_LOCATIONS][16];

void initLocationStateKeys() {
  for (uint8_t i = 0; i < MAX_LOCATIONS; i++) {
    snprintf(pm2_5Keys[i], sizeof(pm2_5Keys[i]), "%s_pm25", locations[i].name);
    snprintf(pm2_5MaxKeys[i], sizeof(pm2_5MaxKeys[i]), "%s_pm25max", locations[i].name);

    locations[i].pm2_5 = NvsProp<TimedValue>(pm2_5Keys[i]);
    locations[i].pm2_5max = NvsProp<TimedValue>(pm2_5MaxKeys[i]);
  }
}

uint8_t visibleLocationSlots() {
  const uint16_t headerY = 3 * PAD;
  const uint16_t rowStartY = headerY + 16 + PAD;
  const uint16_t footerReserve = 14 + PAD;
  const uint16_t rowStep = FONTSIZE + PAD;

  if (SCR_HEIGHT <= rowStartY + footerReserve) {
    return 0;
  }

  uint16_t available = SCR_HEIGHT - rowStartY - footerReserve;
  uint8_t rows = available / rowStep;

  return rows;
}

bool loadConfigPreferences() {
  if (!prefs.begin("config", true)) {
    Serial0.println("Config NVS open failed");
    return false;
  }

  prefs.getString("p_ssid", portal_ssid, sizeof(portal_ssid));
  prefs.getString("p_pwd", portal_pwd, sizeof(portal_pwd));
  prefs.getString("w_ssid", ssid, sizeof(ssid));
  prefs.getString("w_pwd", pwd, sizeof(pwd));
  prefs.getString("pa_key", purpleAirApiKey, sizeof(purpleAirApiKey));
  prefs.getString("pa_field", purpleAirTargetField, sizeof(purpleAirTargetField));

  sleepSeconds = prefs.getUInt("sleep_s", sleepSeconds);
  if (sleepSeconds == 0) {
    sleepSeconds = 1;
  }

  pm25ReadInterval = prefs.getUInt("pm25int", pm25ReadInterval);
  if (pm25ReadInterval == 0) {
    pm25ReadInterval = 1;
  }

  uint32_t count = prefs.getUInt("loc_cnt", locationCount);
  if (count < 1) {
    count = 1;
  } else if (count > MAX_LOCATIONS) {
    count = MAX_LOCATIONS;
  }
  locationCount = static_cast<uint8_t>(count);

  for (uint8_t i = 0; i < MAX_LOCATIONS; i++) {
    char key[16];

    snprintf(key, sizeof(key), "l%u_name", i);
    prefs.getString(key, locations[i].name, sizeof(locations[i].name));

    snprintf(key, sizeof(key), "l%u_lat", i);
    locations[i].latitude = prefs.getFloat(key, locations[i].latitude);

    snprintf(key, sizeof(key), "l%u_lon", i);
    locations[i].longitude = prefs.getFloat(key, locations[i].longitude);

    snprintf(key, sizeof(key), "l%u_idx", i);
    locations[i].purpleAirSensorIndex = prefs.getUInt(key, locations[i].purpleAirSensorIndex);

    snprintf(key, sizeof(key), "l%u_key", i);
    prefs.getString(key, locations[i].purpleAirReadKey, sizeof(locations[i].purpleAirReadKey));
  }

  prefs.end();
  return true;
}

void loadStatePreferences() {
  if (!prefs.begin("state", true)) {
    Serial0.println("State NVS open failed");
    return;
  }

  bootCount.load(prefs, 0);
  Serial0.printf("Boot count: %lu\n", static_cast<unsigned long>(bootCount.get()));

  initLocationStateKeys();

  for (uint8_t i = 0; i < MAX_LOCATIONS; i++) {
    locations[i].pm2_5.load(prefs, TimedValue{});
    Serial0.print(locations[i].name);
    Serial0.print(" last PurpleAir read PM2.5: ");
    printTimestamp(locations[i].pm2_5.get().time);
    Serial0.println();

    locations[i].pm2_5max.load(prefs, TimedValue{});
    Serial0.print(locations[i].name);
    Serial0.printf(" daily max value %.f at ", locations[i].pm2_5max.get().value);
    printTimestamp(locations[i].pm2_5max.get().time);
    Serial0.println();
  }

  prefs.end();
}

/** Write preferences to NVS */
void writePreferences() {
  if (!prefs.begin("state", false)) {
    Serial0.println("State NVS open failed");
    return;
  }

  bootCount.set(bootCount.get() + 1);
  bootCount.save(prefs);
  for (uint8_t i = 0; i < MAX_LOCATIONS; i++) {
    locations[i].pm2_5.save(prefs);
    locations[i].pm2_5max.save(prefs);
  }
  
  prefs.end();
}

/** Print unix timestamp to serial, formatted to current timezone */ 
void printTimestamp(uint32_t ts) {
  char buf[24];

  if (formatLocalTimestamp(ts, buf, sizeof(buf))) {
    Serial0.print(buf);
  } else {
    Serial0.print("n/a");
  }
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
  for (uint8_t i = 0; i < locationCount; i++) {
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

void updateDailyMax(NvsProp<TimedValue> &dailyMax, const TimedValue &reading) {
  if (!reading.hasValue()) {
    return;
  }

  TimedValue current = dailyMax.get();

  bool sameDay = false;

  if (current.hasValue()) {
    time_t currentRaw = (time_t)current.time;
    time_t readingRaw = (time_t)reading.time;

    struct tm currentTm;
    struct tm readingTm;

    localtime_r(&currentRaw, &currentTm);
    localtime_r(&readingRaw, &readingTm);

    sameDay =
      currentTm.tm_year == readingTm.tm_year &&
      currentTm.tm_yday == readingTm.tm_yday;
  }

  if (!sameDay || reading.value > current.value) {
    dailyMax.set(reading);
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
  TimedValue pm2_5 = info->pm2_5.get();
  if (cur_time - pm2_5.time < pm25ReadInterval) {
    Serial0.print("PM2_5 value for ");
    Serial0.print(info->name);
    Serial0.printf(" still current, skipping read (diff=%d)\n", cur_time-pm2_5.time);
    return false;
  }

  char url[256];

  if (info->purpleAirReadKey[0] != '\0') {
    snprintf(
      url,
      sizeof(url),
      "https://api.purpleair.com/v1/sensors/%u?fields=%s&read_key=%s",
      info->purpleAirSensorIndex,
      purpleAirTargetField,
      info->purpleAirReadKey);
  } else {
    snprintf(
      url,
      sizeof(url),
      "https://api.purpleair.com/v1/sensors/%u?fields=%s",
      info->purpleAirSensorIndex,
      purpleAirTargetField);
  }

  Serial0.printf("Connecting to URL '%s'\n", url);

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


  JsonVariant valueNode = doc["sensor"][purpleAirTargetField];

  if (valueNode.isNull()) {
    valueNode = doc["sensor"]["stats"][purpleAirTargetField];
  }
  if (valueNode.isNull()) {
    Serial0.printf("Missing/invalid PurpleAir field: '%s'\n", purpleAirTargetField);
    return false;
  }

  float value = valueNode | NAN;

  uint32_t readingTime = doc["data_time_stamp"] | 0;

  if (readingTime == 0) {
    Serial0.printf("PurpleAir response missing time stamp, defaulting to current time\n");
    readingTime = cur_time;
  }

  TimedValue newValue{readingTime, value};

  updateDailyMax(info->pm2_5max, newValue);

  info->pm2_5.set(newValue);
  tsStore.append(info->pm2_5.getKey(), pm2_5);

  return true;
}

void fetchAllAirQualityInfo() {
  for (uint8_t i = 0; i < locationCount; i++) {
    bool ok = fetchAirQualityInfo(&locations[i]);
    if (ok) {
      Serial0.print(locations[i].name);
      Serial0.print(": PM2.5 ");
      Serial0.print(locations[i].pm2_5.get().value, 1);
      Serial0.print(" ug/m3\n");
    } else {
      Serial0.print(locations[i].name);
      Serial0.println(": AQ fetch failed/skipped");
    }
  }
}

/** Connect to WiFi */
void connectWiFi(bool access_point) {
  if (access_point) {
    WiFi.mode(WIFI_AP_STA);

    WiFi.softAP(portal_ssid, portal_pwd);

    Serial0.printf("Access point '%s' open\n", portal_ssid);
    Serial0.print("AP IP: ");
    Serial0.println(WiFi.softAPIP());
  } else {
    WiFi.mode(WIFI_STA);
  }

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  WiFi.begin(ssid, pwd);

  Serial0.printf("Connecting to WiFi network '%s'", ssid);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial0.print(".");
  }

  Serial0.println("\nWiFi connected");

  Serial0.print("STA IP: ");
  Serial0.println(WiFi.localIP());
}

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

  const uint16_t CHAR_W = 12;  // FONTSIZE 24 -> 12 px wide
  const uint16_t nameX = 3 * PAD;
  const uint16_t iconX = nameX + 4 * CHAR_W + PAD + 1;  // 3-char name
  const uint16_t tempX = iconX + 24 + 2*PAD;
  const uint16_t humX = tempX + 4 * CHAR_W + PAD;  // enough for "-12 C"
  const uint16_t pmX = humX + 4 * CHAR_W + PAD;    // enough for "100%"
  const uint16_t pmmaxX = pmX + 3 * CHAR_W + 2*PAD;

  // Header line
  displayDrawString(nameX, y, "Loc.", BLACK, 16);
  displayDrawString(tempX, y, "T", BLACK, 16);
  displayDrawString(humX, y, "RH", BLACK, 16);
  displayDrawString(pmX, y+2, "PM2.5", BLACK, 12);
  displayDrawString(pmmaxX+4, y+2, "max", BLACK, 12);

  y += 16 + PAD;

  uint8_t visibleLocations = visibleLocationSlots();
  if (visibleLocations > locationCount) {
    visibleLocations = locationCount;
  }

  for (uint8_t i = 0; i < visibleLocations; i++) {
    displayDrawString(nameX, y, locations[i].name, BLACK, FONTSIZE);

    displayDrawBitmap(iconX, y, weatherCodeToBitmap(locations[i].weathercode), 24, 24, BLACK);

    snprintf(linebuf, sizeof(linebuf), "%.0f", locations[i].temperature);
    displayDrawString(tempX, y, linebuf, BLACK, FONTSIZE);
    displayDrawString(tempX + strlen(linebuf) * CHAR_W + 6, y + 7, "C", BLACK, 16);

    snprintf(linebuf, sizeof(linebuf), "%u%%", locations[i].humidity);
    displayDrawString(humX, y, linebuf, BLACK, FONTSIZE);

    snprintf(linebuf, sizeof(linebuf), "%.0f", locations[i].pm2_5.get().value);
    displayDrawString(pmX, y, linebuf, BLACK, FONTSIZE);

    if (locations[i].pm2_5max.get().hasValue()) {
      snprintf(linebuf, sizeof(linebuf), "%.0f", locations[i].pm2_5max.get().value);
      displayDrawString(pmmaxX, y + 8, linebuf, BLACK, 12);
    }

    y += FONTSIZE + PAD;
  }


  if (timeValid) {
    strftime(linebuf, sizeof(linebuf), "Last update: %Y-%m-%d %H:%M:%S", &timeinfo);
    displayDrawString(PAD, SCR_HEIGHT - 14, linebuf, BLACK, 12);
  } else {
    displayDrawString(PAD, SCR_HEIGHT - 14, "Last update time unavailable", BLACK, 12);
  }
  snprintf(linebuf, sizeof(linebuf), "boot #%u", bootCount.get());
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

  pinMode(MENU_KEY, INPUT_PULLUP);
  pinMode(EXIT_KEY, INPUT_PULLUP);

  esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);

  // Wake if either button goes LOW.
  esp_sleep_enable_ext1_wakeup(
    (1ULL << MENU_KEY) | (1ULL << EXIT_KEY),
    ESP_EXT1_WAKEUP_ANY_LOW
  );

  esp_deep_sleep_start();
}

BootMode printWakeReason() {
  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();

  if (reason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial0.println("Wake reason: timer");
    return BootMode::Timer;
  }

  if (reason == ESP_SLEEP_WAKEUP_EXT1) {
    uint64_t wakeMask = esp_sleep_get_ext1_wakeup_status();

    if (wakeMask & (1ULL << MENU_KEY)) {
      Serial0.println("Wake reason: MENU");
      return BootMode::Menu;
    }

    if (wakeMask & (1ULL << EXIT_KEY)) {
      Serial0.println("Wake reason: EXIT");
      return BootMode::Exit;
    }

    Serial0.println("Wake reason: EXT1");
    return BootMode::Normal;
  }

  Serial0.println("Wake reason: normal boot");
  return BootMode::Normal;
}

void loop() { }
