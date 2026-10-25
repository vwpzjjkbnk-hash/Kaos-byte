// SkyPortal360 open toolchain: a small vsnprintf. Covers what the plugin formats: %d %i %u %x %X
// %p %s %c %%, flags '-' and '0', a width, a precision for %s, and the h/l/ll size prefixes.
// No floating point (the open build is soft-float and never formats floats). Portable C++, also
// built natively by tests/test_xrt.cpp.
#include <stdio.h>
#include <string.h>

namespace {

struct Out {
  char* buf;
  size_t cap;
  size_t len;  // characters that would have been written
  void Put(char c) {
    if (cap && len < cap - 1) buf[len] = c;
    ++len;
  }
};

// Unsigned 64-bit to text without 64-bit division helpers when the value fits in 32 bits.
int Digits(unsigned long long v, unsigned base, bool upper, char* tmp) {
  const char* set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  int n = 0;
  if (v == 0) tmp[n++] = '0';
  while (v) {
    if (v <= 0xFFFFFFFFull) {
      unsigned int w = (unsigned int)v;
      while (w) {
        tmp[n++] = set[w % base];
        w /= base;
      }
      break;
    }
    // Rare (64-bit values): base 16 only needs shifts; base 10 does schoolbook division by
    // 32-bit halves.
    if (base == 16) {
      tmp[n++] = set[v & 15];
      v >>= 4;
    } else {
      // (hi:lo) / base in 16-bit steps; every intermediate is below base << 16, so 32 bits.
      unsigned int hi = (unsigned int)(v >> 32), lo = (unsigned int)v;
      unsigned int r = hi % base;
      hi /= base;
      const unsigned int mid = (r << 16) | (lo >> 16);
      const unsigned int q1 = mid / base;
      r = mid % base;
      const unsigned int low = (r << 16) | (lo & 0xFFFF);
      const unsigned int q0 = low / base;
      r = low % base;
      tmp[n++] = set[r];
      v = ((unsigned long long)hi << 32) | ((unsigned long long)q1 << 16) | q0;
    }
  }
  return n;
}

void Pad(Out& o, int count, char c) {
  while (count-- > 0) o.Put(c);
}

}  // namespace

extern "C" {

int vsnprintf(char* buf, size_t cap, const char* fmt, va_list ap) {
  Out o = {buf, cap, 0};
  for (const char* p = fmt; *p; ++p) {
    if (*p != '%') {
      o.Put(*p);
      continue;
    }
    ++p;
    bool left = false, zero = false;
    for (;; ++p) {
      if (*p == '-') left = true;
      else if (*p == '0') zero = true;
      else if (*p == ' ' || *p == '+' || *p == '#') {}  // accepted, ignored
      else break;
    }
    int width = 0;
    if (*p == '*') {
      width = va_arg(ap, int);
      ++p;
    } else {
      while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
    }
    int precision = -1;
    if (*p == '.') {
      ++p;
      precision = 0;
      if (*p == '*') {
        precision = va_arg(ap, int);
        ++p;
      } else {
        while (*p >= '0' && *p <= '9') precision = precision * 10 + (*p++ - '0');
      }
    }
    int size = 0;  // 0 int, 1 long, 2 long long
    while (*p == 'h' || *p == 'l' || *p == 'z') {
      if (*p == 'l') ++size;
      if (*p == 'z') size = 1;
      ++p;
    }
    char tmp[24];
    switch (*p) {
      case 'd':
      case 'i':
      case 'u':
      case 'x':
      case 'X':
      case 'p': {
        unsigned long long v;
        bool negative = false;
        if (*p == 'p') {
          v = (unsigned long long)(size_t)va_arg(ap, void*);
        } else if (*p == 'd' || *p == 'i') {
          long long s = size == 2 ? va_arg(ap, long long)
                      : size == 1 ? (long long)va_arg(ap, long)
                                  : (long long)va_arg(ap, int);
          negative = s < 0;
          v = negative ? (unsigned long long)(-(s + 1)) + 1 : (unsigned long long)s;
        } else {
          v = size == 2 ? va_arg(ap, unsigned long long)
            : size == 1 ? (unsigned long long)va_arg(ap, unsigned long)
                        : (unsigned long long)va_arg(ap, unsigned int);
        }
        const unsigned base = (*p == 'x' || *p == 'X' || *p == 'p') ? 16 : 10;
        const int n = Digits(v, base, *p == 'X', tmp);
        const int body = n + (negative ? 1 : 0);
        if (!left && !zero) Pad(o, width - body, ' ');
        if (negative) o.Put('-');
        if (!left && zero) Pad(o, width - body, '0');
        for (int i = n - 1; i >= 0; --i) o.Put(tmp[i]);
        if (left) Pad(o, width - body, ' ');
        break;
      }
      case 's': {
        const char* s = va_arg(ap, const char*);
        if (!s) s = "(null)";
        int n = 0;
        while (s[n] && (precision < 0 || n < precision)) ++n;
        if (!left) Pad(o, width - n, ' ');
        for (int i = 0; i < n; ++i) o.Put(s[i]);
        if (left) Pad(o, width - n, ' ');
        break;
      }
      case 'c': {
        const char c = (char)va_arg(ap, int);
        if (!left) Pad(o, width - 1, ' ');
        o.Put(c);
        if (left) Pad(o, width - 1, ' ');
        break;
      }
      case '%':
        o.Put('%');
        break;
      case 0:
        --p;  // format ended mid-spec
        break;
      default:  // unknown: print it as-is
        o.Put('%');
        o.Put(*p);
        break;
    }
  }
  if (cap) buf[o.len < cap ? o.len : cap - 1] = 0;
  return (int)o.len;
}

int snprintf(char* buf, size_t cap, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(buf, cap, fmt, ap);
  va_end(ap);
  return n;
}

// MSVC: returns the length written, -1 if truncated (the buffer is still terminated with
// _TRUNCATE).
int _vsnprintf_s(char* buf, size_t cap, size_t count, const char* fmt, va_list ap) {
  if (!buf || !cap) return -1;
  size_t limit = cap;
  if (count != (size_t)-1 && count + 1 < limit) limit = count + 1;
  const int n = vsnprintf(buf, limit, fmt, ap);
  return (n < 0 || (size_t)n >= limit) ? -1 : n;
}

int sprintf_s(char* buf, size_t cap, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(buf, cap, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= cap) return -1;
  return n;
}

// Wide formatting for util::Notify: the format is narrowed (the plugin's wide formats are ASCII
// and take no wide string arguments), formatted, then widened.
int _vsnwprintf_s(wchar_t* buf, size_t cap, size_t count, const wchar_t* fmt, va_list ap) {
  if (!buf || !cap) return -1;
  char narrow_fmt[256];
  size_t i = 0;
  for (; fmt[i] && i < sizeof(narrow_fmt) - 1; ++i) narrow_fmt[i] = (char)fmt[i];
  narrow_fmt[i] = 0;
  char text[512];
  size_t limit = cap < sizeof(text) ? cap : sizeof(text);
  if (count != (size_t)-1 && count + 1 < limit) limit = count + 1;
  const int n = vsnprintf(text, limit, narrow_fmt, ap);
  size_t k = 0;
  for (; text[k]; ++k) buf[k] = (wchar_t)(unsigned char)text[k];
  buf[k] = 0;
  return (n < 0 || (size_t)n >= limit) ? -1 : n;
}

}  // extern "C"
