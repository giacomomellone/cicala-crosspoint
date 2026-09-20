#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ota_release {
struct Version {
  uint32_t parts[3]{};
  bool candidate = false;
};

inline bool parseVersion(const char* text, Version& out, bool allowBuildSuffix = false) {
  if (!text || !*text) return false;
  Version value;
  for (unsigned part = 0; part < 3; ++part) {
    if (*text < '0' || *text > '9') return false;
    do {
      const unsigned digit = *text++ - '0';
      if (value.parts[part] > (UINT32_MAX - digit) / 10) return false;
      value.parts[part] = value.parts[part] * 10 + digit;
    } while (*text >= '0' && *text <= '9');
    if (part < 2 && *text++ != '.') return false;
  }
  if (*text) {
    if (!allowBuildSuffix || (*text != '-' && *text != '+')) return false;
    value.candidate = strncmp(text, "-rc", 3) == 0 && (text[3] == '\0' || text[3] == '+' || text[3] == '.');
    ++text;
    if (!*text) return false;
    for (; *text; ++text) {
      if (!((*text >= '0' && *text <= '9') || (*text >= 'a' && *text <= 'z') || (*text >= 'A' && *text <= 'Z') ||
            *text == '.' || *text == '-' || *text == '+'))
        return false;
    }
  }
  out = value;
  return true;
}

inline bool newerStable(const char* current, const char* latest) {
  Version from, to;
  if (!parseVersion(current, from, true) || !parseVersion(latest, to)) return false;
  for (unsigned part = 0; part < 3; ++part)
    if (from.parts[part] != to.parts[part]) return to.parts[part] > from.parts[part];
  return from.candidate;
}

inline bool assetName(char* out, size_t capacity, const char* prefix, const char* tag, const char* suffix) {
  Version version;
  if (!parseVersion(tag, version)) return false;
  const int size = snprintf(out, capacity, "%s-%s%s.bin", prefix, tag, suffix);
  return size > 0 && static_cast<size_t>(size) < capacity;
}
}  // namespace ota_release
