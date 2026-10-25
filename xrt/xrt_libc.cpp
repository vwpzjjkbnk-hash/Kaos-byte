// SkyPortal360 open toolchain: the C library subset the plugin needs. Freestanding and portable
// (also built natively by tests/test_xrt.cpp).
#include <string.h>
#include <stdlib.h>

extern "C" {

void* memcpy(void* dst, const void* src, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  // Word copies when both are aligned (figure blocks and frames are).
  if ((((size_t)d | (size_t)s) & 3) == 0) {
    while (n >= 4) {
      *(unsigned int*)d = *(const unsigned int*)s;
      d += 4;
      s += 4;
      n -= 4;
    }
  }
  while (n--) *d++ = *s++;
  return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  if (d == s || n == 0) return dst;
  if (d < s || d >= s + n) return memcpy(dst, src, n);
  d += n;
  s += n;
  while (n--) *--d = *--s;
  return dst;
}

void* memset(void* dst, int c, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char v = (unsigned char)c;
  while (n && ((size_t)d & 3)) {
    *d++ = v;
    --n;
  }
  const unsigned int w = v * 0x01010101u;
  while (n >= 4) {
    *(unsigned int*)d = w;
    d += 4;
    n -= 4;
  }
  while (n--) *d++ = v;
  return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
  const unsigned char* x = (const unsigned char*)a;
  const unsigned char* y = (const unsigned char*)b;
  for (; n; --n, ++x, ++y) {
    if (*x != *y) return *x < *y ? -1 : 1;
  }
  return 0;
}

size_t strlen(const char* s) {
  const char* p = s;
  while (*p) ++p;
  return (size_t)(p - s);
}

char* strchr(const char* s, int c) {
  const char ch = (char)c;
  for (;; ++s) {
    if (*s == ch) return (char*)s;
    if (!*s) return 0;
  }
}

char* strrchr(const char* s, int c) {
  const char ch = (char)c;
  const char* last = 0;
  for (;; ++s) {
    if (*s == ch) last = s;
    if (!*s) return (char*)last;
  }
}

char* strcpy(char* dst, const char* src) {
  char* d = dst;
  while ((*d++ = *src++) != 0) {
  }
  return dst;
}

char* strcat(char* dst, const char* src) {
  strcpy(dst + strlen(dst), src);
  return dst;
}

char* strncat(char* dst, const char* src, size_t n) {
  char* d = dst + strlen(dst);
  while (n-- && *src) *d++ = *src++;
  *d = 0;
  return dst;
}

int strcmp(const char* a, const char* b) {
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c; }

int _stricmp(const char* a, const char* b) {
  for (;; ++a, ++b) {
    const int x = lower((unsigned char)*a);
    const int y = lower((unsigned char)*b);
    if (x != y || !x) return x - y;
  }
}

int _strnicmp(const char* a, const char* b, size_t n) {
  for (; n; --n, ++a, ++b) {
    const int x = lower((unsigned char)*a);
    const int y = lower((unsigned char)*b);
    if (x != y || !x) return x - y;
  }
  return 0;
}

int strncmp(const char* a, const char* b, size_t n) {
  for (; n; --n, ++a, ++b) {
    if (*a != *b || !*a) return (int)(unsigned char)*a - (int)(unsigned char)*b;
  }
  return 0;
}

// MSVC semantics as the plugin uses them: count == _TRUNCATE copies as much as fits and always
// terminates. Returns 0, or 80 (STRUNCATE) when truncated.
int strncpy_s(char* dst, size_t cap, const char* src, size_t count) {
  if (!dst || !cap) return 22;  // EINVAL
  if (!src) {
    dst[0] = 0;
    return 22;
  }
  size_t limit = cap - 1;
  if (count != (size_t)-1 && count < limit) limit = count;
  size_t i = 0;
  for (; i < limit && src[i]; ++i) dst[i] = src[i];
  dst[i] = 0;
  return (src[i] && i == cap - 1) ? 80 : 0;
}

char* strtok_s(char* s, const char* delims, char** context) {
  char* p = s ? s : *context;
  if (!p) return 0;
  while (*p && strchr(delims, *p)) ++p;  // skip leading delimiters
  if (!*p) {
    *context = p;
    return 0;
  }
  char* token = p;
  while (*p && !strchr(delims, *p)) ++p;
  if (*p) *p++ = 0;
  *context = p;
  return token;
}

unsigned long strtoul(const char* s, char** end, int base) {
  const char* p = s;
  while (*p == ' ' || *p == '\t') ++p;
  if (*p == '+') ++p;
  if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
    base = 16;
  } else if (base == 0) {
    base = (p[0] == '0') ? 8 : 10;
  }
  unsigned long value = 0;
  for (;; ++p) {
    int digit;
    if (*p >= '0' && *p <= '9') digit = *p - '0';
    else if (*p >= 'a' && *p <= 'z') digit = *p - 'a' + 10;
    else if (*p >= 'A' && *p <= 'Z') digit = *p - 'A' + 10;
    else break;
    if (digit >= base) break;
    value = value * (unsigned long)base + (unsigned long)digit;
  }
  if (end) *end = (char*)p;
  return value;
}

}  // extern "C"
