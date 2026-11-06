// Native tests for the open toolchain's freestanding C library (xrt/xrt_libc.cpp,
// xrt/xrt_printf.cpp), compared against the host C library where they overlap.
// Built with -fno-builtin so the calls below reach the xrt versions, which are linked in under
// their own names via the xrt_ prefix trick in the Makefile (objcopy --prefix-symbols).
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern "C" {
int xrt_vsnprintf(char* buf, size_t cap, const char* fmt, va_list ap);
int xrt_sprintf_s(char* buf, size_t cap, const char* fmt, ...);
int xrt_strncpy_s(char* dst, size_t cap, const char* src, size_t count);
char* xrt_strtok_s(char* s, const char* delims, char** context);
int xrt__stricmp(const char* a, const char* b);
unsigned long xrt_strtoul(const char* s, char** end, int base);
void* xrt_memmove(void* dst, const void* src, size_t n);
void* xrt_memset(void* dst, int c, size_t n);
void* xrt_memcpy(void* dst, const void* src, size_t n);
char* xrt_strrchr(const char* s, int c);
char* xrt_strcpy(char* dst, const char* src);
char* xrt_strcat(char* dst, const char* src);
char* xrt_strncat(char* dst, const char* src, size_t n);
char* xrt_strstr(const char* haystack, const char* needle);
}

static int g_fail = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);         \
      ++g_fail;                                                     \
    }                                                               \
  } while (0)

static int Fmt(char* buf, size_t cap, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const int n = xrt_vsnprintf(buf, cap, fmt, ap);
  va_end(ap);
  return n;
}

// Same format and arguments through xrt and the host; both outputs must match.
#define SAME(fmt, ...)                                                        \
  do {                                                                        \
    char a[256], b[256];                                                      \
    const int na = Fmt(a, sizeof(a), fmt, __VA_ARGS__);                       \
    const int nb = snprintf(b, sizeof(b), fmt, __VA_ARGS__);                  \
    if (na != nb || strcmp(a, b)) {                                           \
      printf("  FAIL %s:%d  \"%s\": xrt \"%s\" (%d) vs host \"%s\" (%d)\n",   \
             __FILE__, __LINE__, fmt, a, na, b, nb);                          \
      ++g_fail;                                                               \
    }                                                                         \
  } while (0)

int main() {
  printf("printf\n");
  {
    static const char* const kHay[] = {"GET / HTTP/1.1\r\n\r\n", "abcabcabd", "", "aaa", "x.qoi"};
    static const char* const kNeedle[] = {"\r\n\r\n", "abd", "a", "", ".qoi", "zz", "abcabd"};
    for (size_t h = 0; h < sizeof(kHay) / sizeof(kHay[0]); ++h) {
      for (size_t n = 0; n < sizeof(kNeedle) / sizeof(kNeedle[0]); ++n) {
        CHECK(xrt_strstr(kHay[h], kNeedle[n]) == strstr(kHay[h], kNeedle[n]));
      }
    }
  }
  SAME("%d %i %u", -5, 42, 7u);
  SAME("%08X|%x|%X", 0x8A01793Cu, 0xbeefu, 0u);
  SAME("[%5d][%-5d][%05d]", 42, 42, -42);
  SAME("%s and %.3s and [%8s] [%-8s]", "abc", "abcdef", "hi", "hi");
  SAME("%c%c%%", 'o', 'k');
  SAME("%lu %ld %lx", 4000000000ul, -1l, 0xdeadbeeful);
  SAME("%llu %lld %llx", 18446744073709551615ull, -9223372036854775807ll - 1, 0x0123456789abcdefull);
  SAME("%u.%u.%u.%u", 2u, 0u, 21256u, 0u);
  SAME("Game: TitleID = 0x%08X  MediaID = 0x%08X  ModuleVersion = \"%u.%u.%u.%u\"",
       0x41560911u, 0x4FC9256Fu, 0u, 0u, 5u, 1u);
  SAME("%s", "");
  {
    char small[8];
    const int n = Fmt(small, sizeof(small), "%s", "0123456789");
    CHECK(n == 10 && !strcmp(small, "0123456"));
    CHECK(xrt_sprintf_s(small, sizeof(small), "%s", "0123456789") == -1);
    CHECK(!strcmp(small, "0123456"));
    char none[1] = {'x'};
    CHECK(Fmt(none, 1, "abc") == 3 && none[0] == 0);
  }

  printf("strings\n");
  {
    char d[8];
    CHECK(xrt_strncpy_s(d, sizeof(d), "abc", (size_t)-1) == 0 && !strcmp(d, "abc"));
    CHECK(xrt_strncpy_s(d, sizeof(d), "abcdefghij", (size_t)-1) == 80 && !strcmp(d, "abcdefg"));
    CHECK(xrt_strncpy_s(d, sizeof(d), "abcdef", 2) == 0 && !strcmp(d, "ab"));
    CHECK(xrt__stricmp("LB+rb", "lb+RB") == 0);
    CHECK(xrt__stricmp("Spyro", "Spyro2") < 0 && xrt__stricmp("b", "A") > 0);
    char t[] = "  LB+RB, BACK+LT ";
    char* ctx = 0;
    const char* toks[8];
    int n = 0;
    for (char* tok = xrt_strtok_s(t, "+ ,", &ctx); tok; tok = xrt_strtok_s(0, "+ ,", &ctx)) {
      toks[n++] = tok;
    }
    CHECK(n == 4 && !strcmp(toks[0], "LB") && !strcmp(toks[1], "RB") && !strcmp(toks[2], "BACK") &&
          !strcmp(toks[3], "LT"));
    char* end = 0;
    CHECK(xrt_strtoul("0x4156091F", &end, 0) == 0x4156091Ful && *end == 0);
    CHECK(xrt_strtoul("41560911", 0, 16) == 0x41560911ul);
    CHECK(xrt_strtoul("123abc", &end, 10) == 123 && !strcmp(end, "abc"));
    CHECK(!strcmp(xrt_strrchr("SKYHDD:\\SkyPortal\\Figures\\*", '\\'), "\\*"));
    char cat[24];
    CHECK(xrt_strcpy(cat, "On: ") == cat && !strcmp(cat, "On: "));
    CHECK(xrt_strcat(cat, "Spyro") == cat && !strcmp(cat, "On: Spyro"));
    CHECK(xrt_strncat(cat, ", Bash, Eruptor", 6) == cat && !strcmp(cat, "On: Spyro, Bash"));
  }

  printf("memory\n");
  {
    unsigned char buf[64], ref[64];
    for (int i = 0; i < 64; ++i) buf[i] = ref[i] = (unsigned char)i;
    xrt_memmove(buf + 3, buf, 40);  // overlapping forward
    memmove(ref + 3, ref, 40);
    CHECK(!memcmp(buf, ref, 64));
    xrt_memmove(buf, buf + 5, 40);  // overlapping backward
    memmove(ref, ref + 5, 40);
    CHECK(!memcmp(buf, ref, 64));
    xrt_memset(buf + 1, 0xAB, 37);  // unaligned start/end
    memset(ref + 1, 0xAB, 37);
    CHECK(!memcmp(buf, ref, 64));
    unsigned int aligned_src[8] = {1, 2, 3, 4, 5, 6, 7, 8}, aligned_dst[8];
    xrt_memcpy(aligned_dst, aligned_src, sizeof(aligned_src));
    CHECK(!memcmp(aligned_dst, aligned_src, sizeof(aligned_src)));
    xrt_memcpy(buf + 1, ref + 2, 13);
    CHECK(!memcmp(buf + 1, ref + 2, 13));
  }

  if (g_fail) {
    printf("%d check(s) failed\n", g_fail);
    return 1;
  }
  printf("all xrt libc tests passed\n");
  return 0;
}
