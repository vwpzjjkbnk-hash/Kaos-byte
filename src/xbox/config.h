// SkyPortal360 - skyportal.ini reader (no Win32 profile API on the 360, so a tiny parser).
#pragma once

#include "stdafx.h"

class Config {
 public:
  Config();
  bool Load(const char* path);  // false if the file is missing (defaults then apply)

  const char* Get(const char* section, const char* key, const char* fallback) const;
  int GetInt(const char* section, const char* key, int fallback) const;  // dec or 0x-hex
  bool GetBool(const char* section, const char* key, bool fallback) const;

  // Iterates the values of one section in file order: returns false past the end.
  bool Value(const char* section, int index, const char** key, const char** value) const;

 private:
  enum { kMaxEntries = 160, kSectionLen = 24, kKeyLen = 32, kValueLen = 192 };
  struct Entry {
    char section[kSectionLen];
    char key[kKeyLen];
    char value[kValueLen];
  };
  Entry entries_[kMaxEntries];
  int count_;
};
