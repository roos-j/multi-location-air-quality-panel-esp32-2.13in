#ifndef UTIL_H
#define UTIL_H

#include <Preferences.h>

/** Tracks a property stored in NVS. Keeps a dirty flag to avoid redundant writes. */
template <typename T>
class NvsProp {
  static_assert(std::is_trivially_copyable<T>::value, "NvsProp<T> requires a trivially copyable type");

public:
  explicit NvsProp(const char *key) : key_(key) {}

  bool load(Preferences &prefs, const T &defaultValue) {
    if (prefs.getBytesLength(key_) != sizeof(T)) {
      value_ = defaultValue;
      dirty_ = false;
      return false;
    }

    T tmp{};
    size_t n = prefs.getBytes(key_, &tmp, sizeof(T));

    if (n != sizeof(T)) {
      value_ = defaultValue;
      dirty_ = false;
      return false;
    }

    value_ = tmp;
    dirty_ = false;
    return true;
  }

  bool save(Preferences &prefs) {
    if (!dirty_) {
      return true;
    }

    size_t n = prefs.putBytes(key_, &value_, sizeof(T));

    if (n == sizeof(T)) {
      dirty_ = false;
      return true;
    }

    return false;
  }

  bool set(const T &newValue) {
    if (value_ == newValue) {
      return false;
    }
    value_ = newValue;
    dirty_ = true;
    return true;
  }

  const T &get() const {
    return value_;
  }

  const char *getKey() const {
    return key_;
  }

  bool isDirty() const {
    return dirty_;
  }

private:
  const char *key_;
  T value_{};
  bool dirty_ = false;
};

/** Contains a single sensor value with a timestamp */
struct TimedValue {
  uint32_t time;
  float value;

  bool hasValue() const {
    return time != 0;
  }

  bool operator==(const TimedValue &other) const {
    return time == other.time &&
           value == other.value;
  }
};

/** Manage time series storage */
class TsStore {
public:
  bool begin();

  bool append(const char *seriesKey, const TimedValue &value);

  uint64_t totalSize();

  size_t prune(uint32_t cutoffTime);

  bool makePath(char *out, size_t outSize, const char *seriesKey, uint32_t timestamp);
};

#endif