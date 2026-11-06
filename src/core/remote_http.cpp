// Project Kaos-Byte - the phone remote's HTTP helpers. See remote_http.h.
#include "remote_http.h"

#include <string.h>

namespace rh {

namespace {

int HexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool EqualsNoCase(const char* a, const char* b) {
  for (; *a && *b; ++a, ++b) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
    if (x != y) return false;
  }
  return *a == *b;
}

bool StartsWith(const char* s, const char* prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

// Decimal digits of `value` into out (no printf: the XDK's C library has no snprintf).
void FormatUnsigned(unsigned value, char* out) {
  char tmp[12];
  int n = 0;
  do {
    tmp[n++] = (char)('0' + value % 10);
    value /= 10;
  } while (value);
  for (int i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
  out[n] = 0;
}

// Appends text to out (cap bytes, always terminated); returns the new length.
int Append(char* out, int cap, int len, const char* text) {
  while (*text && len < cap - 1) out[len++] = *text++;
  if (cap > 0) out[len < cap ? len : cap - 1] = 0;
  return len;
}

}  // namespace

bool ParseRequestLine(const char* request, int len, char* method, int method_cap, char* path,
                      int path_cap, char* query, int query_cap) {
  if (!request || len <= 0 || method_cap < 2 || path_cap < 2 || query_cap < 1) return false;
  int i = 0, n = 0;
  while (i < len && request[i] != ' ' && request[i] != '\r' && request[i] != '\n') {
    if (n >= method_cap - 1) return false;
    method[n++] = request[i++];
  }
  method[n] = 0;
  if (!n || i >= len || request[i] != ' ') return false;
  ++i;
  if (i >= len || request[i] != '/') return false;
  n = 0;
  while (i < len && request[i] != ' ' && request[i] != '?' && request[i] != '\r' && request[i] != '\n') {
    if (n >= path_cap - 1) return false;
    path[n++] = request[i++];
  }
  path[n] = 0;
  n = 0;
  if (i < len && request[i] == '?') {
    ++i;
    while (i < len && request[i] != ' ' && request[i] != '\r' && request[i] != '\n') {
      if (n >= query_cap - 1) return false;
      query[n++] = request[i++];
    }
  }
  query[n] = 0;
  if (i >= len || request[i] != ' ') return false;  // "HTTP/1.x" must follow
  UrlDecode(path, false);
  return true;
}

void UrlDecode(char* s, bool plus_is_space) {
  char* out = s;
  for (const char* p = s; *p; ++p) {
    if (*p == '%' && HexValue(p[1]) >= 0 && HexValue(p[2]) >= 0) {
      const int v = HexValue(p[1]) * 16 + HexValue(p[2]);
      *out++ = v ? (char)v : '_';  // no NULs smuggled into names
      p += 2;
    } else if (*p == '+' && plus_is_space) {
      *out++ = ' ';
    } else {
      *out++ = *p;
    }
  }
  *out = 0;
}

bool QueryValue(const char* query, const char* key, char* out, int cap) {
  if (!query || !key || cap < 1) return false;
  const int klen = (int)strlen(key);
  const char* p = query;
  while (*p) {
    const char* end = strchr(p, '&');
    if (!end) end = p + strlen(p);
    if (end - p >= klen && !strncmp(p, key, klen) && (p[klen] == '=' || p + klen == end)) {
      const char* v = p + klen + (p[klen] == '=' ? 1 : 0);
      int n = (int)(end - v);
      if (n > cap - 1) n = cap - 1;
      memcpy(out, v, n);
      out[n] = 0;
      UrlDecode(out, true);
      return true;
    }
    p = *end ? end + 1 : end;
  }
  return false;
}

int QueryInt(const char* query, const char* key, int fallback) {
  char v[16];
  if (!QueryValue(query, key, v, sizeof(v)) || !v[0]) return fallback;
  int sign = 1, i = 0, value = 0;
  if (v[0] == '-') {
    sign = -1;
    i = 1;
  }
  if (!v[i]) return fallback;
  for (; v[i]; ++i) {
    if (v[i] < '0' || v[i] > '9' || value > 100000000) return fallback;
    value = value * 10 + (v[i] - '0');
  }
  return sign * value;
}

bool SafeFilePath(const char* url_path, char* out, int cap) {
  if (!url_path || url_path[0] != '/') return false;
  const char* p = url_path + 1;
  if (!StartsWith(p, "UI/") && !StartsWith(p, "Images/") && !StartsWith(p, "Phone/")) return false;
  int n = 0;
  char prev = 0;
  for (; *p; ++p) {
    const char c = *p;
    // Printable characters only; no drive letters, no backslash games, no "..".
    if ((unsigned char)c < 0x20 || c == ':' || c == '\\' || c == '*' || c == '?' || c == '"' ||
        c == '<' || c == '>' || c == '|') {
      return false;
    }
    if (c == '.' && prev == '.') return false;
    if (c == '/' && prev == '/') return false;
    if (n >= cap - 1) return false;
    out[n++] = c == '/' ? '\\' : c;
    prev = c;
  }
  out[n] = 0;
  return n > 0 && out[n - 1] != '\\';
}

const char* ContentType(const char* path) {
  const char* dot = strrchr(path, '.');
  if (!dot) return "application/octet-stream";
  if (EqualsNoCase(dot, ".html") || EqualsNoCase(dot, ".htm")) return "text/html; charset=utf-8";
  if (EqualsNoCase(dot, ".js")) return "text/javascript; charset=utf-8";
  if (EqualsNoCase(dot, ".css")) return "text/css; charset=utf-8";
  if (EqualsNoCase(dot, ".json")) return "application/json";
  if (EqualsNoCase(dot, ".png")) return "image/png";
  if (EqualsNoCase(dot, ".jpg") || EqualsNoCase(dot, ".jpeg")) return "image/jpeg";
  if (EqualsNoCase(dot, ".svg")) return "image/svg+xml";
  if (EqualsNoCase(dot, ".txt") || EqualsNoCase(dot, ".ini")) return "text/plain; charset=utf-8";
  return "application/octet-stream";  // .qoi: decoded by the page
}

Json::Json(char* buf, int cap) : buf_(buf), cap_(cap), len_(0), overflow_(false) {
  if (cap_ > 0) buf_[0] = 0;
}

void Json::Put(char c) {
  if (len_ < cap_ - 1) {
    buf_[len_++] = c;
    buf_[len_] = 0;
  } else {
    overflow_ = true;
  }
}

void Json::Raw(const char* text) {
  for (; text && *text; ++text) Put(*text);
}

void Json::Char(char c) { Put(c); }

void Json::Str(const char* text) {
  static const char kHex[] = "0123456789abcdef";
  Put('"');
  for (; text && *text; ++text) {
    const unsigned char c = (unsigned char)*text;
    if (c == '"' || c == '\\') {
      Put('\\');
      Put((char)c);
    } else if (c < 0x20) {
      Put('\\');
      Put('u');
      Put('0');
      Put('0');
      Put(kHex[c >> 4]);
      Put(kHex[c & 15]);
    } else if (c >= 0x80) {
      // The names are ASCII; anything else (a stray Latin-1 byte) becomes its code point.
      Put('\\');
      Put('u');
      Put('0');
      Put('0');
      Put(kHex[c >> 4]);
      Put(kHex[c & 15]);
    } else {
      Put((char)c);
    }
  }
  Put('"');
}

void Json::PathStr(const char* text) {
  char tmp[300];
  int n = 0;
  for (; text && *text && n < (int)sizeof(tmp) - 1; ++text) tmp[n++] = *text == '\\' ? '/' : *text;
  tmp[n] = 0;
  Str(tmp);
}

void Json::Int(int value) {
  char tmp[16];
  if (value < 0) {
    Put('-');
    FormatUnsigned(0u - (unsigned)value, tmp);
  } else {
    FormatUnsigned((unsigned)value, tmp);
  }
  Raw(tmp);
}

int HeadersEnd(const char* buf, int len) {
  for (int i = 0; i + 3 < len; ++i) {
    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') return i + 4;
  }
  return -1;
}

bool WantsClose(const char* request, int len) {
  // Each header line: "Connection: ... close ..." (any case).
  for (int i = 0; i < len;) {
    int end = i;
    while (end < len && request[end] != '\n') ++end;
    static const char kName[] = "connection:";
    bool match = end - i > 11;
    for (int k = 0; match && k < 11; ++k) {
      char c = request[i + k];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if (c != kName[k]) match = false;
    }
    if (match) {
      for (int k = i + 11; k + 5 <= end; ++k) {
        char w[6];
        for (int m = 0; m < 5; ++m) {
          char c = request[k + m];
          w[m] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
        w[5] = 0;
        if (!strcmp(w, "close")) return true;
      }
    }
    i = end + 1;
  }
  return false;
}

int ResponseHeader(char* out, int cap, int status, const char* content_type, unsigned length,
                   Cache cache, bool keep_alive) {
  const char* reason = status == 200 ? "OK" : status == 404 ? "Not Found" : status == 400 ? "Bad Request"
                       : status == 405 ? "Method Not Allowed" : status == 503 ? "Service Unavailable"
                       : "Error";
  char number[12];
  int len = Append(out, cap, 0, "HTTP/1.1 ");
  FormatUnsigned((unsigned)status, number);
  len = Append(out, cap, len, number);
  len = Append(out, cap, len, " ");
  len = Append(out, cap, len, reason);
  len = Append(out, cap, len, "\r\nContent-Type: ");
  len = Append(out, cap, len, content_type);
  len = Append(out, cap, len, "\r\nContent-Length: ");
  FormatUnsigned(length, number);
  len = Append(out, cap, len, number);
  len = Append(out, cap, len, "\r\nCache-Control: ");
  len = Append(out, cap, len, cache == kCacheWeek ? "max-age=604800" : cache == kCacheDay ? "max-age=86400" : "no-store");
  len = Append(out, cap, len, keep_alive ? "\r\nConnection: keep-alive\r\nKeep-Alive: timeout=60\r\n\r\n"
                                         : "\r\nConnection: close\r\n\r\n");
  return len;
}

}  // namespace rh
