// Native test for the staticrift.ini reader (src/xbox/config.cpp), built against the XDK stubs
// with tiny host shims for the MSVC CRT functions the Xbox build gets from the XDK.
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "../src/xbox/config.h"

// ---- host shims ----
int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
int strncpy_s(char* d, size_t cap, const char* s, size_t) {
  size_t n = strlen(s);
  if (n >= cap) n = cap - 1;
  memcpy(d, s, n);
  d[n] = 0;
  return 0;
}
void InitializeCriticalSection(CRITICAL_SECTION*) {}
void DeleteCriticalSection(CRITICAL_SECTION*) {}
void EnterCriticalSection(CRITICAL_SECTION*) {}
void LeaveCriticalSection(CRITICAL_SECTION*) {}
namespace util {
int ReadAll(const char* path, char* out, int cap) {
  FILE* f = fopen(path, "rb");
  if (!f) return -1;
  const int n = (int)fread(out, 1, cap, f);
  fclose(f);
  return n;
}
}  // namespace util

static int g_fail = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);         \
      ++g_fail;                                                     \
    }                                                               \
  } while (0)

int main(int argc, char** argv) {
  const char* shipped = argc > 1 ? argv[1] : "../dist/The Static Rift/staticrift.ini";
  Config c;
  CHECK(c.Load(shipped));
  CHECK(c.GetBool("General", "Enabled", false));
  CHECK(c.GetBool("General", "SaveWrites", false));
  CHECK(c.GetBool("General", "Speaker", false));
  CHECK(!strcmp(c.Get("Controls", "Modifier", "?"), "LB+RB"));
  CHECK(!c.GetBool("Titles", "PatchAllTitles", true));
  CHECK(!c.GetBool("Menu", "NewestOnly", true));  // ALL lists every release
  CHECK(c.GetBool("Menu", "PauseGame", false));   // every game pauses behind the menu
  CHECK(c.GetBool("Menu", "OnlyCompatible", false));
  CHECK(c.GetBool("Remote", "Enabled", false));    // the phone remote is on
  CHECK(c.GetInt("Remote", "Port", 0) == 80);
  const char* k;
  const char* v;
  int startup = 0;
  while (c.Value("Startup", startup, &k, &v)) ++startup;
  CHECK(startup == 0);  // all commented out in the shipped file

  // A hand-written file with BOM, CRLF, odd spacing, comments, hex and repeated keys.
  const char* tmp = "test_config.tmp.ini";
  FILE* f = fopen(tmp, "wb");
  fputs("\xEF\xBB\xBF; comment\r\n[General]\r\n  Enabled=0 ; off\r\nNotifications = yes\r\n"
        "[controls]\r\nModifier=BACK+LT\r\n[Titles]\r\nExtra1 = 41560911\r\nExtra2=0x4156091F\r\n"
        "PatchAllTitles=0\r\n[Startup]\r\nFigure1 = Spyro\r\nFigure2= Giants\\Tree Rex.bin\r\n"
        "[General]\r\nEnabled = 1\r\n",
        f);
  fclose(f);
  Config d;
  CHECK(d.Load(tmp));
  CHECK(d.GetBool("General", "Enabled", false));  // last one wins
  CHECK(d.GetBool("general", "notifications", false));
  CHECK(!strcmp(d.Get("Controls", "Modifier", "?"), "BACK+LT"));
  CHECK(d.GetInt("Titles", "Extra2", 0) == 0x4156091F);
  CHECK(d.Value("Startup", 0, &k, &v) && !strcmp(v, "Spyro"));
  CHECK(d.Value("Startup", 1, &k, &v) && !strcmp(v, "Giants\\Tree Rex.bin"));
  CHECK(!d.Value("Startup", 2, &k, &v));
  CHECK(d.Value("Titles", 0, &k, &v) && !strcmp(k, "Extra1") && !strcmp(v, "41560911"));
  CHECK(!Config().Load("/nonexistent/staticrift.ini"));

  if (g_fail) {
    printf("%d check(s) failed\n", g_fail);
    return 1;
  }
  printf("all config tests passed\n");
  return 0;
}
