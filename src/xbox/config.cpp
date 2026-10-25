// SkyPortal360 - skyportal.ini reader. See config.h.
#include "config.h"

#include <stdlib.h>

#include "util.h"

namespace {

char* Trim(char* s) {
  while (*s == ' ' || *s == '\t') ++s;
  char* end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
    *--end = 0;
  }
  return s;
}

void Copy(char* dst, size_t cap, const char* src) { strncpy_s(dst, cap, src, _TRUNCATE); }

}  // namespace

Config::Config() : count_(0) { memset(entries_, 0, sizeof(entries_)); }

bool Config::Load(const char* path) {
  count_ = 0;
  static char buf[16 * 1024];
  const int n = util::ReadAll(path, buf, sizeof(buf) - 1);
  if (n < 0) return false;
  buf[n] = 0;

  char section[kSectionLen] = "";
  char* line = buf;
  while (line && *line) {
    char* next = strchr(line, '\n');
    if (next) *next++ = 0;
    char* s = Trim(line);
    // UTF-8 BOM on the first line (Notepad).
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) {
      s += 3;
    }
    if (*s == '[') {
      char* close = strchr(s, ']');
      if (close) {
        *close = 0;
        Copy(section, sizeof(section), Trim(s + 1));
      }
    } else if (*s && *s != ';' && *s != '#') {
      char* eq = strchr(s, '=');
      if (eq && count_ < kMaxEntries) {
        *eq = 0;
        char* value = Trim(eq + 1);
        // Strip a trailing "; comment" (values never contain ';').
        char* semi = strchr(value, ';');
        if (semi) {
          *semi = 0;
          value = Trim(value);
        }
        Entry& e = entries_[count_++];
        Copy(e.section, sizeof(e.section), section);
        Copy(e.key, sizeof(e.key), Trim(s));
        Copy(e.value, sizeof(e.value), value);
      }
    }
    line = next;
  }
  return true;
}

const char* Config::Get(const char* section, const char* key, const char* fallback) const {
  for (int i = count_ - 1; i >= 0; --i) {  // last one wins
    if (!_stricmp(entries_[i].section, section) && !_stricmp(entries_[i].key, key)) {
      return entries_[i].value;
    }
  }
  return fallback;
}

int Config::GetInt(const char* section, const char* key, int fallback) const {
  const char* v = Get(section, key, NULL);
  if (!v || !*v) return fallback;
  return (int)strtoul(v, NULL, 0);
}

bool Config::GetBool(const char* section, const char* key, bool fallback) const {
  const char* v = Get(section, key, NULL);
  if (!v || !*v) return fallback;
  if (!_stricmp(v, "1") || !_stricmp(v, "true") || !_stricmp(v, "yes") || !_stricmp(v, "on")) {
    return true;
  }
  if (!_stricmp(v, "0") || !_stricmp(v, "false") || !_stricmp(v, "no") || !_stricmp(v, "off")) {
    return false;
  }
  return fallback;
}

bool Config::Value(const char* section, int index, const char** key, const char** value) const {
  int seen = 0;
  for (int i = 0; i < count_; ++i) {
    if (_stricmp(entries_[i].section, section)) continue;
    if (seen++ == index) {
      *key = entries_[i].key;
      *value = entries_[i].value;
      return true;
    }
  }
  return false;
}
