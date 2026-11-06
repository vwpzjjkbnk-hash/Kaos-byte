// Native test for the figure library (src/xbox/figures.cpp): the menu's order by type, sections,
// "My Picks" and the game filter, against a small pack written to ./figures_fixture.
//   test_figures                     the checks
//   test_figures <The Static Rift> [picks.ini]   also a report for a real pack (counts, sections,
//                                                picks)
// Built against the XDK stubs, with host shims below for the file APIs the library uses.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>
#include <vector>

#include "../src/xbox/figures.h"
#include "../src/xbox/elements.h"

// ---- host shims: Win32 file calls on top of stdio / dirent, "\" paths turned into "/" ----
static std::string HostPath(const char* p) {
  std::string s(p);
  for (size_t i = 0; i < s.size(); ++i)
    if (s[i] == '\\') s[i] = '/';
  return s;
}
int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
extern "C" int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
int strncpy_s(char* d, size_t cap, const char* s, size_t) {
  size_t n = strlen(s);
  if (n >= cap) n = cap - 1;
  memcpy(d, s, n);
  d[n] = 0;
  return 0;
}
int sprintf_s(char* d, size_t cap, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(d, cap, fmt, ap);
  va_end(ap);
  return n;
}
void InitializeCriticalSection(CRITICAL_SECTION*) {}
void DeleteCriticalSection(CRITICAL_SECTION*) {}
void EnterCriticalSection(CRITICAL_SECTION*) {}
void LeaveCriticalSection(CRITICAL_SECTION*) {}
HANDLE CreateFile(const char* path, DWORD access, DWORD, LPSECURITY_ATTRIBUTES, DWORD disp, DWORD,
                  HANDLE) {
  const char* mode = (access & GENERIC_WRITE) ? (disp == CREATE_ALWAYS ? "wb" : "r+b") : "rb";
  FILE* f = fopen(HostPath(path).c_str(), mode);
  return f ? (HANDLE)f : INVALID_HANDLE_VALUE;
}
BOOL ReadFile(HANDLE h, void* out, DWORD n, DWORD* got, void*) {
  *got = (DWORD)fread(out, 1, n, (FILE*)h);
  return TRUE;
}
BOOL WriteFile(HANDLE h, const void* in, DWORD n, DWORD* put, void*) {
  *put = (DWORD)fwrite(in, 1, n, (FILE*)h);
  return TRUE;
}
BOOL CloseHandle(HANDLE h) {
  fclose((FILE*)h);
  return TRUE;
}
DWORD GetFileSize(HANDLE h, DWORD*) {
  struct stat st;
  fstat(fileno((FILE*)h), &st);
  return (DWORD)st.st_size;
}
BOOL CreateDirectory(const char* path, LPSECURITY_ATTRIBUTES) {
  return mkdir(HostPath(path).c_str(), 0755) == 0;
}
struct FindState {
  std::string dir;
  std::vector<std::string> names;
  size_t next;
};
static bool FillFind(FindState* f, WIN32_FIND_DATA* fd) {
  if (f->next >= f->names.size()) return false;
  const std::string& name = f->names[f->next++];
  memset(fd, 0, sizeof(*fd));
  strncpy(fd->cFileName, name.c_str(), MAX_PATH - 1);
  struct stat st;
  stat((f->dir + "/" + name).c_str(), &st);
  fd->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
  fd->nFileSizeLow = (DWORD)st.st_size;
  return true;
}
HANDLE FindFirstFile(const char* pattern, WIN32_FIND_DATA* fd) {
  std::string dir = HostPath(pattern);
  dir = dir.substr(0, dir.rfind('/'));  // drop "/*"
  DIR* d = opendir(dir.c_str());
  if (!d) return INVALID_HANDLE_VALUE;
  FindState* f = new FindState;
  f->dir = dir;
  f->next = 0;
  for (struct dirent* e = readdir(d); e; e = readdir(d)) f->names.push_back(e->d_name);
  closedir(d);
  if (!FillFind(f, fd)) {
    delete f;
    return INVALID_HANDLE_VALUE;
  }
  return (HANDLE)f;
}
BOOL FindNextFile(HANDLE h, WIN32_FIND_DATA* fd) { return FillFind((FindState*)h, fd); }
BOOL FindClose(HANDLE h) {
  delete (FindState*)h;
  return TRUE;
}
namespace util {
bool ReadExact(const char* path, void* out, DWORD size) {
  FILE* f = fopen(HostPath(path).c_str(), "rb");
  if (!f) return false;
  const size_t n = fread(out, 1, size, f);
  fclose(f);
  return n == size;
}
bool WriteAll(const char* path, const void* data, DWORD size, bool) {
  FILE* f = fopen(HostPath(path).c_str(), "wb");
  if (!f) return false;
  fwrite(data, 1, size, f);
  fclose(f);
  return true;
}
bool FileExists(const char* path) {
  struct stat st;
  return stat(HostPath(path).c_str(), &st) == 0;
}
}  // namespace util

// ---- checks ----
static int g_fail = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);         \
      ++g_fail;                                                     \
    }                                                               \
  } while (0)

static void MkdirP(const std::string& path) {
  std::string cur;
  for (size_t i = 0; i <= path.size(); ++i) {
    if (i == path.size() || path[i] == '/') {
      if (!cur.empty()) mkdir(cur.c_str(), 0755);
    }
    if (i < path.size()) cur += path[i];
  }
}

static void WriteDump(const std::string& root, const std::string& rel, int toy) {
  const std::string path = root + "/Figures/" + rel;
  MkdirP(path.substr(0, path.rfind('/')));
  unsigned char d[1024];
  memset(d, 0x11, sizeof(d));
  d[0x10] = (unsigned char)(toy & 0xFF);
  d[0x11] = (unsigned char)(toy >> 8);
  FILE* f = fopen(path.c_str(), "wb");
  fwrite(d, 1, sizeof(d), f);
  fclose(f);
}

static std::vector<std::string> Names(FigureLibrary& lib) {
  std::vector<std::string> v;
  for (int p = 0; p < lib.BrowseCount(); ++p) v.push_back(lib.Name(lib.BrowseAt(p)));
  return v;
}

static std::string Join(const std::vector<std::string>& v) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + v[i];
  return s;
}

static void TestFixture() {
  printf("order by type, sections, My Picks (fixture)\n");
  const std::string root = "figures_fixture";
  system("rm -rf figures_fixture");
  MkdirP(root + "/UI");
  struct Fig {
    const char* rel;
    const char* name;
    int game, element;
    const char* series;
    int first, toy;
    char kind;
  };
  // Elements: 1 Air, 2 Earth, 3 Fire, 4 Water, 5 Magic, 6 Tech, 7 Life.
  const Fig figs[] = {
      {"1. Spyro's Adventure/1) Figures/Spyro.dump", "Spyro", 1, 5, "Series 1", 1, 16, 'R'},
      {"1. Spyro's Adventure/1) Figures/Drobot.dump", "Drobot", 1, 6, "Series 1", 1, 20, 'R'},
      {"1. Spyro's Adventure/2) Magic Items/Anvil Rain.dump", "Anvil Rain", 1, 0, "Magic Item", 1, 200, '-'},
      {"1. Spyro's Adventure/3) Adventure Packs/Dragon's Peak.dump", "Dragon's Peak", 1, 0, "Adventure Pack", 1, 300, '-'},
      {"2. Giants/1) Giants/Tree Rex.dump", "Tree Rex", 2, 7, "Giant", 2, 112, 'R'},
      {"2. Giants/3) Returning (Series 2)/Drobot (Series 2).dump", "Drobot", 2, 6, "Series 2", 1, 20, 'R'},
      {"3. Swap Force/1) Swappers/Blast Zone/Blast Zone Top.dump", "Blast Zone Top", 3, 3, "Swapper Top", 3, 2004, 'R'},
      {"3. Swap Force/1) Swappers/Blast Zone/Blast Zone Bottom.dump", "Blast Zone Bottom", 3, 3, "Swapper Bottom", 3, 1004, 'R'},
      {"4. Trap Team/1) Trap Masters/Wildfire.dump", "Wildfire", 4, 3, "Trap Master", 4, 458, 'R'},
      {"4. Trap Team/4) Eon's Elite/Eon's Elite Spyro.dump", "Eon's Elite Spyro", 4, 5, "Eon's Elite", 1, 16, 'V'},
      {"4. Trap Team/7) Traps/Air/Air Toucan.dump", "Air Toucan", 4, 1, "Trap", 4, 212, '-'},
      {"4. Trap Team/7) Traps/Air/Pain-Yatta.dump", "Pain-Yatta", 4, 5, "Villain", 4, 215, '-'},
      {"5. SuperChargers/2) Vehicles/Hot Streak.dump", "Hot Streak", 5, 3, "Land Vehicle", 5, 3224, '-'},
      {"5. SuperChargers/1) SuperChargers/Spitfire.dump", "Spitfire", 5, 3, "SuperCharger", 5, 3412, 'R'},
      {"6. Imaginators/1) Senseis/Pain-Yatta.dump", "Pain-Yatta", 6, 5, "Villain Sensei", 6, 615, 'R'},
      {"6. Imaginators/3) Creation Crystals/Fire/Fire Angel.dump", "Fire Angel", 6, 3, "Creation Crystal", 6, 682, '-'},
      {"6. Imaginators/8) Unreleased/Instant Wildfire.dump", "Instant Wildfire", 6, 3, "Unreleased", 6, 458, '-'},
      // SuperChargers share their character's id: Stealth Elf (26) stays a regular Skylander.
      {"1. Spyro's Adventure/1) Figures/Stealth Elf.dump", "Stealth Elf", 1, 7, "Series 1", 1, 26, 'R'},
      {"1. Spyro's Adventure/4) In-Game Variants/Dark Stealth Elf.dump", "Dark Stealth Elf", 1, 7, "In-Game Variant", 1, 26, 'V'},
      {"5. SuperChargers/1) SuperChargers/Super Shot Stealth Elf.dump", "Super Shot Stealth Elf", 5, 7, "SuperCharger", 5, 26, 'R'},
      {"5. SuperChargers/5) In-Game Variants/Dark Super Shot Stealth Elf.dump", "Dark Super Shot Stealth Elf", 5, 7, "In-Game Variant", 5, 26, 'V'},
  };
  std::string idx = "# test index\n";
  for (size_t i = 0; i < sizeof(figs) / sizeof(figs[0]); ++i) {
    const Fig& f = figs[i];
    WriteDump(root, f.rel, f.toy);
    // Full-path keys, as tools/mkassets.py writes them: lower case letters, digits and "/".
    std::string path = f.rel;
    path = path.substr(0, path.rfind('.'));
    std::string key;
    for (size_t k = 0; k < path.size(); ++k) {
      const char c = (char)tolower(path[k]);
      if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/') key += c;
    }
    char line[512];
    snprintf(line, sizeof(line), "%s\t%s\t%d\t%d\t%s\t-\t%d\t%d\t%c\n", key.c_str(), f.name, f.game,
             f.element, f.series, f.first, f.toy, f.kind);
    idx += line;
  }
  WriteDump(root, "My Spyro.bin", 16);  // a loose dump of your own, not in the index
  FILE* fi = fopen((root + "/UI/figures.idx").c_str(), "wb");
  fwrite(idx.data(), 1, idx.size(), fi);
  fclose(fi);

  FigureLibrary lib;
  lib.SetRoot(root.c_str());
  CHECK(lib.LoadIndex());
  CHECK(lib.SortByType());  // the default

  // All six games' figures (Imaginators runs everything), every release.
  lib.SetGameFilter(6);
  lib.SetNewestOnly(false);
  lib.Scan();
  {
    int stealth = 0;
    for (int p = 0; p < lib.BrowseCount(); ++p) {
      const int id = lib.BrowseAt(p);
      const std::string n = lib.Name(id);
      const int c = lib.GetCategory(id);
      if (n == "Stealth Elf" || n == "Dark Stealth Elf") {
        CHECK(c == FigureLibrary::kCatSkylander);
        ++stealth;
      }
      if (n == "Super Shot Stealth Elf" || n == "Dark Super Shot Stealth Elf") {
        CHECK(c == FigureLibrary::kCatGimmick);
        ++stealth;
      }
    }
    CHECK(stealth == 4);
  }
  std::string got = Join(Names(lib));
  printf("  Imaginators, by type: %s\n", got.c_str());
  CHECK(got ==
        "Blast Zone Bottom, Blast Zone Top, Instant Wildfire, Spitfire, Wildfire, "     // gimmick: Fire
        "Pain-Yatta, "                                                                  //   Magic
        "Dark Super Shot Stealth Elf, Super Shot Stealth Elf, Tree Rex, "               //   Life
        "Eon's Elite Spyro, My Spyro, Spyro, Drobot, Drobot, Dark Stealth Elf, Stealth Elf, "  // Skylanders
        "Fire Angel, "                                                                  // crystals
        "Air Toucan, Pain-Yatta, "                                                      // traps: Air, Magic
        "Hot Streak, "                                                                  // vehicles
        "Anvil Rain, "                                                                  // magic items
        "Dragon's Peak");                                                               // adventure packs
  // Sections: up/down jumps category + element.
  CHECK(lib.GroupJump(0, +1) == 5);   // Gimmick/Fire -> Gimmick/Magic (Pain-Yatta the Sensei)
  CHECK(lib.GroupJump(5, +1) == 6);   // -> Gimmick/Life
  CHECK(lib.GroupJump(6, +1) == 9);   // -> Skylanders/Magic
  CHECK(lib.GroupJump(10, -1) == 9);  // back to the start of the section
  CHECK(lib.GroupJump(9, -1) == 6);   // ... and from there to the previous one
  char label[96];
  CHECK(lib.SectionLabel(0, label, sizeof(label)) && !strcmp(label, "Gimmick Skylanders / Fire"));
  CHECK(lib.SectionLabel(16, label, sizeof(label)) && !strcmp(label, "Creation Crystals / Fire"));
  CHECK(lib.SectionLabel(20, label, sizeof(label)) && !strcmp(label, "Magic Items"));
  CHECK(lib.SectionLabel(21, label, sizeof(label)) && !strcmp(label, "Adventure Packs"));
  CHECK(lib.RosterAt(0) == 0);
  // The older Drobot first.
  CHECK(lib.Game(lib.BrowseAt(12)) == 1 && lib.Game(lib.BrowseAt(13)) == 2);

  // Spyro's Adventure: only what it can use.
  lib.SetGameFilter(1);
  lib.Scan();
  got = Join(Names(lib));
  printf("  Spyro's Adventure: %s\n", got.c_str());
  // (Series 2 Drobot works in Spyro's Adventure too; NewestOnly would list only it.)
  CHECK(got == "Eon's Elite Spyro, My Spyro, Spyro, Drobot, Drobot, Dark Stealth Elf, Stealth Elf, "
               "Anvil Rain, Dragon's Peak");

  // My Picks in Giants: Drobot -> its newest release (Series 2), Tree Rex; Blast Zone can't be
  // used in Giants. Then the rest (items, adventure packs).
  const char* picks[] = {"Drobot", "tree rex", "Blast Zone"};
  lib.SetPicks(picks, 3);
  lib.SetGameFilter(2);
  lib.SetPicksMode(true);
  lib.Scan();
  got = Join(Names(lib));
  printf("  Giants, My Picks: %s\n", got.c_str());
  CHECK(got == "Tree Rex, Drobot, Anvil Rain, Dragon's Peak");
  CHECK(lib.Game(lib.BrowseAt(1)) == 2);
  CHECK(lib.PicksFound() == 2);

  // A game or series in the pick chooses that release; a near spelling still finds a figure.
  const char* picks_q[] = {"Spyro's Adventure Drobot", "Treee Rex", "Nobody At All"};
  lib.SetPicks(picks_q, 3);
  lib.SetPicksMode(true);
  lib.Scan();
  got = Join(Names(lib));
  printf("  Giants, picks with a game and a misspelling: %s\n", got.c_str());
  CHECK(got == "Tree Rex, Drobot, Anvil Rain, Dragon's Peak");
  CHECK(lib.Game(lib.BrowseAt(1)) == 1);
  CHECK(lib.PicksFound() == 2 && lib.PickFound(0) && lib.PickFound(1) && !lib.PickFound(2));
  CHECK(!strcmp(lib.PickText(2), "Nobody At All"));
  const char* picks_s[] = {"Drobot (series 1)"};
  lib.SetPicks(picks_s, 1);
  lib.SetPicksMode(true);
  lib.Scan();
  CHECK(Join(Names(lib)) == "Drobot, Anvil Rain, Dragon's Peak" && lib.Game(lib.BrowseAt(0)) == 1);
  const char* picks_s2[] = {"Drobot (Series 2)"};
  lib.SetPicks(picks_s2, 1);
  lib.SetPicksMode(true);
  lib.Scan();
  CHECK(lib.Game(lib.BrowseAt(0)) == 2);
  lib.SetPicks(picks, 3);
  lib.SetPicksMode(true);

  // Swap Force: a swapper's name picks both halves; NewestOnly on as in the menu.
  lib.SetGameFilter(3);
  lib.SetNewestOnly(true);
  lib.Resort();
  got = Join(Names(lib));
  printf("  Swap Force, My Picks: %s\n", got.c_str());
  CHECK(got == "Blast Zone Bottom, Blast Zone Top, Tree Rex, Drobot, Anvil Rain, Dragon's Peak");

  // Imaginators: "Pain-Yatta" is the Villain Sensei, not the Trap Team villain trap.
  const char* picks6[] = {"Pain-Yatta"};
  lib.SetPicks(picks6, 1);
  lib.SetGameFilter(6);
  lib.SetPicksMode(true);
  lib.Scan();
  got = Join(Names(lib));
  printf("  Imaginators, My Picks: %s\n", got.c_str());
  CHECK(got == "Pain-Yatta, Fire Angel, Air Toucan, Pain-Yatta, Hot Streak, Anvil Rain, Dragon's Peak");
  CHECK(lib.GetCategory(lib.BrowseAt(0)) == FigureLibrary::kCatGimmick);
  CHECK(lib.PicksFound() == 1);

  // Rosters (picks.ini sections): the running game's first, then back to Spyro's Adventure, each
  // in the usual order; a figure picked in two rosters is in both; a SuperCharger brings its
  // vehicle. Then everything that isn't a Skylander.
  const char* picks_r[] = {"Pain-Yatta", "Drobot (series 1)", "Spitfire", "Tree Rex", "Drobot",
                           "Spyro", "Stealth Elf", "Spyro"};
  const signed char rosters[] = {6, 6, 5, 2, 2, 2, 1, 1};
  lib.SetPicks(picks_r, 8, rosters);
  lib.SetPicksMode(true);
  lib.Resort();
  got = Join(Names(lib));
  printf("  Imaginators, My Picks by roster: %s\n", got.c_str());
  CHECK(got == "Pain-Yatta, Drobot, "         // Imaginators' roster
               "Spitfire, Hot Streak, "       // SuperChargers'
               "Tree Rex, Spyro, Drobot, "    // Giants'
               "Spyro, Stealth Elf, "         // Spyro's Adventure's
               "Fire Angel, Air Toucan, Pain-Yatta, Hot Streak, Anvil Rain, Dragon's Peak");  // the rest
  CHECK(lib.PicksFound() == 8);
  CHECK(lib.RosterAt(0) == 6 && lib.RosterAt(1) == 6 && lib.RosterAt(2) == 5 && lib.RosterAt(3) == 5);
  CHECK(lib.RosterAt(4) == 2 && lib.RosterAt(7) == 1 && lib.RosterAt(8) == 1 && lib.RosterAt(9) == 0);
  CHECK(lib.Game(lib.BrowseAt(1)) == 1 && lib.Game(lib.BrowseAt(6)) == 2);  // Series 1, Series 2
  CHECK(lib.BrowseAt(5) == lib.BrowseAt(7));                                // Spyro in both
  CHECK(lib.GroupJump(0, +1) == 1);   // Gimmick/Magic -> Skylanders/Tech
  CHECK(lib.GroupJump(1, +1) == 2);   // -> the next roster
  CHECK(lib.GroupJump(3, +1) == 4);   // Hot Streak (vehicle) -> Giants' roster
  CHECK(lib.GroupJump(5, +1) == 6);   // Spyro (Magic) -> Drobot (Tech)
  CHECK(lib.GroupJump(6, +1) == 7);   // Giants' roster -> Spyro's Adventure's (Spyro again)
  CHECK(lib.GroupJump(7, -1) == 6);
  CHECK(lib.GroupJump(8, +1) == 9);   // Spyro's Adventure's roster -> Creation Crystals
  CHECK(lib.SectionLabel(9, label, sizeof(label)) && !strcmp(label, "Creation Crystals / Fire"));
  CHECK(lib.SectionLabel(3, label, sizeof(label)) && !strcmp(label, "Vehicles / Fire"));
  // Rescanning keeps the rosters.
  lib.Scan();
  CHECK(Join(Names(lib)) == got);
  lib.SetPicks(picks6, 1);

  // One game's own figures (R3 after My Picks): every release from that game's folder.
  lib.SetPicksMode(false);
  CHECK(lib.CountFromGame(4) == 4 && lib.CountFromGame(2) == 2 && lib.CountFromGame(5) == 4);
  lib.SetViewGame(4);
  lib.Resort();
  got = Join(Names(lib));
  printf("  Imaginators, Trap Team's figures: %s\n", got.c_str());
  CHECK(got == "Wildfire, Eon's Elite Spyro, Air Toucan, Pain-Yatta");
  lib.SetViewGame(0);

  // A list of your own (Favorites, a list made on the phone): its figures in its order, each
  // once, only those the game filter lets through; unknown ids are skipped.
  {
    const int wildfire = lib.FindByName("Wildfire"), toucan = lib.FindByName("Air Toucan");
    const int spyro = lib.FindByName("Spyro");
    CHECK(wildfire >= 0 && toucan >= 0 && spyro >= 0);
    short ids[6] = {(short)toucan, 9999, (short)wildfire, (short)toucan, -1, (short)spyro};
    lib.SetListView(ids, 6, 3);
    lib.Resort();
    CHECK(lib.ListTag() == 3);
    got = Join(Names(lib));
    printf("  Imaginators, a list: %s\n", got.c_str());
    CHECK(lib.BrowseCount() == 3 && lib.BrowseAt(0) == toucan && lib.BrowseAt(1) == wildfire &&
          lib.BrowseAt(2) == spyro);
    lib.SetGameFilter(2);  // Giants: Trap Team's figures can't be used
    lib.Scan();
    CHECK(lib.BrowseCount() == 1 && lib.BrowseAt(0) == spyro);
    lib.SetGameFilter(6);
    lib.SetListView(ids, 0, 4);  // an empty list
    lib.Resort();
    CHECK(lib.BrowseCount() == 0 && lib.ListTag() == 4);
    lib.SetListView(NULL, -1, 0);
    lib.Scan();
    CHECK(lib.ListTag() == -1 && lib.BrowseCount() > 3);
  }

  // Back to everything, then the old folder order.
  lib.SetPicksMode(false);
  lib.SetSortByType(false);
  lib.SetNewestOnly(false);
  lib.Scan();
  got = Join(Names(lib));
  printf("  Imaginators, by folder: %s\n", got.c_str());
  CHECK(Names(lib)[0] == "My Spyro" && Names(lib)[1] == "Spyro");  // loose first, then SSA's figures
  CHECK(!lib.SectionLabel(0, label, sizeof(label)));
  system("rm -rf figures_fixture");
}

// A report for a real pack: what each game lists, by type, and what the picks resolve to.
// picks.ini's names for one game (sections named as in the menu), split at commas.
static std::vector<std::string> IniPicks(const char* path, const char* game) {
  std::vector<std::string> out;
  FILE* f = fopen(path, "rb");
  if (!f) return out;
  char line[1024];
  bool in = false;
  while (fgets(line, sizeof(line), f)) {
    std::string l = line;
    while (!l.empty() && (l[l.size() - 1] == '\n' || l[l.size() - 1] == '\r' || l[l.size() - 1] == ' '))
      l.erase(l.size() - 1);
    if (l.empty() || l[0] == ';') continue;
    if (l[0] == '[') {
      in = l == std::string("[") + game + "]";
      continue;
    }
    const size_t eq = l.find('=');
    if (!in || eq == std::string::npos) continue;
    std::string v = l.substr(eq + 1);
    size_t start = 0;
    while (start <= v.size()) {
      size_t comma = v.find(',', start);
      if (comma == std::string::npos) comma = v.size();
      std::string n = v.substr(start, comma - start);
      while (!n.empty() && n[0] == ' ') n.erase(0, 1);
      while (!n.empty() && n[n.size() - 1] == ' ') n.erase(n.size() - 1);
      if (!n.empty()) out.push_back(n);
      start = comma + 1;
    }
  }
  fclose(f);
  return out;
}

static void Report(const char* root, const char* picks_ini) {
  static const char* const kGames[] = {"", "Spyro's Adventure", "Giants", "Swap Force", "Trap Team",
                                       "SuperChargers", "Imaginators"};
  static const char* const kPicks[] = {
      "",
      "Drobot,Chop Chop,Stealth Elf,Whirlwind,Terrafin,Slam Bam,Double Trouble,Flameslinger",
      "Bouncer,Drobot,Eye-Brawl,Chop Chop,Tree Rex,Stealth Elf,Swarm,Jet-Vac,Crusher,Bash,Thumpback,"
      "Chill,Ninjini,Double Trouble,Hot Head,Ignitor",
      "Magna Charge,Countdown,Night Shift,Cynder,Stink Bomb,Bumble Blast,Boom Jet,Scratch,Doom Stone,"
      "Terrafin,Wash Buckler,Wham-Shell,Hoot Loop,Star Strike,Blast Zone,Smolderdash",
      "Gearshift,Tread Head,Short Cut,Cynder,Tuff Luck,Stealth Elf,Gusto,Blades,Wallop,Fist Bump,"
      "Snap Shot,Lob-Star,Blastermind,Deja Vu,Wildfire,Torch,Knight Light,Spotlight,Knight Mare,Blackout",
      "Double Dare Trigger Happy,Bone Bash Roller Brawl,Super Shot Stealth Elf,Stormblade,"
      "Shark Shooter Terrafin,Deep Dive Gill Grunt,Big Bubble Pop Fizz,Spitfire,Astroblast,Nightfall",
      "Ro-Bow,Tread Head,Chopscotch,Cynder,Chompy Mage,Stealth Elf,Air Strike,Jet-Vac,Grave Clobber,"
      "Smash Hit,Tidepool,Snap Shot,Pain-Yatta,Pop Fizz,Flare Wolf,Trail Blazer,Blaster-Tron,Spotlight,"
      "Hood Sickle,Knight Mare"};
  FigureLibrary lib;
  lib.SetRoot(root);
  if (!lib.LoadIndex()) {
    printf("no UI/figures.idx in %s\n", root);
    return;
  }
  static const char* const kTypeOrder[] = {"gimmick", "Skylanders", "crystals", "traps",
                                             "vehicles", "magic items", "adventure packs"};
  for (int g = 1; g <= 6; ++g) {
    lib.SetGameFilter(g);
    lib.SetNewestOnly(false);  // the shipped default
    lib.SetPicksMode(false);
    lib.SetViewGame(0);
    const int n = lib.Scan();
    printf("\n== %s: ALL lists %d (%d for later games)\n", kGames[g], n, lib.HiddenCount());
    // ALL: the types in the menu's order, each once, in one run.
    std::string runs;
    int last = -1;
    for (int p = 0; p < n; ++p) {
      const int c = lib.GetCategory(lib.BrowseAt(p));
      if (c != last) runs += std::string(runs.empty() ? "" : ", ") + FigureLibrary::CategoryName(c);
      if (last >= 0 && FigureLibrary::TypeRank(c) < FigureLibrary::TypeRank(last)) runs += " (OUT OF ORDER)";
      last = c;
    }
    printf("   ALL order: %s\n", runs.c_str());
    int per[FigureLibrary::kCatCount] = {0};
    for (int p = 0; p < n; ++p) ++per[lib.GetCategory(lib.BrowseAt(p))];
    printf("   ALL by type:");
    for (int c = 0; c < FigureLibrary::kCatCount; ++c) printf(" %s %d;", FigureLibrary::CategoryName(c), per[c]);
    printf("\n");
    (void)kTypeOrder;
    // Each game's own list.
    for (int v = 1; v <= g; ++v) {
      lib.SetViewGame(v);
      lib.Resort();
      printf("   %s figures: %d\n", kGames[v], lib.BrowseCount());
    }
    lib.SetViewGame(0);
    // My Picks: this game's roster and the earlier games', as the plugin loads them.
    std::vector<std::string> names;
    std::vector<signed char> rosters;
    for (int r = g; r >= 1; --r) {
      std::vector<std::string> part;
      if (picks_ini) {
        part = IniPicks(picks_ini, kGames[r]);
      } else {
        std::string all = kPicks[r];
        size_t start = 0;
        while (start < all.size()) {
          size_t comma = all.find(',', start);
          if (comma == std::string::npos) comma = all.size();
          part.push_back(all.substr(start, comma - start));
          start = comma + 1;
        }
      }
      for (size_t i = 0; i < part.size(); ++i) {
        names.push_back(part[i]);
        rosters.push_back((signed char)r);
      }
    }
    std::vector<const char*> ptrs;
    for (size_t i = 0; i < names.size(); ++i) ptrs.push_back(names[i].c_str());
    lib.SetPicks(&ptrs[0], (int)ptrs.size(), &rosters[0]);
    lib.SetPicksMode(true);
    const int m = lib.Scan();
    printf("   My Picks: %d of %d found, %d listed:\n", lib.PicksFound(), (int)names.size(), m);
    for (int p = 0; p < lib.PickCount(); ++p) {
      if (!lib.PickFound(p)) printf("     NOT FOUND: %s\n", lib.PickText(p));
    }
    for (int p = 0; p < m; ++p) {
      const int id = lib.BrowseAt(p);
      char label[96], series[40], heading[140];
      lib.SectionLabel(p, label, sizeof(label));
      lib.Series(id, series, sizeof(series));
      snprintf(heading, sizeof(heading), "%s Roster / %s", kGames[lib.RosterAt(p)], label);
      printf("     %-52s %-26s %s (game %d)\n", heading, lib.Name(id), series, lib.Game(id));
    }
  }
}

int main(int argc, char** argv) {
  TestFixture();
  if (argc > 1) Report(argv[1], argc > 2 ? argv[2] : NULL);  // [picks.ini]
  if (g_fail) {
    printf("%d check(s) failed\n", g_fail);
    return 1;
  }
  printf("all figure library tests passed\n");
  return 0;
}
