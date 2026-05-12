#include "portal.h"
#include "app.h"
#include <WebServer.h>
#include <LittleFS.h>

WebServer *server = nullptr;


/** Portal mode opens a wifi server and stays active until dismissed */
void setupPortalMode() {
  Serial0.println("Starting portal mode...");

  startFileServer();
  showPortalScreen();

  while (true) {
    server->handleClient();

    if (digitalRead(EXIT_KEY) == LOW) {
      //delay(250);
      //goToDeepSleep(SLEEP_SECONDS);
      displayClear();
      displayDrawCenteredString((SCR_HEIGHT - 24) / 2, "One moment...", BLACK, 24);
      displayUpdate();
      setupNormalMode();
    }

    delay(5);
  }
}

void showPortalScreen() {
  char linebuf[128];

  int blockHeight = 30 * 2;
  int startY = (SCR_HEIGHT - blockHeight) / 2;

  snprintf(linebuf, sizeof(linebuf), "SSID: %s", PORTAL_SSID);
  displayDrawCenteredString(startY, linebuf, BLACK, 24);

  snprintf(linebuf, sizeof(linebuf), "Password: %s", PORTAL_PWD);
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
  server->on("/download", HTTP_GET, handleDownload);
  server->on("/delete", HTTP_POST, handleDeleteFile);
  server->on("/delete-all", HTTP_POST, handleDeleteAll);

  server->begin();

  Serial0.print("File server: http://");
  Serial0.print(WiFi.softAPIP());
  Serial0.println("/files");
}

static void redirectToFiles() {
  server->sendHeader("Location", "/files");
  server->send(303, "text/plain", "");
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

  if (arg.length() == 0 || arg.length() >= outSize) {
    return false;
  }

  arg.toCharArray(out, outSize);
  return true;
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
