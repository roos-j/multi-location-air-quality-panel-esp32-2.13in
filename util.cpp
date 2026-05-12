#include "util.h"

#include <LittleFS.h>
#include <time.h>

namespace {

constexpr const char *tsDir = "/ts";

int monthIndex(int year, int month) {
  return year * 12 + (month - 1);
}

bool monthIndexFromTimestamp(uint32_t timestamp, int &outMonthIndex) {
  if (timestamp == 0) {
    return false;
  }

  time_t raw = static_cast<time_t>(timestamp);
  struct tm timeinfo;

  gmtime_r(&raw, &timeinfo);

  int year = timeinfo.tm_year + 1900;
  int month = timeinfo.tm_mon + 1;

  outMonthIndex = monthIndex(year, month);
  return true;
}

bool normalizeTsPath(char *out, size_t outSize, const char *name) {
  if (out == nullptr || outSize == 0 || name == nullptr || name[0] == '\0') {
    return false;
  }

  int n;

  if (name[0] == '/') {
    n = snprintf(out, outSize, "%s", name);
  } else {
    n = snprintf(out, outSize, "%s/%s", tsDir, name);
  }

  return n > 0 && static_cast<size_t>(n) < outSize;
}

bool monthIndexFromPath(const char *path, int &outMonthIndex) {
  if (path == nullptr) {
    return false;
  }

  const char *dot = strrchr(path, '.');
  if (dot == nullptr || strcmp(dot, ".bin") != 0) {
    return false;
  }

  const char *underscore = strrchr(path, '_');
  if (underscore == nullptr) {
    return false;
  }

  const char *ym = underscore + 1;

  // Need exactly YYYYMM before ".bin".
  if (dot - ym != 6) {
    return false;
  }

  for (int i = 0; i < 6; i++) {
    if (ym[i] < '0' || ym[i] > '9') {
      return false;
    }
  }

  int year =
    (ym[0] - '0') * 1000 +
    (ym[1] - '0') * 100 +
    (ym[2] - '0') * 10 +
    (ym[3] - '0');

  int month =
    (ym[4] - '0') * 10 +
    (ym[5] - '0');

  if (month < 1 || month > 12) {
    return false;
  }

  outMonthIndex = monthIndex(year, month);
  return true;
}

}  // namespace

bool TsStore::begin() {
  if (!LittleFS.begin(true)) {
    return false;
  }

  LittleFS.mkdir(tsDir);
  return true;
}

bool TsStore::append(const char *seriesKey, const TimedValue &reading) {
  if (!reading.hasValue()) {
    return false;
  }

  char path[64];

  if (!makePath(path, sizeof(path), seriesKey, reading.time)) {
    return false;
  }

  File file = LittleFS.open(path, FILE_APPEND);
  if (!file) {
    return false;
  }

  size_t written = file.write(
    reinterpret_cast<const uint8_t *>(&reading),
    sizeof(reading)
  );

  file.close();

  return written == sizeof(reading);
}

uint64_t TsStore::totalSize() {
  File dir = LittleFS.open(tsDir);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }

    return 0;
  }

  uint64_t total = 0;

  File file = dir.openNextFile();

  while (file) {
    if (!file.isDirectory()) {
      total += file.size();
    }

    file.close();
    file = dir.openNextFile();
  }

  dir.close();

  return total;
}

/** Delete all files older than cutoff time. */
size_t TsStore::prune(uint32_t cutoffTime) {
  int cutoffMonth;

  if (!monthIndexFromTimestamp(cutoffTime, cutoffMonth)) {
    return 0;
  }

  File dir = LittleFS.open(tsDir);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }

    return 0;
  }

  size_t deleted = 0;

  File file = dir.openNextFile();

  while (file) {
    char path[96];
    bool shouldDelete = false;

    if (!file.isDirectory() &&
        normalizeTsPath(path, sizeof(path), file.name())) {
      int fileMonth;

      if (monthIndexFromPath(path, fileMonth) && fileMonth < cutoffMonth) {
        shouldDelete = true;
      }
    }

    file.close();

    if (shouldDelete) {
      if (LittleFS.remove(path)) {
        deleted++;
      }
    }

    file = dir.openNextFile();
  }

  dir.close();

  return deleted;
}

bool TsStore::makePath(char *out, size_t outSize, const char *seriesKey, uint32_t timestamp) {
  if (out == nullptr || outSize == 0 || seriesKey == nullptr || timestamp == 0) {
    return false;
  }

  time_t raw = static_cast<time_t>(timestamp);
  struct tm timeinfo;

  // UTC month grouping
  gmtime_r(&raw, &timeinfo);

  int year = timeinfo.tm_year + 1900;
  int month = timeinfo.tm_mon + 1;

  int n = snprintf(
    out,
    outSize,
    "%s/%s_%04d%02d.bin",
    tsDir,
    seriesKey,
    year,
    month
  );

  return n > 0 && static_cast<size_t>(n) < outSize;
}

void formatBytes(size_t bytes, char *out, size_t outSize) {
  if (bytes < 1024) {
    snprintf(out, outSize, "%u B", (unsigned)bytes);
  } else if (bytes < 1024UL * 1024UL) {
    snprintf(out, outSize, "%.1f KB", bytes / 1024.0f);
  } else {
    snprintf(out, outSize, "%.2f MB", bytes / (1024.0f * 1024.0f));
  }
}
