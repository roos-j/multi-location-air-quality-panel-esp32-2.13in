#include "portal.h"
#include "app.h"
#include <WebServer.h>
#include <LittleFS.h>
#include <stdlib.h>

WebServer *server = nullptr;

struct LocationForm {
  char name[sizeof(locations[0].name)];
  float latitude;
  float longitude;
  uint32_t sensorIndex;
  char readKey[sizeof(locations[0].purpleAirReadKey)];
};

struct ConfigForm {
  char portalSsid[sizeof(portal_ssid)];
  char portalPwd[sizeof(portal_pwd)];
  char wifiSsid[sizeof(ssid)];
  char wifiPwd[sizeof(pwd)];
  char apiKey[sizeof(purpleAirApiKey)];
  char targetField[sizeof(purpleAirTargetField)];
  uint32_t sleepSeconds;
  uint32_t pm25ReadInterval;
  uint32_t locationCount;
  LocationForm locations[MAX_LOCATIONS];
};

static ConfigForm defaultConfig;
static bool defaultConfigCaptured = false;

static void copyCurrentConfigForm(ConfigForm &cfg);
static bool parseConfigForm(ConfigForm &cfg);
static void applyConfigForm(const ConfigForm &cfg);
static bool saveConfigPreferencesInternal();
static bool saveConfigPreferences();
static void renderConfigPage(bool saved, bool reset);
static void handleConfigReset();

void captureDefaultConfig() {
  if (defaultConfigCaptured) {
    return;
  }

  copyCurrentConfigForm(defaultConfig);
  defaultConfigCaptured = true;
}

void displayWaitMessage() {
  displayClear();
  displayDrawCenteredString((SCR_HEIGHT - 24) / 2, "One moment...", BLACK, 24);
  displayUpdate();
}

/** Portal mode opens a wifi server and stays active until dismissed */
void setupPortalMode() {
  Serial0.println("Starting portal mode...");

  displayWaitMessage();
  startFileServer();
  showPortalScreen();

  while (true) {
    server->handleClient();

    if (digitalRead(EXIT_KEY) == LOW) {
      //delay(250);
      //goToDeepSleep(SLEEP_SECONDS);
      displayWaitMessage();
      setupNormalMode();
    }

    delay(5);
  }
}

void showPortalScreen() {
  char linebuf[128];

  int blockHeight = 30 * 2;
  int startY = (SCR_HEIGHT - blockHeight) / 2;

  snprintf(linebuf, sizeof(linebuf), "SSID: %s", portal_ssid);
  displayDrawCenteredString(startY, linebuf, BLACK, 24);

  snprintf(linebuf, sizeof(linebuf), "Password: %s", portal_pwd);
  displayDrawCenteredString(startY + 30, linebuf, BLACK, 24);

  displayDrawString(
    PAD,
    SCR_HEIGHT - 14,
    "Press EXIT to close",
    BLACK,
    12
  );

  displayUpdate();
}

/** Start file server for portal mode */
void startFileServer() {
  if (server == nullptr) {
    server = new WebServer(80);
  }

  server->on("/", HTTP_GET, handleFiles);
  server->on("/files", HTTP_GET, handleFiles);
  server->on("/config", HTTP_GET, handleConfig);
  server->on("/config", HTTP_POST, handleConfig);
  server->on("/config-reset", HTTP_POST, handleConfigReset);
  server->on("/download", HTTP_GET, handleDownload);
  server->on("/delete", HTTP_POST, handleDeleteFile);
  server->on("/delete-all", HTTP_POST, handleDeleteAll);

  server->begin();

  Serial0.print("File server: http://");
  Serial0.print(WiFi.softAPIP());
  Serial0.println("/files or /config");
}

static void redirectToFiles() {
  server->sendHeader("Location", "/files");
  server->send(303, "text/plain", "");
}

static void copyCurrentConfigForm(ConfigForm &cfg) {
  memcpy(cfg.portalSsid, portal_ssid, sizeof(cfg.portalSsid));
  memcpy(cfg.portalPwd, portal_pwd, sizeof(cfg.portalPwd));
  memcpy(cfg.wifiSsid, ssid, sizeof(cfg.wifiSsid));
  memcpy(cfg.wifiPwd, pwd, sizeof(cfg.wifiPwd));
  memcpy(cfg.apiKey, purpleAirApiKey, sizeof(cfg.apiKey));
  memcpy(cfg.targetField, purpleAirTargetField, sizeof(cfg.targetField));
  cfg.sleepSeconds = sleepSeconds;
  cfg.pm25ReadInterval = pm25ReadInterval;
  cfg.locationCount = locationCount;

  for (uint8_t i = 0; i < MAX_LOCATIONS; i++) {
    memcpy(cfg.locations[i].name, locations[i].name, sizeof(cfg.locations[i].name));
    cfg.locations[i].latitude = locations[i].latitude;
    cfg.locations[i].longitude = locations[i].longitude;
    cfg.locations[i].sensorIndex = locations[i].purpleAirSensorIndex;
    memcpy(cfg.locations[i].readKey, locations[i].purpleAirReadKey, sizeof(cfg.locations[i].readKey));
  }
}

static bool isSafeTsPath(const char *path) {
  if (path == nullptr) {
    return false;
  }

  if (strncmp(path, "/ts/", 4) != 0) {
    return false;
  }

  if (strstr(path, "..") != nullptr) {
    return false;
  }

  return true;
}

static bool copyRequestArg(const char *name, char *out, size_t outSize) {
  if (server == nullptr || out == nullptr || outSize == 0) {
    return false;
  }

  if (!server->hasArg(name)) {
    return false;
  }

  String arg = server->arg(name);
  return copyCString(out, outSize, arg.c_str());
}

static bool parseRequestUInt(const char *name, uint32_t &out) {
  return server != nullptr &&
         name != nullptr &&
         server->hasArg(name) &&
         parseUint32(server->arg(name).c_str(), out);
}

static bool parseRequestFloat(const char *name, float &out) {
  return server != nullptr &&
         name != nullptr &&
         server->hasArg(name) &&
         parseFloatValue(server->arg(name).c_str(), out);
}

static void renderInputField(const char *label, const char *name, const char *type, const char *value, const char *attrs) {
  if (server == nullptr) {
    return;
  }

  bool secret = type != nullptr && strcmp(type, "password") == 0;
  char escaped[128];

  server->sendContent("<div>");
  server->sendContent("<label for='");
  server->sendContent(name);
  server->sendContent("'>");
  if (htmlEscape(label, escaped, sizeof(escaped))) {
    server->sendContent(escaped);
  }
  server->sendContent("</label>");
  if (secret) {
    server->sendContent("<div class='secret-field'>");
  }
  server->sendContent("<input id='");
  server->sendContent(name);
  server->sendContent("' name='");
  server->sendContent(name);
  server->sendContent("' type='");
  server->sendContent(type);
  server->sendContent("' value='");
  if (htmlEscape(value, escaped, sizeof(escaped))) {
    server->sendContent(escaped);
  }
  server->sendContent("'");

  if (attrs != nullptr && attrs[0] != '\0') {
    server->sendContent(" ");
    server->sendContent(attrs);
  }

  if (secret) {
    server->sendContent(">");
    server->sendContent("<button class='secret-toggle' type='button' aria-label='Show secret' aria-pressed='false' onclick=\"toggleSecret(this, '");
    server->sendContent(name);
    server->sendContent("')\">");
    server->sendContent(
      "<svg class='eye-open' viewBox='0 0 24 24' aria-hidden='true' focusable='false'>"
      "<path d='M12 5c5.5 0 9.8 4 11 7-1.2 3-5.5 7-11 7S2.2 15 1 12c1.2-3 5.5-7 11-7zm0 2C8 7 4.7 9.8 3.1 12 4.7 14.2 8 17 12 17s7.3-2.8 8.9-5C19.3 9.8 16 7 12 7zm0 2.5A2.5 2.5 0 1 1 12 14a2.5 2.5 0 0 1 0-5z'/>"
      "</svg>"
      "<svg class='eye-slash' viewBox='0 0 24 24' aria-hidden='true' focusable='false'>"
      "<path d='M2.1 3.5 20.5 21.9l1.4-1.4-3.2-3.2c2.4-1.8 4.3-4 5.3-5.3-1.2-3-5.5-7-11-7-1.4 0-2.7.2-3.9.6L3.5 2.1 2.1 3.5zm8.4 8.4L9 10.4A2.5 2.5 0 0 0 12.5 14l-2-2.1zM12 5C6.5 5 2.2 9 1 12c.7 1.7 2.3 3.9 5.1 5.7l2-2C6.6 14.2 4.7 12.5 3.9 12c1.5-2.1 4.7-5 8.1-5 1.1 0 2.1.2 3 .5l1.6-1.6C15.4 5.5 13.8 5 12 5z'/>"
      "</svg>");
    server->sendContent("</button>");
    server->sendContent("</div>");
  } else {
    server->sendContent(">");
  }
  server->sendContent("</div>");
}

static void renderConfigLocation(uint8_t index) {
  if (server == nullptr) {
    return;
  }

  char fieldName[32];
  char valueBuf[64];
  char attrs[96];
  char legend[32];
  char indexText[8];

  snprintf(legend, sizeof(legend), "Location %u", index + 1);
  snprintf(indexText, sizeof(indexText), "%u", index);

  server->sendContent("<fieldset class='loc' data-loc-index='");
  server->sendContent(indexText);
  server->sendContent("'");

  if (index >= locationCount) {
    server->sendContent(" hidden");
  }

  server->sendContent("><legend>");
  char escapedLegend[64];

  if (htmlEscape(legend, escapedLegend, sizeof(escapedLegend))) {
    server->sendContent(escapedLegend);
  }
  server->sendContent("</legend><div class='grid-2'>");

  snprintf(fieldName, sizeof(fieldName), "loc%u_name", index);
  snprintf(attrs, sizeof(attrs), "maxlength='%u' autocomplete='off'", (unsigned)(sizeof(locations[index].name) - 1));
  renderInputField("Name", fieldName, "text", locations[index].name, attrs);

  snprintf(fieldName, sizeof(fieldName), "loc%u_idx", index);
  snprintf(
    valueBuf,
    sizeof(valueBuf),
    "%lu",
    static_cast<unsigned long>(locations[index].purpleAirSensorIndex)
  );
  renderInputField("PurpleAir sensor index", fieldName, "number", valueBuf, "min='0' step='1'");

  snprintf(fieldName, sizeof(fieldName), "loc%u_lat", index);
  snprintf(valueBuf, sizeof(valueBuf), "%.6f", locations[index].latitude);
  renderInputField("Latitude", fieldName, "number", valueBuf, "step='any'");

  snprintf(fieldName, sizeof(fieldName), "loc%u_lon", index);
  snprintf(valueBuf, sizeof(valueBuf), "%.6f", locations[index].longitude);
  renderInputField("Longitude", fieldName, "number", valueBuf, "step='any'");

  snprintf(fieldName, sizeof(fieldName), "loc%u_key", index);
  snprintf(attrs, sizeof(attrs), "maxlength='%u' autocomplete='off'", (unsigned)(sizeof(locations[index].purpleAirReadKey) - 1));
  renderInputField("PurpleAir read key", fieldName, "password", locations[index].purpleAirReadKey, attrs);

  server->sendContent("</div></fieldset>");
}

static void applyConfigForm(const ConfigForm &cfg) {
  memcpy(portal_ssid, cfg.portalSsid, sizeof(portal_ssid));
  memcpy(portal_pwd, cfg.portalPwd, sizeof(portal_pwd));
  memcpy(ssid, cfg.wifiSsid, sizeof(ssid));
  memcpy(pwd, cfg.wifiPwd, sizeof(pwd));
  memcpy(purpleAirApiKey, cfg.apiKey, sizeof(purpleAirApiKey));
  memcpy(purpleAirTargetField, cfg.targetField, sizeof(purpleAirTargetField));
  sleepSeconds = cfg.sleepSeconds;
  pm25ReadInterval = cfg.pm25ReadInterval;
  locationCount = static_cast<uint8_t>(cfg.locationCount);

  for (uint8_t i = 0; i < MAX_LOCATIONS; i++) {
    memcpy(locations[i].name, cfg.locations[i].name, sizeof(locations[i].name));
    locations[i].latitude = cfg.locations[i].latitude;
    locations[i].longitude = cfg.locations[i].longitude;
    locations[i].purpleAirSensorIndex = cfg.locations[i].sensorIndex;
    memcpy(locations[i].purpleAirReadKey, cfg.locations[i].readKey, sizeof(locations[i].purpleAirReadKey));
  }
}

static bool saveConfigPreferencesInternal() {
  if (!prefs.begin("config", false)) {
    Serial0.println("Config NVS open failed");
    return false;
  }

  bool ok = true;

  ok &= prefs.putString("p_ssid", portal_ssid) > 0;
  ok &= prefs.putString("p_pwd", portal_pwd) > 0;
  ok &= prefs.putString("w_ssid", ssid) > 0;
  ok &= prefs.putString("w_pwd", pwd) > 0;
  ok &= prefs.putString("pa_key", purpleAirApiKey) > 0;
  ok &= prefs.putString("pa_field", purpleAirTargetField) > 0;
  ok &= prefs.putUInt("sleep_s", sleepSeconds) > 0;
  ok &= prefs.putUInt("pm25int", pm25ReadInterval) > 0;
  ok &= prefs.putUInt("loc_cnt", locationCount) > 0;

  for (uint8_t i = 0; i < MAX_LOCATIONS; i++) {
    char key[16];

    snprintf(key, sizeof(key), "l%u_name", i);
    ok &= prefs.putString(key, locations[i].name) > 0;

    snprintf(key, sizeof(key), "l%u_lat", i);
    ok &= prefs.putFloat(key, locations[i].latitude) > 0;

    snprintf(key, sizeof(key), "l%u_lon", i);
    ok &= prefs.putFloat(key, locations[i].longitude) > 0;

    snprintf(key, sizeof(key), "l%u_idx", i);
    ok &= prefs.putUInt(key, locations[i].purpleAirSensorIndex) > 0;

    snprintf(key, sizeof(key), "l%u_key", i);
    ok &= prefs.putString(key, locations[i].purpleAirReadKey) > 0;
  }

  prefs.end();
  return ok;
}

static bool saveConfigPreferences() {
  if (sleepSeconds == 0) {
    sleepSeconds = 1;
  }

  if (pm25ReadInterval == 0) {
    pm25ReadInterval = 1;
  }

  if (locationCount < 1) {
    locationCount = 1;
  } else if (locationCount > MAX_LOCATIONS) {
    locationCount = MAX_LOCATIONS;
  }

  return saveConfigPreferencesInternal();
}

static void handleConfigReset() {
  if (server == nullptr) {
    return;
  }

  if (!defaultConfigCaptured) {
    server->send(500, "text/plain", "Default config snapshot unavailable");
    return;
  }

  applyConfigForm(defaultConfig);

  if (!saveConfigPreferences()) {
    server->send(500, "text/plain", "Failed to save defaults");
    return;
  }

  server->sendHeader("Location", "/config?reset=1");
  server->send(303, "text/plain", "");
}

static bool parseConfigForm(ConfigForm &cfg) {
  if (server == nullptr) {
    return false;
  }

  if (!copyRequestArg("portal_ssid", cfg.portalSsid, sizeof(cfg.portalSsid))) {
    return false;
  }
  if (!copyRequestArg("portal_pwd", cfg.portalPwd, sizeof(cfg.portalPwd))) {
    return false;
  }
  if (!copyRequestArg("wifi_ssid", cfg.wifiSsid, sizeof(cfg.wifiSsid))) {
    return false;
  }
  if (!copyRequestArg("wifi_pwd", cfg.wifiPwd, sizeof(cfg.wifiPwd))) {
    return false;
  }
  if (!copyRequestArg("pa_key", cfg.apiKey, sizeof(cfg.apiKey))) {
    return false;
  }
  if (!copyRequestArg("pa_field", cfg.targetField, sizeof(cfg.targetField))) {
    return false;
  }

  if (!parseRequestUInt("sleep_seconds", cfg.sleepSeconds) || cfg.sleepSeconds == 0) {
    return false;
  }

  if (!parseRequestUInt("pm25_read_interval", cfg.pm25ReadInterval) || cfg.pm25ReadInterval == 0) {
    return false;
  }

  cfg.locationCount = locationCount;
  if (server->hasArg("location_count")) {
    if (!parseRequestUInt("location_count", cfg.locationCount)) {
      return false;
    }
  }

  if (cfg.locationCount < 1) {
    cfg.locationCount = 1;
  } else if (cfg.locationCount > MAX_LOCATIONS) {
    cfg.locationCount = MAX_LOCATIONS;
  }

  for (uint8_t i = 0; i < MAX_LOCATIONS; i++) {
    char fieldName[32];

    snprintf(fieldName, sizeof(fieldName), "loc%u_name", i);
    if (!copyRequestArg(fieldName, cfg.locations[i].name, sizeof(cfg.locations[i].name))) {
      return false;
    }

    snprintf(fieldName, sizeof(fieldName), "loc%u_lat", i);
    if (!parseRequestFloat(fieldName, cfg.locations[i].latitude)) {
      return false;
    }

    snprintf(fieldName, sizeof(fieldName), "loc%u_lon", i);
    if (!parseRequestFloat(fieldName, cfg.locations[i].longitude)) {
      return false;
    }

    snprintf(fieldName, sizeof(fieldName), "loc%u_idx", i);
    if (!parseRequestUInt(fieldName, cfg.locations[i].sensorIndex)) {
      return false;
    }

    snprintf(fieldName, sizeof(fieldName), "loc%u_key", i);
    if (!copyRequestArg(fieldName, cfg.locations[i].readKey, sizeof(cfg.locations[i].readKey))) {
      return false;
    }
  }

  if (cfg.portalSsid[0] == '\0' || cfg.wifiSsid[0] == '\0') {
    return false;
  }

  return true;
}

static void renderConfigPage(bool saved, bool reset) {
  if (server == nullptr) {
    return;
  }

  char numBuf[32];
  char maxLocationsBuf[8];

  snprintf(maxLocationsBuf, sizeof(maxLocationsBuf), "%u", MAX_LOCATIONS);

  server->sendContent(
    "<!doctype html>"
    "<html lang='en'>"
    "<head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Config</title>"
    "<style>"
    "body{font-family:system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
    "margin:0;background:linear-gradient(180deg,#f4f7fb 0%,#eef2f7 100%);color:#1f2937;}"
    "main{max-width:1080px;margin:32px auto;padding:0 16px 40px;}"
    ".top{display:flex;justify-content:space-between;align-items:flex-end;gap:16px;flex-wrap:wrap;margin-bottom:20px;}"
    "h1{font-size:32px;margin:0 0 6px;}"
    ".sub{color:#5b6472;font-size:14px;line-height:1.4;}"
    ".banner{padding:12px 14px;border-radius:12px;background:#e8f5e9;color:#1b5e20;margin:0 0 18px;}"
    ".card{background:white;border-radius:18px;box-shadow:0 14px 40px rgba(17,24,39,.08);padding:18px;}"
    "fieldset{border:1px solid #e5e7eb;border-radius:16px;padding:16px 16px 8px;margin:18px 0;background:#fbfcfe;}"
    "legend{padding:0 8px;font-weight:700;color:#111827;}"
    ".grid-3,.grid-2{display:grid;gap:12px 16px;}"
    ".grid-3{grid-template-columns:repeat(3,minmax(0,1fr));}"
    ".grid-2{grid-template-columns:repeat(2,minmax(0,1fr));}"
    ".span-2{grid-column:span 2;}"
    "label{display:block;font-size:12px;font-weight:700;letter-spacing:.04em;text-transform:uppercase;color:#52606d;margin-bottom:6px;}"
    "input{width:100%;box-sizing:border-box;padding:10px 12px;border:1px solid #d8dee9;border-radius:10px;background:#fff;font:inherit;}"
    ".secret-field{position:relative;}"
    ".secret-field input{padding-right:3rem;}"
    ".secret-toggle{position:absolute;right:0.45rem;top:50%;transform:translateY(-50%);display:inline-flex;align-items:center;justify-content:center;width:2rem;height:2rem;border:0;border-radius:999px;background:transparent;color:#52606d;cursor:pointer;}"
    ".secret-toggle:hover{background:#eef2f7;color:#0f172a;}"
    ".secret-toggle:focus{outline:none;box-shadow:0 0 0 3px rgba(11,99,206,.12);}"
    ".secret-toggle svg{width:1.1rem;height:1.1rem;fill:currentColor;display:block;}"
    ".secret-toggle .eye-slash{display:none;}"
    ".secret-toggle.is-visible .eye-open{display:none;}"
    ".secret-toggle.is-visible .eye-slash{display:block;}"
    "input:focus{outline:none;border-color:#0b63ce;box-shadow:0 0 0 3px rgba(11,99,206,.12);}"
    ".note{margin-top:10px;color:#5b6472;font-size:13px;line-height:1.45;}"
    ".actions{display:flex;justify-content:flex-end;gap:12px;align-items:center;margin-top:18px;flex-wrap:wrap;}"
    ".btn{display:inline-block;padding:10px 14px;border:0;border-radius:10px;background:#0b63ce;color:white;text-decoration:none;font-size:14px;font-weight:600;cursor:pointer;}"
    ".btn:hover{text-decoration:none;background:#094f9c;}"
    ".btn.secondary{background:#64748b;}"
    ".btn.secondary:hover{background:#475569;}"
    ".btn.danger{background:#c62828;}"
    ".btn.danger:hover{background:#9f1f1f;}"
    ".loc[hidden]{display:none;}"
    "@media (max-width:720px){.grid-3,.grid-2{grid-template-columns:1fr;} .span-2{grid-column:span 1;} .top{align-items:flex-start;} .actions{justify-content:stretch;} .btn{width:100%;text-align:center;}}"
    "</style>"
    "</head>"
    "<body>"
    "<main>"
    "<div class='top'>"
    "<div>"
    "<h1>Configuration</h1>"
    "</div>"
    "<div>"
    "<a class='btn secondary' href='/files'>Files</a>"
    "</div>"
    "</div>"
  );

  if (reset) {
    server->sendContent("<div class='banner'>Defaults restored.</div>");
  } else if (saved) {
    server->sendContent("<div class='banner'>Settings saved.</div>");
  }

  server->sendContent(
    "<div class='card'>"
    "<form method='POST' action='/config' onsubmit=\"return confirm('Save settings?');\">"
    "<fieldset>"
    "<legend>Portal and WiFi</legend>"
    "<div class='grid-3'>"
  );

  renderInputField("Portal SSID", "portal_ssid", "text", portal_ssid, "maxlength='31' autocomplete='off'");
  renderInputField("Portal password", "portal_pwd", "password", portal_pwd, "maxlength='63' autocomplete='off'");
  renderInputField("WiFi SSID", "wifi_ssid", "text", ssid, "maxlength='31' autocomplete='off'");
  renderInputField("WiFi password", "wifi_pwd", "password", pwd, "maxlength='63' autocomplete='off'");
  renderInputField("PurpleAir API key", "pa_key", "password", purpleAirApiKey, "maxlength='63' autocomplete='off'");
  renderInputField("PurpleAir field", "pa_field", "text", purpleAirTargetField, "maxlength='31' autocomplete='off'");

  server->sendContent("</div></fieldset>");

  server->sendContent(
    "<fieldset>"
    "<legend>Intervals</legend>"
    "<div class='grid-3'>"
  );

  snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(sleepSeconds));
  renderInputField("Sleep interval (sec)", "sleep_seconds", "number", numBuf, "min='1' step='1'");

  snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(pm25ReadInterval));
  renderInputField("PM2.5 read interval (sec)", "pm25_read_interval", "number", numBuf, "min='1' step='1'");

  /*
  snprintf(numBuf, sizeof(numBuf), "%u", static_cast<unsigned>(locationCount));
  snprintf(attrs, sizeof(attrs), "min='1' max='%u' step='1' oninput='syncLocationCount()'", MAX_LOCATIONS);
  renderInputField("Location count", "location_count", "number", numBuf, attrs);

  server->sendContent("</div><div class='note'>Location count controls how many location panels are active. Extra slots stay saved but hidden.</div></fieldset>");
  */

  server->sendContent("</div></fieldset>");

  server->sendContent("<fieldset><legend>Locations</legend>");

  for (uint8_t i = 0; i < MAX_LOCATIONS; i++) {
    renderConfigLocation(i);
  }

  server->sendContent(
    "</fieldset>"
    "<div class='actions'>"
    "<button class='btn' type='submit'>Save</button>"
    "<button class='btn danger' type='submit' formaction='/config-reset' formmethod='POST' formnovalidate onclick=\"return confirm('Reset all settings to defaults?');\">Reset to defaults</button>"
    "<a class='btn secondary' href='/files'>Back</a>"
    "</div>"
    "</form>"
    "</div>"
    "<script>"
    "function toggleSecret(button,inputId){"
    "const input=document.getElementById(inputId);"
    "if(!input)return;"
    "const hidden=input.type==='password';"
    "input.type=hidden?'text':'password';"
    "button.classList.toggle('is-visible',hidden);"
    "button.setAttribute('aria-pressed',hidden?'true':'false');"
    "button.setAttribute('aria-label',hidden?'Hide secret':'Show secret');"
    "}"
    "function syncLocationCount(){"
    "const input=document.getElementById('location_count');"
    "if(!input)return;"
    "const count=Math.max(1,Math.min(parseInt(input.value||'1',10)||1,"
  );

  server->sendContent(maxLocationsBuf);

  server->sendContent(
    "));"
    "document.querySelectorAll('.loc').forEach(function(el){"
    "const idx=parseInt(el.getAttribute('data-loc-index'),10);"
    "el.hidden=idx>=count;"
    "});"
    "}"
    "window.addEventListener('DOMContentLoaded',syncLocationCount);"
    "</script>"
    "</main>"
    "</body>"
    "</html>"
  );
}

void handleDeleteFile() {
  if (server == nullptr) {
    return;
  }

  char path[96];

  if (!copyRequestArg("path", path, sizeof(path))) {
    server->send(400, "text/plain", "Missing or invalid path");
    return;
  }

  if (!isSafeTsPath(path)) {
    server->send(403, "text/plain", "Forbidden");
    return;
  }

  LittleFS.remove(path);

  redirectToFiles();
}

void handleDeleteAll() {
  if (server == nullptr) {
    return;
  }

  File dir = LittleFS.open("/ts");

  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }

    server->send(500, "text/plain", "Could not open /ts");
    return;
  }

  File file = dir.openNextFile();

  while (file) {
    if (!file.isDirectory()) {
      char path[96];

      snprintf(path, sizeof(path), "%s", file.path());

      file.close();

      if (isSafeTsPath(path)) {
        LittleFS.remove(path);
      }
    } else {
      file.close();
    }

    file = dir.openNextFile();
  }

  dir.close();

  redirectToFiles();
}

/** Generate file index on portal page */
void handleFiles() {
  if (server == nullptr) {
    return;
  }

  File dir = LittleFS.open("/ts");

  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }

    server->send(500, "text/plain", "Could not open /ts");
    return;
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "text/html", "");

  server->sendContent(
    "<!doctype html>"
    "<html lang='en'>"
    "<head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Time series files</title>"
    "<style>"
    "body{font-family:system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
    "margin:0;background:#f6f7f9;color:#222;}"
    "main{max-width:900px;margin:32px auto;padding:0 16px;}"
    ".top{display:flex;justify-content:space-between;align-items:center;gap:16px;margin-bottom:24px;}"
    "h1{font-size:28px;margin:0 0 6px;}"
    ".sub{color:#666;}"
    "table{width:100%;border-collapse:collapse;background:white;border-radius:12px;"
    "overflow:hidden;box-shadow:0 2px 12px rgba(0,0,0,.08);}"
    "th,td{padding:12px 14px;border-bottom:1px solid #eee;text-align:left;}"
    "th{background:#fafafa;font-weight:600;color:#444;}"
    "tr:last-child td{border-bottom:none;}"
    ".size{text-align:right;white-space:nowrap;color:#555;}"
    ".action{text-align:right;white-space:nowrap;}"
    "a{color:#0b63ce;text-decoration:none;}"
    "a:hover{text-decoration:underline;}"
    ".btn{display:inline-block;padding:6px 10px;border:0;border-radius:8px;background:#0b63ce;"
    "color:white;text-decoration:none;font-size:14px;cursor:pointer;margin-left:6px;}"
    ".btn:hover{text-decoration:none;background:#084f9f;}"
    ".danger{background:#c62828;}"
    ".danger:hover{background:#9f1f1f;}"
    "form{display:inline;}"
    ".empty{padding:24px;background:white;border-radius:12px;box-shadow:0 2px 12px rgba(0,0,0,.08);}"
    "</style>"
    "</head>"
    "<body>"
    "<main>"
    "<div class='top'>"
    "<div>"
    "<h1>Time series files</h1>"
    "<div class='sub'>Folder <code>/ts</code></div>"
    "</div>"
    "<a class='btn' href='/config'>Config</a>"
    "<form method='POST' action='/delete-all' "
    "onsubmit=\"return confirm('Delete all files?');\">"
    "<button class='btn danger' type='submit'>Delete all</button>"
    "</form>"
    "</div>"
  );

  File file = dir.openNextFile();
  bool anyFiles = false;

  server->sendContent(
    "<table>"
    "<thead>"
    "<tr>"
    "<th>File</th>"
    "<th class='size'>Size</th>"
    "<th class='action'>Action</th>"
    "</tr>"
    "</thead>"
    "<tbody>"
  );

  while (file) {
    if (!file.isDirectory()) {
      anyFiles = true;

      char sizeText[24];
      char line[768];

      formatBytes(file.size(), sizeText, sizeof(sizeText));

      int n = snprintf(
        line,
        sizeof(line),
        "<tr>"
        "<td><a href='/download?path=%s'>%s</a></td>"
        "<td class='size'>%s</td>"
        "<td class='action'>"
        "<a class='btn' href='/download?path=%s'>Download</a>"
        "<form method='POST' action='/delete' "
        "onsubmit=\"return confirm('Delete this file?');\">"
        "<input type='hidden' name='path' value='%s'>"
        "<button class='btn danger' type='submit'>Delete</button>"
        "</form>"
        "</td>"
        "</tr>",
        file.path(),
        file.name(),
        sizeText,
        file.path(),
        file.path()
      );

      if (n > 0 && (size_t)n < sizeof(line)) {
        server->sendContent(line);
      }
    }

    file.close();
    file = dir.openNextFile();
  }

  dir.close();

  server->sendContent(
    "</tbody>"
    "</table>"
  );

  if (!anyFiles) {
    server->sendContent(
      "<script>document.querySelector('table').style.display='none';</script>"
      "<div class='empty'>No files found.</div>"
    );
  }

  server->sendContent(
    "</main>"
    "</body>"
    "</html>"
  );
}

/** Handle config page on portal */
void handleConfig() {
  if (server == nullptr) {
    return;
  }

  if (server->method() == HTTP_POST) {
    ConfigForm cfg{};

    if (!parseConfigForm(cfg)) {
      server->send(400, "text/plain", "Missing or invalid config field");
      return;
    }

    applyConfigForm(cfg);

    if (!saveConfigPreferences()) {
      server->send(500, "text/plain", "Failed to save config");
      return;
    }

    server->sendHeader("Location", "/config?saved=1");
    server->send(303, "text/plain", "");
    return;
  }

  bool saved = server->hasArg("saved");
  bool reset = server->hasArg("reset");

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "text/html", "");
  renderConfigPage(saved, reset);
}

/** Handle download request on portal page */
void handleDownload() {
  if (server == nullptr) {
    return;
  }

  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  // WebServer arg API returns String, so copy it into a fixed buffer immediately.
  String pathArg = server->arg("path");

  char path[96];

  if (pathArg.length() == 0 || pathArg.length() >= sizeof(path)) {
    server->send(400, "text/plain", "Bad path");
    return;
  }

  pathArg.toCharArray(path, sizeof(path));

  // Only allow downloading files from /ts.
  if (strncmp(path, "/ts/", 4) != 0) {
    server->send(403, "text/plain", "Forbidden");
    return;
  }

  // Basic path traversal guard.
  if (strstr(path, "..") != nullptr) {
    server->send(403, "text/plain", "Forbidden");
    return;
  }

  File file = LittleFS.open(path, FILE_READ);

  if (!file || file.isDirectory()) {
    if (file) {
      file.close();
    }

    server->send(404, "text/plain", "File not found");
    return;
  }

  const char *slash = strrchr(path, '/');
  const char *filename = slash ? slash + 1 : path;

  char disposition[128];

  snprintf(
    disposition,
    sizeof(disposition),
    "attachment; filename=\"%s\"",
    filename
  );

  server->sendHeader("Content-Disposition", disposition);
  server->streamFile(file, "application/octet-stream");

  file.close();
}
