#include "portal.h"
#include "app.h"
#include <WebServer.h>
#include <LittleFS.h>
#include <math.h>
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
static void renderViewLocationControl(uint8_t index, const char *currentMonth);
static void renderViewPage();
static void handleView();
static void handleViewData();
static void handleConfigReset();
static void sendViewDataJson(uint8_t locationIndex, const char *monthText);
static bool currentUtcMonthText(char *out, size_t outSize);

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

  displayClear();

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

  server->on("/", HTTP_GET, handleView);
  server->on("/files", HTTP_GET, handleFiles);
  server->on("/view", HTTP_GET, handleView);
  server->on("/view-data", HTTP_GET, handleViewData);
  server->on("/config", HTTP_GET, handleConfig);
  server->on("/config", HTTP_POST, handleConfig);
  server->on("/config-reset", HTTP_POST, handleConfigReset);
  server->on("/download", HTTP_GET, handleDownload);
  server->on("/delete", HTTP_POST, handleDeleteFile);
  server->on("/delete-all", HTTP_POST, handleDeleteAll);

  server->begin();

  Serial0.print("File server: http://");
  Serial0.print(WiFi.softAPIP());
  Serial0.println("/view, /files or /config");
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

static bool currentUtcMonthText(char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return false;
  }

  time_t now = time(nullptr);

  if (formatUtcMonth(static_cast<uint32_t>(now), out, outSize)) {
    return true;
  }

  if (outSize >= 8) {
    snprintf(out, outSize, "1970-01");
    return true;
  }

  return false;
}

static void renderViewLocationControl(uint8_t index, const char *currentMonth) {
  if (server == nullptr || currentMonth == nullptr) {
    return;
  }

  char nameBuf[64];
  char nameEscaped[96];
  char indexBuf[8];

  const char *name = locations[index].name;

  if (name == nullptr || name[0] == '\0') {
    snprintf(nameBuf, sizeof(nameBuf), "Location %u", index + 1);
    name = nameBuf;
  }

  if (!htmlEscape(name, nameEscaped, sizeof(nameEscaped))) {
    snprintf(nameEscaped, sizeof(nameEscaped), "Location %u", index + 1);
  }

  snprintf(indexBuf, sizeof(indexBuf), "%u", index);

  server->sendContent("<div class='control-box series-control' data-series='");
  server->sendContent(indexBuf);
  server->sendContent("'>");
  server->sendContent("<div class='control-head'>");
  server->sendContent("<div>");
  server->sendContent("<div class='control-name series-name'>");
  server->sendContent(nameEscaped);
  server->sendContent("</div>");
  server->sendContent("</div>");
  server->sendContent("<div class='control-status series-status' id='series-status-");
  server->sendContent(indexBuf);
  server->sendContent("'>Idle</div>");
  server->sendContent("</div>");
  server->sendContent("<label for='month-");
  server->sendContent(indexBuf);
  server->sendContent("'>Month</label>");
  server->sendContent("<input id='month-");
  server->sendContent(indexBuf);
  server->sendContent("' class='month-input' type='month' value='");
  server->sendContent(currentMonth);
  server->sendContent("'>");
  server->sendContent("</div>");
}

static void sendViewDataJson(uint8_t locationIndex, const char *monthText) {
  if (server == nullptr || monthText == nullptr || locationIndex >= MAX_LOCATIONS) {
    return;
  }

  int year = 0;
  int month = 0;

  if (!parseMonthText(monthText, year, month)) {
    server->send(400, "text/plain", "Invalid month");
    return;
  }

  char path[80];
  const char *seriesKey = locations[locationIndex].pm2_5.getKey();

  if (seriesKey == nullptr ||
      !makeMonthPath(path, sizeof(path), seriesKey, year, month)) {
    server->send(500, "text/plain", "Failed to build path");
    return;
  }

  File file = LittleFS.open(path, FILE_READ);

  server->sendContent("{\"month\":\"");
  server->sendContent(monthText);
  server->sendContent("\",\"points\":[");

  bool firstPoint = true;

  if (file && !file.isDirectory()) {
    TimedValue reading{};

    while (true) {
      size_t n = file.read(reinterpret_cast<uint8_t *>(&reading), sizeof(reading));

      if (n != sizeof(reading)) {
        break;
      }

      if (!reading.hasValue() || !isfinite(reading.value)) {
        continue;
      }

      char timeBuf[24];

      if (!formatLocalTimestamp(reading.time, timeBuf, sizeof(timeBuf))) {
        continue;
      }

      char pointBuf[144];
      int written = snprintf(
        pointBuf,
        sizeof(pointBuf),
        "%s{\"ts\":%lu,\"label\":\"%s\",\"value\":%.3f}",
        firstPoint ? "" : ",",
        static_cast<unsigned long>(reading.time),
        timeBuf,
        reading.value
      );

      if (written > 0 && static_cast<size_t>(written) < sizeof(pointBuf)) {
        server->sendContent(pointBuf);
        firstPoint = false;
      }
    }
  }

  if (file) {
    file.close();
  }

  server->sendContent("]}");
}

static void renderViewPage() {
  if (server == nullptr) {
    return;
  }

  char currentMonth[8];
  if (!currentUtcMonthText(currentMonth, sizeof(currentMonth))) {
    snprintf(currentMonth, sizeof(currentMonth), "1970-01");
  }

  server->sendHeader("Cache-Control", "no-store, max-age=0");
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "text/html", "");

  server->sendContent(R"VIEWPAGE(
<!doctype html>
<html lang='en'>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<title>Time series view</title>
<style>
body{font-family:system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;background:linear-gradient(180deg,#eef4fb 0%,#f7f9fc 44%,#eef2f8 100%);color:#102033;}
main{max-width:1240px;margin:24px auto;padding:0 16px 36px;}
.top{display:flex;justify-content:space-between;align-items:flex-end;gap:16px;flex-wrap:wrap;margin-bottom:18px;}
h1{font-size:32px;line-height:1.1;margin:0 0 6px;letter-spacing:-0.02em;}
.toolbar{display:flex;gap:10px;flex-wrap:wrap;}
.card{background:rgba(255,255,255,.92);backdrop-filter:saturate(1.1) blur(4px);border:1px solid rgba(148,163,184,.18);border-radius:18px;box-shadow:0 18px 40px rgba(15,23,42,.08);padding:16px;}
.controls{margin-top:16px;}
.controls-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px;}
.control-box{background:#f8fbff;border:1px solid #e4ebf4;border-radius:16px;padding:14px;}
.control-head{display:flex;justify-content:space-between;gap:10px;align-items:flex-start;margin-bottom:10px;}
.control-name{font-size:16px;font-weight:700;color:#0f172a;}
.control-status{font-size:12px;color:#64748b;white-space:nowrap;}
label{display:block;font-size:12px;font-weight:700;letter-spacing:.04em;text-transform:uppercase;color:#52606d;margin-bottom:6px;}
input,select{width:100%;box-sizing:border-box;padding:10px 12px;border:1px solid #d8dee9;border-radius:10px;background:#fff;font:inherit;color:#102033;}
input:focus,select:focus{outline:none;border-color:#0b63ce;box-shadow:0 0 0 3px rgba(11,99,206,.12);}
.options{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px;margin-top:16px;}
.btn{display:inline-block;padding:10px 14px;border:0;border-radius:10px;background:#0b63ce;color:#fff;text-decoration:none;font-size:14px;font-weight:600;cursor:pointer;}
.btn:hover{text-decoration:none;background:#094f9c;}
.btn.secondary{background:#64748b;}
.btn.secondary:hover{background:#475569;}
.chart-area{display:grid;gap:16px;}
.chart-card{background:rgba(255,255,255,.96);border:1px solid rgba(148,163,184,.18);border-radius:20px;box-shadow:0 18px 40px rgba(15,23,42,.08);padding:16px;}
.chart-top{display:flex;justify-content:space-between;gap:16px;align-items:flex-start;flex-wrap:wrap;margin-bottom:12px;}
.chart-title{font-size:20px;font-weight:700;letter-spacing:-0.01em;color:#0f172a;}
.legend{display:flex;flex-wrap:wrap;gap:8px;justify-content:flex-end;}
.legend-item{display:flex;align-items:center;gap:8px;padding:7px 10px;border-radius:999px;background:#f8fafc;border:1px solid #e5e7eb;font-size:12px;color:#334155;}
.legend-toggle{appearance:none;-webkit-appearance:none;font:inherit;color:inherit;cursor:pointer;transition:opacity .15s ease,border-color .15s ease,background .15s ease,box-shadow .15s ease,transform .15s ease;}
.legend-toggle:hover{background:#eef4ff;border-color:#cbd5e1;}
.legend-toggle:focus{outline:none;box-shadow:0 0 0 3px rgba(11,99,206,.14);}
.legend-toggle.is-hidden{opacity:.45;}
.legend-toggle.is-hidden:hover{background:#f8fafc;}
.legend-toggle.is-hidden .legend-swatch{opacity:.55;}
.legend-swatch{width:10px;height:10px;border-radius:999px;flex:0 0 auto;}
.legend-month{color:#64748b;}
.legend-loading{color:#0b63ce;font-weight:600;}
.chart-shell{position:relative;height:360px;border-radius:16px;background:linear-gradient(180deg,#f8fbff 0%,#fdfefe 100%);border:1px solid #e7edf4;overflow:hidden;}
.chart-shell canvas{display:block;width:100%;height:100%;touch-action:none;}
.chart-tooltip{position:absolute;left:0;top:0;transform:translate3d(-9999px,-9999px,0);pointer-events:none;background:rgba(15,23,42,.96);color:#fff;padding:10px 12px;border-radius:12px;box-shadow:0 14px 30px rgba(15,23,42,.22);font-size:12px;line-height:1.45;min-width:180px;max-width:260px;}
.chart-tooltip .series{font-weight:700;margin-bottom:2px;}
.chart-tooltip .value{color:#dbeafe;}
.chart-tooltip .time{color:#cbd5e1;}
.chart-empty{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:#64748b;font-size:14px;background:repeating-linear-gradient(135deg,rgba(148,163,184,.08),rgba(148,163,184,.08) 12px,rgba(148,163,184,.04) 12px,rgba(148,163,184,.04) 24px);}
.chart-empty[hidden]{display:none;}
.chart-footer{margin-top:10px;color:#64748b;font-size:12px;line-height:1.5;display:grid;gap:4px;}
.stat-row{display:flex;flex-wrap:wrap;gap:8px;}
.stat-row.is-hidden{opacity:.45;}
.stat-name{font-weight:700;}
.stat-values{color:#64748b;}
.series-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;}
.series-control:nth-child(1){--series-color:#0b63ce;}
.series-control:nth-child(2){--series-color:#d97706;}
.series-control:nth-child(3){--series-color:#059669;}
.series-control{border-left:4px solid var(--series-color);}
.series-ready{color:#64748b;}
.series-pending{color:#0b63ce;}
@media (max-width:1000px){.controls-grid,.options,.series-grid{grid-template-columns:1fr;}.chart-shell{height:320px;}}
</style>
</head>
<body>
<main>
<div class='top'>
  <div>
    <h1>Time series view</h1>
  </div>
  <div class='toolbar'>
    <a class='btn secondary' href='/files'>Files</a>
    <a class='btn secondary' href='/config'>Config</a>
  </div>
</div>
<div id='chart-area' class='chart-area'></div>
<div class='card controls'>
  <div class='controls-grid'>
    <div class='control-box'>
      <label for='layout-select'>Plot layout</label>
      <select id='layout-select'>
        <option value='combined'>One plot</option>
        <option value='separate'>Separate plots</option>
      </select>
    </div>
    <div class='control-box'>
      <label for='style-select'>Plot style</label>
      <select id='style-select'>
        <option value='dots'>Dot plot</option>
        <option value='linear' selected>Linear interpolation</option>
        <option value='smooth'>Smooth interpolation</option>
      </select>
    </div>
  </div>
  <div class='series-grid' style='margin-top:16px;'>
)VIEWPAGE");

  for (uint8_t i = 0; i < MAX_LOCATIONS; i++) {
    renderViewLocationControl(i, currentMonth);
  }

  server->sendContent(R"VIEWPAGE(
  </div>
</div>
<script>
const SERIES_COLORS = ['#0b63ce', '#d97706', '#059669'];

const app = {
  layout: 'combined',
  style: 'linear',
  cache: new Map(),
  series: [],
  chartArea: null,
  layoutSelect: null,
  styleSelect: null
};

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, function(ch) {
    return ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[ch]);
  });
}

function formatValue(value) {
  if (!Number.isFinite(value)) {
    return 'n/a';
  }
  const fixed = value.toFixed(2);
  return fixed.replace(/\.?0+$/, '');
}

function statsFor(points) {
  const valid = points.filter(function(point) {
    return Number.isFinite(point.value);
  });

  if (!valid.length) {
    return null;
  }

  let sum = 0;
  let min = valid[0].value;
  let max = valid[0].value;

  for (const point of valid) {
    sum += point.value;
    if (point.value < min) min = point.value;
    if (point.value > max) max = point.value;
  }

  const avg = sum / valid.length;
  let variance = 0;

  for (const point of valid) {
    const delta = point.value - avg;
    variance += delta * delta;
  }

  return {
    avg: avg,
    min: min,
    max: max,
    std: Math.sqrt(variance / valid.length)
  };
}

function statsText(stats) {
  if (!stats) {
    return 'No samples for this month.';
  }

  return 'avg ' + formatValue(stats.avg) + ' ug/m3; min ' + formatValue(stats.min) +
    '; max ' + formatValue(stats.max) + '; std dev ' + formatValue(stats.std);
}

function sampleLabel(label) {
  if (!label) {
    return '';
  }
  return label.length >= 16 ? label.slice(5, 16) : label;
}

function buildSeriesState() {
  app.series = Array.from(document.querySelectorAll('.series-control')).map(function(card, index) {
    const input = card.querySelector('.month-input');
    const name = card.querySelector('.series-name');
    const status = card.querySelector('.series-status');

    return {
      index: index,
      card: card,
      name: name ? name.textContent.trim() : ('Location ' + (index + 1)),
      input: input,
      status: status,
      month: input ? input.value : '',
      hiddenInCombined: false,
      points: [],
      loading: false,
      requestToken: 0
    };
  });
}

function setSeriesStatus(series, text, loading) {
  if (!series.status) {
    return;
  }

  series.status.textContent = text;
  series.status.classList.toggle('series-pending', !!loading);
  series.status.classList.toggle('series-ready', !loading);
}

function formatMonthLabel(series) {
  return series.month ? series.month : 'unknown month';
}

function updateChartControls() {
  app.layout = app.layoutSelect.value;
  app.style = app.styleSelect.value;
  renderCharts();
}

function loadSeries(series) {
  if (!series.input) {
    return;
  }

  const month = series.input.value;
  series.month = month;
  const token = ++series.requestToken;
  const cacheKey = series.index + '|' + month;
  const cached = app.cache.get(cacheKey);

  if (cached) {
    series.points = cached.points;
    series.loading = false;
    setSeriesStatus(series, cached.points.length === 1 ? '1 sample' : (cached.points.length + ' samples'), false);
    renderCharts();
    return;
  }

  series.loading = true;
  setSeriesStatus(series, 'Loading...', true);
  renderCharts();

  fetch('/view-data?loc=' + encodeURIComponent(series.index) + '&month=' + encodeURIComponent(month), {cache: 'no-store'})
    .then(function(resp) {
      if (!resp.ok) {
        throw new Error('HTTP ' + resp.status);
      }
      return resp.json();
    })
    .then(function(data) {
      if (token !== series.requestToken) {
        return;
      }

      const points = Array.isArray(data.points) ? data.points.map(function(point) {
        return {
          ts: Number(point.ts),
          label: String(point.label || ''),
          value: Number(point.value)
        };
      }).filter(function(point) {
        return Number.isFinite(point.ts) && Number.isFinite(point.value) && point.label;
      }) : [];

      series.points = points;
      series.loading = false;
      app.cache.set(cacheKey, {points: points});
      setSeriesStatus(series, points.length === 1 ? '1 sample' : (points.length + ' samples'), false);
      renderCharts();
    })
    .catch(function(err) {
      if (token !== series.requestToken) {
        return;
      }

      series.points = [];
      series.loading = false;
      setSeriesStatus(series, 'Load failed', false);
      renderCharts();
      console.error(err);
    });
}

function allGroups() {
  if (app.layout === 'combined') {
    return [app.series];
  }

  return app.series.map(function(series) {
    return [series];
  });
}

function seriesColor(index) {
  return SERIES_COLORS[index % SERIES_COLORS.length];
}

function isSeriesVisible(series) {
  return app.layout !== 'combined' || !series.hiddenInCombined;
}

function visibleSeries(group) {
  return group.filter(function(series) {
    return isSeriesVisible(series);
  });
}

function buildLegend(group) {
  const interactive = app.layout === 'combined';

  return group.map(function(series) {
    const hidden = interactive && series.hiddenInCombined;
    const loading = series.loading ? "<span class='legend-loading'>loading</span>" : '';
    const toggleLabel = hidden ? ('Show ' + series.name) : ('Hide ' + series.name);
    const itemStart = interactive
      ? "<button type='button' class='legend-item legend-toggle" + (hidden ? " is-hidden" : "") + "' data-series='" + series.index + "' aria-pressed='" + (hidden ? 'false' : 'true') + "' aria-label='" + escapeHtml(toggleLabel) + "' title='" + escapeHtml(toggleLabel) + "'>"
      : "<div class='legend-item'>";
    const itemEnd = interactive ? "</button>" : "</div>";

    return (
      itemStart +
      "<span class='legend-swatch' style='background:" + seriesColor(series.index) + ";'></span>" +
      "<span>" + escapeHtml(series.name) + "</span>" +
      "<span class='legend-month'>" + escapeHtml(formatMonthLabel(series)) + "</span>" +
      loading +
      itemEnd
    );
  }).join('');
}

function buildFooter(group) {
  return group.map(function(series) {
    const hidden = app.layout === 'combined' && series.hiddenInCombined;
    const stats = statsFor(series.points);
    const values = series.loading && !series.points.length
      ? 'Loading samples...'
      : statsText(stats);
    return (
      "<div class='stat-row" + (hidden ? " is-hidden" : "") + "'>" +
      "<span class='stat-name' style='color:" + seriesColor(series.index) + ";'>" + escapeHtml(series.name) + "</span>" +
      "<span class='stat-values'>" + escapeHtml(values) + "</span>" +
      "</div>"
    );
  }).join('');
}

function buildChartHtml(group) {
  const combined = app.layout === 'combined';
  const title = combined ? '' : group[0].name;

  return (
    "<div class='chart-card'>" +
    "<div class='chart-top'>" +
    "<div>" +
    "<div class='chart-title'>" + escapeHtml(title) + "</div>" +
    "</div>" +
    "<div class='legend'>" + buildLegend(group) + "</div>" +
    "</div>" +
    "<div class='chart-shell'>" +
    "<canvas class='plot-canvas'></canvas>" +
    "<div class='chart-tooltip' hidden></div>" +
    "<div class='chart-empty' hidden>No samples for this selection.</div>" +
    "</div>" +
    "<div class='chart-footer'>" + buildFooter(group) + "</div>" +
    "</div>"
  );
}

function drawLineSeries(ctx, points, style, baselineY) {
  if (points.length < 2) {
    return;
  }

  if (style === 'smooth' && points.length >= 3) {
    ctx.beginPath();
    ctx.moveTo(points[0].x, points[0].y);

    for (let i = 0; i < points.length - 1; i++) {
      const p0 = points[i - 1] || points[i];
      const p1 = points[i];
      const p2 = points[i + 1];
      const p3 = points[i + 2] || p2;

      const cp1x = p1.x + (p2.x - p0.x) / 6;
      const cp1y = p1.y + (p2.y - p0.y) / 6;
      const cp2x = p2.x - (p3.x - p1.x) / 6;
      const cp2y = p2.y - (p3.y - p1.y) / 6;

      const steps = 16;

      for (let step = 1; step <= steps; step++) {
        const t = step / steps;
        const u = 1 - t;
        let x =
          u * u * u * p1.x +
          3 * u * u * t * cp1x +
          3 * u * t * t * cp2x +
          t * t * t * p2.x;
        let y =
          u * u * u * p1.y +
          3 * u * u * t * cp1y +
          3 * u * t * t * cp2y +
          t * t * t * p2.y;

        if (baselineY != null) {
          y = Math.min(y, baselineY);
        }

        ctx.lineTo(x, y);
      }
    }

    ctx.stroke();
    return;
  }

  ctx.beginPath();
  ctx.moveTo(points[0].x, points[0].y);

  for (let i = 1; i < points.length; i++) {
    ctx.lineTo(points[i].x, points[i].y);
  }

  ctx.stroke();
}

function computePlot(group, width, height) {
  const allPoints = [];
  const seriesPoints = group.map(function(series) {
    const coords = series.points.map(function(point) {
      return {
        ts: point.ts,
        value: point.value,
        label: point.label,
        x: 0,
        y: 0,
        series: series
      };
    });

    for (const point of coords) {
      allPoints.push(point);
    }

    return {
      series: series,
      points: coords
    };
  });

  if (!allPoints.length) {
    return {
      allPoints: allPoints,
      seriesPoints: seriesPoints,
      xMin: 0,
      xMax: 0,
      yMin: 0,
      yMax: 0
    };
  }

  allPoints.sort(function(a, b) {
    return a.ts - b.ts;
  });

  let xMin = allPoints[0].ts;
  let xMax = allPoints[allPoints.length - 1].ts;
  let yMin = allPoints[0].value;
  let yMax = allPoints[0].value;

  for (const point of allPoints) {
    if (point.value < yMin) yMin = point.value;
    if (point.value > yMax) yMax = point.value;
  }

  if (xMin === xMax) {
    xMin -= 12 * 60 * 60;
    xMax += 12 * 60 * 60;
  } else {
    const xPad = Math.max(1800, (xMax - xMin) * 0.03);
    xMin -= xPad;
    xMax += xPad;
  }

  yMin = 0;
  if (yMax <= 0) {
    yMax = 1;
  } else {
    const yPad = Math.max(1, yMax * 0.12);
    yMax += yPad;
  }

  const plotWidth = Math.max(1, width - 72);
  const plotHeight = Math.max(1, height - 56);
  const toX = function(ts) {
    return 52 + ((ts - xMin) / (xMax - xMin)) * plotWidth;
  };
  const toY = function(value) {
    return 18 + ((yMax - value) / (yMax - yMin)) * plotHeight;
  };

  for (const seriesEntry of seriesPoints) {
    for (const point of seriesEntry.points) {
      point.x = toX(point.ts);
      point.y = toY(point.value);
    }
  }

  return {
    allPoints: allPoints,
    seriesPoints: seriesPoints,
    xMin: xMin,
    xMax: xMax,
    yMin: yMin,
    yMax: yMax,
    toX: toX,
    toY: toY,
    width: width,
    height: height
  };
}

function drawChart(card, group) {
  const canvas = card.querySelector('.plot-canvas');
  const tooltip = card.querySelector('.chart-tooltip');
  const empty = card.querySelector('.chart-empty');
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  const width = Math.max(1, Math.round(rect.width));
  const height = Math.max(1, Math.round(rect.height));
  const targetWidth = Math.max(1, Math.round(width * dpr));
  const targetHeight = Math.max(1, Math.round(height * dpr));

  if (canvas.width !== targetWidth || canvas.height !== targetHeight) {
    canvas.width = targetWidth;
    canvas.height = targetHeight;
  }

  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);
  ctx.save();

  const displayedGroup = visibleSeries(group);
  const plot = computePlot(displayedGroup, width, height);
  card.__plot = plot;

  function showTooltip(point, clientX) {
    if (!point) {
      tooltip.hidden = true;
      return;
    }

    const tooltipTitle = escapeHtml(point.series.name);
    const tooltipTime = escapeHtml(point.label);
    const tooltipValue = escapeHtml(formatValue(point.value));

    tooltip.innerHTML = "<div class='series' style='color:" + seriesColor(point.series.index) + ";'>" + tooltipTitle +
      "</div><div class='time'>" + tooltipTime + "</div><div class='value'>" + tooltipValue + " ug/m3</div>";
    tooltip.hidden = false;

    const left = Math.max(8, Math.min(width - 220, clientX + 12));
    tooltip.style.transform = 'translate3d(' + left + 'px, 12px, 0)';
  }

  function nearestPoint(clientX) {
    let nearest = null;
    let bestDistance = Infinity;

    for (const point of plot.allPoints) {
      const distance = Math.abs(point.x - clientX);
      if (distance < bestDistance) {
        bestDistance = distance;
        nearest = point;
      }
    }

    return nearest;
  }

  function handleMove(event) {
    if (!plot.allPoints.length) {
      return;
    }

    const rect = canvas.getBoundingClientRect();
    const clientX = event.clientX - rect.left;
    const point = nearestPoint(clientX);
    card.__hoverPoint = point;
    drawChart(card, group);
    showTooltip(point, clientX);
  }

  function handleLeave() {
    card.__hoverPoint = null;
    tooltip.hidden = true;
    drawChart(card, group);
  }

  function handleLegendClick(event) {
    if (app.layout !== 'combined') {
      return;
    }

    const target = event.target;
    const button = target && target.closest ? target.closest('.legend-toggle') : null;

    if (!button || !card.contains(button)) {
      return;
    }

    const seriesIndex = Number(button.getAttribute('data-series'));
    if (!Number.isFinite(seriesIndex)) {
      return;
    }

    const series = group[seriesIndex];
    if (!series) {
      return;
    }

    event.preventDefault();
    event.stopPropagation();
    series.hiddenInCombined = !series.hiddenInCombined;
    renderCharts();
  }

  if (!card.__handlersAttached) {
    canvas.addEventListener('pointermove', handleMove);
    canvas.addEventListener('pointerleave', handleLeave);
    card.addEventListener('click', handleLegendClick);
    card.__handlersAttached = true;
  }

  if (!displayedGroup.length) {
    empty.hidden = false;
    empty.textContent = 'No visible series selected.';
    tooltip.hidden = true;
    ctx.restore();
    return;
  }

  if (!plot.allPoints.length) {
    empty.hidden = false;
    empty.textContent = displayedGroup.some(function(series) { return series.loading; }) ? 'Loading samples...' : 'No samples for this selection.';
    tooltip.hidden = true;
    ctx.restore();
    return;
  }

  empty.hidden = true;

  const padLeft = 52;
  const padRight = 20;
  const padTop = 18;
  const padBottom = 38;
  const plotW = width - padLeft - padRight;
  const plotH = height - padTop - padBottom;
  const xSpan = plot.xMax - plot.xMin;
  const ySpan = plot.yMax - plot.yMin;

  ctx.lineWidth = 1;
  ctx.strokeStyle = '#dbe3ee';
  ctx.fillStyle = '#334155';
  ctx.font = '11px system-ui,-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif';
  ctx.textBaseline = 'middle';

  ctx.beginPath();
  for (let i = 0; i <= 5; i++) {
    const y = padTop + (plotH * i / 5);
    ctx.moveTo(padLeft, y);
    ctx.lineTo(width - padRight, y);
  }
  ctx.stroke();

  ctx.strokeStyle = '#cdd7e3';
  ctx.beginPath();
  ctx.moveTo(padLeft, padTop);
  ctx.lineTo(padLeft, height - padBottom);
  ctx.lineTo(width - padRight, height - padBottom);
  ctx.stroke();

  for (let i = 0; i <= 5; i++) {
    const value = plot.yMax - ((plot.yMax - plot.yMin) * i / 5);
    const y = padTop + (plotH * i / 5);
    ctx.fillText(formatValue(value), 6, y);
  }

  const tickPoints = [];
  const seenTicks = new Set();
  const sortedTicks = plot.allPoints.slice().sort(function(a, b) { return a.ts - b.ts; });
  const tickCount = Math.min(6, sortedTicks.length);

  for (let i = 0; i < tickCount; i++) {
    const index = tickCount === 1 ? 0 : Math.round((sortedTicks.length - 1) * i / (tickCount - 1));
    const point = sortedTicks[index];
    const key = point.ts + '|' + point.label;
    if (seenTicks.has(key)) {
      continue;
    }
    seenTicks.add(key);
    tickPoints.push(point);
  }

  ctx.textBaseline = 'top';
  for (const point of tickPoints) {
    const x = plot.toX(point.ts);
    ctx.strokeStyle = '#cdd7e3';
    ctx.beginPath();
    ctx.moveTo(x, height - padBottom);
    ctx.lineTo(x, height - padBottom + 4);
    ctx.stroke();
    ctx.fillStyle = '#64748b';
    ctx.fillText(sampleLabel(point.label), Math.max(0, Math.min(width - 40, x - 22)), height - padBottom + 7);
  }

  const shouldDrawDots = app.style === 'dots';
  const baselineY = plot.toY(0);

  for (const seriesEntry of plot.seriesPoints) {
    const series = seriesEntry.series;
    const points = seriesEntry.points;
    if (!points.length) {
      continue;
    }

    ctx.strokeStyle = seriesColor(series.index);
    ctx.fillStyle = seriesColor(series.index);
    ctx.lineWidth = 2.4;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';

    if (app.style !== 'dots') {
      drawLineSeries(ctx, points, app.style, baselineY);
    }

    if (shouldDrawDots) {
      for (const point of points) {
        ctx.beginPath();
        ctx.arc(point.x, point.y, 2.2, 0, Math.PI * 2);
        ctx.fill();
      }
    }
  }

  const hoverPoint = card.__hoverPoint || null;
  const activeHoverPoint = hoverPoint && isSeriesVisible(hoverPoint.series) ? hoverPoint : null;

  if (activeHoverPoint) {
    ctx.strokeStyle = 'rgba(15,23,42,.22)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(activeHoverPoint.x, padTop);
    ctx.lineTo(activeHoverPoint.x, height - padBottom);
    ctx.stroke();

    ctx.fillStyle = seriesColor(activeHoverPoint.series.index);
    ctx.beginPath();
    ctx.arc(activeHoverPoint.x, activeHoverPoint.y, 4.4, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = '#fff';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(activeHoverPoint.x, activeHoverPoint.y, 7, 0, Math.PI * 2);
    ctx.stroke();
  }

  ctx.restore();
}

function renderCharts() {
  const groups = allGroups();

  app.chartArea.innerHTML = groups.map(function(group) {
    return buildChartHtml(group);
  }).join('');

  const cards = Array.from(app.chartArea.querySelectorAll('.chart-card'));

  cards.forEach(function(card, index) {
    drawChart(card, groups[index]);
  });
}

function initViewPage() {
  app.chartArea = document.getElementById('chart-area');
  app.layoutSelect = document.getElementById('layout-select');
  app.styleSelect = document.getElementById('style-select');

  buildSeriesState();

  app.layoutSelect.addEventListener('change', updateChartControls);
  app.styleSelect.addEventListener('change', updateChartControls);

  for (const series of app.series) {
    if (!series.input) {
      continue;
    }

    series.input.addEventListener('change', function() {
      loadSeries(series);
    });
    loadSeries(series);
  }

  window.addEventListener('resize', function() {
    renderCharts();
  });

  renderCharts();
}

document.addEventListener('DOMContentLoaded', initViewPage);
</script>
</main>
</body>
</html>
)VIEWPAGE");
}

static void handleView() {
  if (server == nullptr) {
    return;
  }

  renderViewPage();
}

static void handleViewData() {
  if (server == nullptr) {
    return;
  }

  uint32_t locationIndex = 0;

  if (server->hasArg("loc")) {
    if (!parseRequestUInt("loc", locationIndex)) {
      server->send(400, "text/plain", "Invalid location");
      return;
    }
  }

  if (locationIndex >= MAX_LOCATIONS) {
    server->send(400, "text/plain", "Invalid location");
    return;
  }

  char monthText[8];
  int year = 0;
  int month = 0;

  if (server->hasArg("month")) {
    String monthArg = server->arg("month");
    if (!parseMonthText(monthArg.c_str(), year, month)) {
      server->send(400, "text/plain", "Invalid month");
      return;
    }

    snprintf(monthText, sizeof(monthText), "%04d-%02d", year, month);
  } else {
    if (!currentUtcMonthText(monthText, sizeof(monthText))) {
      server->send(500, "text/plain", "Failed to determine month");
      return;
    }
  }

  server->sendHeader("Cache-Control", "no-store, max-age=0");
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  sendViewDataJson(static_cast<uint8_t>(locationIndex), monthText);
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
    "<a class='btn secondary' href='/view'>View</a>"
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
    "<a class='btn' href='/view'>View</a>"
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
