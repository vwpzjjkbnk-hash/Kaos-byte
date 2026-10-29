// SkyPortal360 - the figure library. See figures.h.
#include "figures.h"

#include <stdlib.h>

#include "elements.h"
#include "util.h"

namespace {

bool HasIgnoredExtension(const char* file) {
  const char* dot = strrchr(file, '.');
  if (!dot) return false;
  static const char* kIgnored[] = {".txt", ".ini", ".log", ".bak", ".tmp", ".xex",
                                   ".nfc", ".json", ".qoi", ".idx", ".png", ".jpg"};
  for (size_t i = 0; i < sizeof(kIgnored) / sizeof(kIgnored[0]); ++i) {
    if (!_stricmp(dot, kIgnored[i])) return true;
  }
  return false;
}

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

bool KeyChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

// The index key of a path relative to Figures (tools/mkassets.py makes the same keys): every
// folder and the file name without its extension, lower-case letters and digits only, joined
// with '/'. With `base_only`, "*" and the file name alone (for dumps outside the pack layout).
void MakeKey(const char* rel, bool base_only, char* out, int cap) {
  const char* start = rel;
  if (base_only) {
    const char* slash = strrchr(rel, '\\');
    if (slash) start = slash + 1;
  }
  const char* dot = strrchr(start, '.');
  const char* end = (dot && !strchr(dot, '\\')) ? dot : start + strlen(start);
  int n = 0;
  if (base_only && n < cap - 1) out[n++] = '*';
  for (const char* p = start; p < end && n < cap - 1; ++p) {
    const char c = Lower(*p);
    if (c == '\\' || c == '/') {
      out[n++] = '/';
    } else if (KeyChar(c)) {
      out[n++] = c;
    }
  }
  out[n] = 0;
}

unsigned int Hash(const char* s) {
  unsigned int h = 2166136261u;
  for (; *s; ++s) {
    h ^= (unsigned char)*s;
    h *= 16777619u;
  }
  return h;
}

// "Eye-Brawl", "Eye Brawl" -> "eyebrawl": names compared without case, spaces or punctuation.
void Normalize(const char* in, char* out, int cap) {
  int n = 0;
  for (; *in && n < cap - 1; ++in) {
    const char c = Lower(*in);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[n++] = c;
  }
  out[n] = 0;
}

// The same without anything in brackets: "Corrupted Heartbreaker Buckshot (Max Level Top Path)"
// -> "corruptedheartbreakerbuckshot".
void NormalizeBase(const char* in, char* out, int cap) {
  int n = 0, depth = 0;
  for (; *in && n < cap - 1; ++in) {
    const char c = Lower(*in);
    if (c == '(') ++depth;
    else if (c == ')' && depth) --depth;
    else if (!depth && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) out[n++] = c;
  }
  out[n] = 0;
}

// A figure name as picks.ini matches it: normalized, with "LightCore" taken out wherever it is
// ("LightCore Smolderdash", "Smolderdash LightCore") and reported in *lightcore.
void PickKey(const char* in, char* out, int cap, bool* lightcore) {
  Normalize(in, out, cap);
  *lightcore = false;
  for (char* at = out; *at; ++at) {
    if (!strncmp(at, "lightcore", 9)) {
      memmove(at, at + 9, strlen(at + 9) + 1);
      *lightcore = true;
      break;
    }
  }
}

// Edit distance between two short names, or -1 when it's over `max`.
int Distance(const char* a, const char* b, int max) {
  const int la = (int)strlen(a), lb = (int)strlen(b);
  if (la - lb > max || lb - la > max || la > 63 || lb > 63) return -1;
  int row[64];
  for (int j = 0; j <= lb; ++j) row[j] = j;
  for (int i = 1; i <= la; ++i) {
    int diag = row[0], low = i;
    row[0] = i;
    for (int j = 1; j <= lb; ++j) {
      const int up = row[j];
      int v = diag + (a[i - 1] != b[j - 1]);
      if (up + 1 < v) v = up + 1;
      if (row[j - 1] + 1 < v) v = row[j - 1] + 1;
      row[j] = v;
      diag = up;
      if (v < low) low = v;
    }
    if (low > max) return -1;
  }
  return row[lb] <= max ? row[lb] : -1;
}

bool EndsWith(const char* s, const char* tail) {
  const size_t ls = strlen(s), lt = strlen(tail);
  return lt && lt <= ls && !strcmp(s + ls - lt, tail);
}

bool ContainsNoCase(const char* s, const char* word) {  // word in lower case
  for (; *s; ++s) {
    int k = 0;
    while (word[k] && Lower(s[k]) == word[k]) ++k;
    if (!word[k]) return true;
  }
  return false;
}

bool OneOf(const char* s, const char* const* list) {
  for (; *list; ++list) {
    if (!strcmp(s, *list)) return true;
  }
  return false;
}

const char* const kTrapSeries[] = {"Trap", "Trap Variant", "Villain", "Villain Variant", 0};
const char* const kVehicleSeries[] = {"Land Vehicle", "Sea Vehicle", "Sky Vehicle", 0};
const char* const kAdventureSeries[] = {"Adventure Pack", "Expansion Pack", "Battle Arena", 0};
const char* const kItemSeries[] = {"Magic Item", "Treasure Chest", "Promo Item", 0};
const char* const kCrystalSeries[] = {"Creation Crystal", "Premade Creation", 0};
const char* const kGimmickSeries[] = {"Giant", "Swapper Top", "Swapper Bottom", "Trap Master",
                                      "SuperCharger", "Sensei", "Villain Sensei", 0};
const char* const kRegularSeries[] = {"Series 1", "Series 2", "Series 3", "Series 4", "Legendary",
                                      "LightCore", "Eon's Elite", "Mini", "Sidekick", 0};

// Character ids (figures.idx column 7), so a variant or an unreleased figure sorts with the
// figure it's a version of. SuperChargers share their character's id (Super Shot Stealth Elf is
// Stealth Elf's 26), so an id alone doesn't make a gimmick Skylander: it takes a gimmick figure
// with that id from the same game, or an id that only gimmick figures have.
enum { kIdBits = 8192, kGames = 7 };
unsigned char g_gimmick_ids[kGames][kIdBits / 8];  // by game
unsigned char g_gimmick_any[kIdBits / 8];
unsigned char g_regular_any[kIdBits / 8];
unsigned char g_crystal_ids[kIdBits / 8];
void SetBit(unsigned char* bits, int id) {
  if (id > 0 && id < kIdBits) bits[id >> 3] |= (unsigned char)(1 << (id & 7));
}
bool HasBit(const unsigned char* bits, int id) {
  return id > 0 && id < kIdBits && (bits[id >> 3] & (1 << (id & 7)));
}

// Length of a path's folder part (up to its last backslash; 0 for a loose file).
int DirLen(const char* rel) {
  const char* s = strrchr(rel, '\\');
  return s ? (int)(s - rel) : 0;
}

// Folder order: CompareRel on the folder parts only.
int CompareDir(const char* a, const char* b) {
  const int la = DirLen(a), lb = DirLen(b);
  if ((la == 0) != (lb == 0)) return la == 0 ? -1 : 1;  // loose files first
  for (int i = 0;; ++i) {
    char x = i < la ? Lower(a[i]) : 0, y = i < lb ? Lower(b[i]) : 0;
    if (x == '\\') x = 1;
    if (y == '\\') y = 1;
    if (x != y || !x) return (int)(unsigned char)x - (int)(unsigned char)y;
  }
}

int CompareNoCase(const char* a, const char* b) {
  for (;; ++a, ++b) {
    const char x = Lower(*a), y = Lower(*b);
    if (x != y || !x) return (int)(unsigned char)x - (int)(unsigned char)y;
  }
}

// Compares two relative paths for browsing. Dumps kept loose at the top of Figures (your own
// figures, usually the ones with game progress) come first; the rest follow folder by folder,
// case-insensitively.
int CompareRel(const char* a, const char* b) {
  const bool loose_a = !strchr(a, '\\'), loose_b = !strchr(b, '\\');
  if (loose_a != loose_b) return loose_a ? -1 : 1;
  for (;; ++a, ++b) {
    char x = Lower(*a), y = Lower(*b);
    if (x == '\\') x = 1;  // a folder separator sorts before any character
    if (y == '\\') y = 1;
    if (x != y || !x) return (int)(unsigned char)x - (int)(unsigned char)y;
  }
}

}  // namespace

FigureLibrary::FigureLibrary()
    : entries_(NULL), count_(0), browse_(NULL), browse_count_(0), filter_game_(0), hidden_count_(0),
      newest_only_(false), older_count_(0), unreadable_dirs_(0), index_(NULL), index_size_(0),
      index_hash_(NULL), index_off_(NULL), index_lines_(0), sort_by_type_(true), pick_count_(0),
      view_game_(0), picks_mode_(false), picks_found_(0) {
  root_[0] = 0;
  entries_ = new Entry[kMaxFigures];
  browse_ = new int[kMaxFigures];
  memset(entries_, 0, sizeof(Entry) * kMaxFigures);
}

void FigureLibrary::SetRoot(const char* root) { strncpy_s(root_, sizeof(root_), root, _TRUNCATE); }

bool FigureLibrary::LoadIndex() {
  char path[MAX_PATH];
  sprintf_s(path, sizeof(path), "%s\\UI\\figures.idx", root_);
  HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return false;
  const DWORD size = GetFileSize(h, NULL);
  if (size == 0 || size > 4 * 1024 * 1024) {
    CloseHandle(h);
    return false;
  }
  index_ = new char[size + 1];
  DWORD got = 0;
  const BOOL ok = ReadFile(h, index_, size, &got, NULL);
  CloseHandle(h);
  if (!ok || got != size) return false;
  index_[size] = 0;
  index_size_ = (int)size;

  int lines = 0;
  for (DWORD i = 0; i < size; ++i) lines += index_[i] == '\n';
  index_hash_ = new unsigned int[lines + 1];
  index_off_ = new int[lines + 1];
  index_lines_ = 0;
  int off = 0;
  while (off < index_size_) {
    int end = off;
    while (end < index_size_ && index_[end] != '\n') ++end;
    if (end > off && index_[off] != '#') {
      char key[kRelLen];
      int n = 0;
      for (int i = off; i < end && index_[i] != '\t' && n < kRelLen - 1; ++i) key[n++] = index_[i];
      key[n] = 0;
      index_hash_[index_lines_] = Hash(key);
      index_off_[index_lines_] = off;
      ++index_lines_;
    }
    off = end + 1;
  }
  // Sort (hash, offset) pairs by hash: shell sort.
  for (int gap = index_lines_ / 2; gap > 0; gap /= 2) {
    for (int i = gap; i < index_lines_; ++i) {
      const unsigned int hv = index_hash_[i];
      const int ov = index_off_[i];
      int j = i;
      while (j >= gap && index_hash_[j - gap] > hv) {
        index_hash_[j] = index_hash_[j - gap];
        index_off_[j] = index_off_[j - gap];
        j -= gap;
      }
      index_hash_[j] = hv;
      index_off_[j] = ov;
    }
  }
  memset(g_gimmick_ids, 0, sizeof(g_gimmick_ids));
  memset(g_gimmick_any, 0, sizeof(g_gimmick_any));
  memset(g_regular_any, 0, sizeof(g_regular_any));
  memset(g_crystal_ids, 0, sizeof(g_crystal_ids));
  for (int i = 0; i < index_lines_; ++i) {
    char series[40], id[16], game[8];
    if (!IndexField(index_off_[i], 4, series, sizeof(series)) ||
        !IndexField(index_off_[i], 7, id, sizeof(id)) ||
        !IndexField(index_off_[i], 2, game, sizeof(game))) {
      continue;
    }
    const int toy = (int)strtoul(id, NULL, 10);
    const int g = (int)strtoul(game, NULL, 10);
    if (OneOf(series, kGimmickSeries)) {
      SetBit(g_gimmick_any, toy);
      if (g > 0 && g < kGames) SetBit(g_gimmick_ids[g], toy);
    }
    if (OneOf(series, kRegularSeries)) SetBit(g_regular_any, toy);
    if (OneOf(series, kCrystalSeries)) SetBit(g_crystal_ids, toy);
  }
  return true;
}

int FigureLibrary::FindIndexLine(const char* rel) const {
  if (!index_lines_) return -1;
  for (int pass = 0; pass < 2; ++pass) {
    char key[kRelLen];
    MakeKey(rel, pass == 1, key, sizeof(key));
    const unsigned int h = Hash(key);
    int lo = 0, hi = index_lines_;
    while (lo < hi) {
      const int mid = (lo + hi) / 2;
      if (index_hash_[mid] < h) lo = mid + 1;
      else hi = mid;
    }
    for (int i = lo; i < index_lines_ && index_hash_[i] == h; ++i) {
      const char* line = index_ + index_off_[i];
      const size_t n = strlen(key);
      if (!strncmp(line, key, n) && line[n] == '\t') return index_off_[i];
    }
  }
  return -1;
}

int FigureLibrary::FindKeyLine(const char* key) const {
  if (!index_lines_) return -1;
  const unsigned int h = Hash(key);
  int lo = 0, hi = index_lines_;
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    if (index_hash_[mid] < h) lo = mid + 1;
    else hi = mid;
  }
  const size_t n = strlen(key);
  for (int i = lo; i < index_lines_ && index_hash_[i] == h; ++i) {
    const char* line = index_ + index_off_[i];
    if (!strncmp(line, key, n) && line[n] == '\t') return index_off_[i];
  }
  return -1;
}

bool FigureLibrary::IndexField(int off, int field, char* out, int cap) const {
  if (cap <= 0) return false;
  out[0] = 0;
  if (off < 0 || !index_) return false;
  int i = off;
  for (int f = 0; f < field; ++f) {
    while (i < index_size_ && index_[i] != '\t' && index_[i] != '\n') ++i;
    if (i >= index_size_ || index_[i] != '\t') return false;
    ++i;
  }
  int n = 0;
  while (i < index_size_ && index_[i] != '\t' && index_[i] != '\n' && index_[i] != '\r' &&
         n < cap - 1) {
    out[n++] = index_[i++];
  }
  out[n] = 0;
  return n > 0;
}

const char* FigureLibrary::Name(int id) const {
  return (id >= 0 && id < count_) ? entries_[id].name : "?";
}

int FigureLibrary::Element(int id) {
  if (id < 0 || id >= count_) return elements::kElementUnknown;
  Entry& e = entries_[id];
  if (e.element < 0) {
    elements::Element el = elements::kElementUnknown;
    char field[8];
    if (IndexField(e.index_line, 3, field, sizeof(field))) el = (elements::Element)strtoul(field, NULL, 10);
    if (el == elements::kElementUnknown) el = elements::FromPath(e.rel);
    if (el == elements::kElementUnknown) {
      static sp::u8 dump[sp::kFigureSize];
      if (Load(id, dump)) el = elements::FromToyId(elements::ToyId(dump));
    }
    e.element = (signed char)el;
  }
  return e.element;
}

int FigureLibrary::Game(int id) const {
  if (id < 0 || id >= count_) return 0;
  char field[8];
  if (IndexField(entries_[id].index_line, 2, field, sizeof(field))) return (int)strtoul(field, NULL, 10);
  // NFC pack layout: the top folder starts with the game number ("4. Trap Team").
  const char* rel = entries_[id].rel;
  if (rel[0] >= '1' && rel[0] <= '6' && rel[1] == '.' && strchr(rel, '\\')) return rel[0] - '0';
  return 0;
}

int FigureLibrary::FirstGame(int id) {
  if (id < 0 || id >= count_) return 0;
  Entry& e = entries_[id];
  if (e.first_game < 0) {
    int g = 0;
    char field[16];
    if (IndexField(e.index_line, 6, field, sizeof(field))) g = (int)strtoul(field, NULL, 10);
    if (g < 1 || g > 6) g = 0;
    if (!g && e.index_line >= 0) {
      g = Game(id);  // an index from before 1.5: the game folder it's in
    } else if (!g && index_lines_) {
      // Not in the index (a dump of your own): look its character up ("@<id>" lines).
      static sp::u8 dump[sp::kFigureSize];
      if (Load(id, dump)) {
        char key[16];
        sprintf_s(key, sizeof(key), "@%d", elements::ToyId(dump));
        if (IndexField(FindKeyLine(key), 1, field, sizeof(field))) g = (int)strtoul(field, NULL, 10);
      }
      if (g < 1 || g > 6) g = 0;
    }
    e.first_game = (signed char)g;
  }
  return e.first_game;
}

void FigureLibrary::ReadRelease(int id) {
  Entry& e = entries_[id];
  if (e.kind) return;
  char field[16];
  e.kind = '-';
  e.family = 0;
  if (IndexField(e.index_line, 8, field, sizeof(field)) && (field[0] == 'R' || field[0] == 'V')) {
    e.kind = field[0];
    if (IndexField(e.index_line, 7, field, sizeof(field))) e.family = (int)strtoul(field, NULL, 10);
    if (e.family <= 0) e.kind = '-';
  }
}

bool FigureLibrary::IsUnreleased(int id) const {
  if (id < 0 || id >= count_) return false;
  // "...\8) Unreleased\..." folders, and "Heartbreaker Buckshot (Unreleased)" elsewhere.
  return ContainsNoCase(entries_[id].rel, "unreleased") ||
         ContainsNoCase(entries_[id].name, "unreleased");
}

int FigureLibrary::CharacterId(int id) const {
  char field[16];
  if (id < 0 || id >= count_ || !IndexField(entries_[id].index_line, 7, field, sizeof(field))) return 0;
  return (int)strtoul(field, NULL, 10);
}

const char* FigureLibrary::CategoryName(int category) {
  static const char* const kNames[kCatCount] = {
      "Gimmick Skylanders", "Creation Crystals", "Skylanders", "Vehicles", "Traps",
      "Adventure Packs", "Magic Items"};
  return category >= 0 && category < kCatCount ? kNames[category] : "";
}

int FigureLibrary::GetCategory(int id) {
  if (id < 0 || id >= count_) return kCatSkylander;
  Entry& e = entries_[id];
  if (e.category >= 0) return e.category;
  char series[40];
  Series(id, series, sizeof(series));
  int c = -1;
  if (OneOf(series, kTrapSeries)) c = kCatTrap;
  else if (OneOf(series, kVehicleSeries)) c = kCatVehicle;
  else if (OneOf(series, kAdventureSeries)) c = kCatAdventure;
  else if (OneOf(series, kItemSeries)) c = kCatItem;
  else if (OneOf(series, kCrystalSeries)) c = kCatCrystal;
  else if (OneOf(series, kGimmickSeries)) c = kCatGimmick;
  else if (OneOf(series, kRegularSeries)) {
    const int toy = CharacterId(id);  // Legendary Sun Runner is a vehicle all the same
    c = toy >= 3200 && toy < 3300 ? kCatVehicle : kCatSkylander;
  }
  if (c < 0) {
    // Variants, re-releases, unreleased figures and files not in the index: by character id.
    int toy = CharacterId(id);
    if (toy <= 0) {
      static sp::u8 dump[sp::kFigureSize];
      if (Load(id, dump)) toy = elements::ToyId(dump);
    }
    const int g = Game(id);
    if ((g > 0 && g < kGames && HasBit(g_gimmick_ids[g], toy)) ||
        (HasBit(g_gimmick_any, toy) && !HasBit(g_regular_any, toy))) {
      c = kCatGimmick;
    } else if (HasBit(g_crystal_ids, toy)) {
      c = kCatCrystal;
    } else if (toy >= 210 && toy <= 220) {
      c = kCatTrap;
    } else if (toy >= 3200 && toy < 3300) {
      c = kCatVehicle;
    } else if ((toy >= 300 && toy < 400) || (toy >= 3300 && toy < 3400)) {
      c = kCatAdventure;
    } else if ((toy >= 200 && toy < 300) || (toy >= 3500 && toy < 3600)) {
      c = kCatItem;
    } else if (toy >= 1000 && toy < 3000) {
      c = kCatGimmick;  // swapper halves
    } else {
      c = kCatSkylander;
    }
  }
  e.category = (signed char)c;
  return c;
}

bool FigureLibrary::SectionLabel(int id, char* out, int cap) {
  if (!sort_by_type_ || id < 0 || id >= count_) return false;
  const int c = GetCategory(id);
  const char* el = elements::Name((elements::Element)Element(id));
  if (el[0]) {
    sprintf_s(out, cap, "%s / %s", CategoryName(c), el);
  } else {
    strncpy_s(out, cap, CategoryName(c), _TRUNCATE);
  }
  return true;
}

void FigureLibrary::SetPicks(const char* const* names, int count) {
  pick_count_ = 0;
  for (int i = 0; i < count && pick_count_ < kMaxPicks; ++i) {
    Pick& p = picks_[pick_count_];
    strncpy_s(p.text, sizeof(p.text), names[i], _TRUNCATE);
    p.game = 0;
    p.series = 0;
    p.found = false;
    // "(Series 2)" / "Series 2" anywhere: that series.
    char text[96];
    strncpy_s(text, sizeof(text), names[i], _TRUNCATE);
    for (char* t = text; *t; ++t) {
      if (Lower(t[0]) != 's' || Lower(t[1]) != 'e' || Lower(t[2]) != 'r' || Lower(t[3]) != 'i' ||
          Lower(t[4]) != 'e' || Lower(t[5]) != 's') {
        continue;
      }
      char* d = t + 6;
      while (*d == ' ') ++d;
      if (*d < '1' || *d > '9') continue;
      p.series = (signed char)(*d - '0');
      char* from = t;
      if (from > text && from[-1] == '(') --from;
      char* to = d + 1;
      if (*to == ')') ++to;
      memmove(from, to, strlen(to) + 1);
      break;
    }
    PickKey(text, p.name, kPickLen, &p.lightcore);
    // A game's name in front: that game's release.
    static const struct { const char* name; int game; } kGames[] = {
        {"spyrosadventure", 1}, {"spyroadventure", 1}, {"giants", 2}, {"swapforce", 3},
        {"trapteam", 4}, {"superchargers", 5}, {"imaginators", 6}};
    for (int g = 0; g < (int)(sizeof(kGames) / sizeof(kGames[0])); ++g) {
      const int len = (int)strlen(kGames[g].name);
      if (!strncmp(p.name, kGames[g].name, len) && p.name[len]) {
        p.game = (signed char)kGames[g].game;
        memmove(p.name, p.name + len, strlen(p.name + len) + 1);
        break;
      }
    }
    if (p.name[0]) ++pick_count_;
  }
  if (!pick_count_) picks_mode_ = false;
}

int FigureLibrary::CountFromGame(int game) {
  int n = 0;
  for (int i = 0; i < count_; ++i) {
    if (!entries_[i].exists || Game(i) != game) continue;
    if (filter_game_ > 0 && FirstGame(i) > filter_game_) continue;
    ++n;
  }
  return n;
}

int FigureLibrary::SeriesNumber(int id) const {
  char series[40];
  Series(id, series, sizeof(series));
  return !strncmp(series, "Series ", 7) && series[7] >= '1' && series[7] <= '9' ? series[7] - '0' : 0;
}

void FigureLibrary::Series(int id, char* out, int cap) const {
  out[0] = 0;
  if (id >= 0 && id < count_) IndexField(entries_[id].index_line, 4, out, cap);
}

bool FigureLibrary::ImagePath(int id, char* out, int cap) const {
  if (id < 0 || id >= count_) return false;
  char image[kRelLen];
  if (!IndexField(entries_[id].index_line, 5, image, sizeof(image)) || image[0] == '-') {
    return false;
  }
  sprintf_s(out, cap, "%s\\Images\\%s", root_, image);
  return true;
}

const char* FigureLibrary::Rel(int id) const {
  return (id >= 0 && id < count_) ? entries_[id].rel : "";
}

int FigureLibrary::BrowseAt(int pos) const {
  return (pos >= 0 && pos < browse_count_) ? browse_[pos] : -1;
}

int FigureLibrary::BrowsePos(int id) const {
  for (int i = 0; i < browse_count_; ++i) {
    if (browse_[i] == id) return i;
  }
  return -1;
}

bool FigureLibrary::SameSection(int a, int b) const {
  if (sort_by_type_) {
    return entries_[a].category == entries_[b].category &&
           entries_[a].element == entries_[b].element;
  }
  const char* x = entries_[a].rel;
  const char* y = entries_[b].rel;
  const int lx = DirLen(x), ly = DirLen(y);
  if (lx != ly || _strnicmp(x, y, lx)) return false;
  return entries_[a].element == entries_[b].element;
}

int FigureLibrary::GroupJump(int pos, int dir) const {
  const int n = browse_count_;
  if (n <= 1) return pos;
  // A section is a folder, or one element's figures within a folder.
  const int here = browse_[pos];
  int p = pos;
  if (dir > 0) {
    for (int k = 0; k < n; ++k) {
      p = (p + 1) % n;
      if (!SameSection(browse_[p], here)) return p;
    }
    return pos;
  }
  // Back: to the start of this section if not already there, else the start of the previous one.
  int start = pos;
  while (start > 0 && SameSection(browse_[start - 1], here)) --start;
  if (start != pos) return start;
  p = (pos - 1 + n) % n;
  const int prev = browse_[p];
  while (p > 0 && SameSection(browse_[p - 1], prev)) --p;
  return p;
}

int FigureLibrary::FindByRel(const char* rel) const {
  for (int i = 0; i < count_; ++i) {
    if (!_stricmp(entries_[i].rel, rel)) return i;
  }
  return -1;
}

int FigureLibrary::FindByName(const char* name) const {
  for (int i = 0; i < count_; ++i) {
    if (!entries_[i].exists) continue;
    if (!_stricmp(entries_[i].name, name)) return i;
    // Also the file name, so startup figures written as file names keep working.
    const char* base = strrchr(entries_[i].rel, '\\');
    base = base ? base + 1 : entries_[i].rel;
    const char* dot = strrchr(base, '.');
    const size_t len = dot ? (size_t)(dot - base) : strlen(base);
    if (strlen(name) == len && !_strnicmp(base, name, (int)len)) return i;
  }
  return -1;
}

void FigureLibrary::FullPath(int id, char* out, size_t cap) const {
  sprintf_s(out, cap, "%s\\Figures\\%s", root_, Rel(id));
}

void FigureLibrary::AddFile(const char* rel) {
  int id = FindByRel(rel);
  if (id < 0) {
    if (count_ >= kMaxFigures) return;
    id = count_++;
    Entry& e = entries_[id];
    strncpy_s(e.rel, sizeof(e.rel), rel, _TRUNCATE);
    e.index_line = FindIndexLine(rel);
    if (!IndexField(e.index_line, 1, e.name, sizeof(e.name))) {
      const char* base = strrchr(rel, '\\');
      base = base ? base + 1 : rel;
      strncpy_s(e.name, sizeof(e.name), base, _TRUNCATE);
      char* dot = strrchr(e.name, '.');
      if (dot && dot != e.name) *dot = 0;
    }
    e.backed_up = false;
    e.element = -1;
    e.first_game = -1;
    e.kind = 0;
    e.family = 0;
    e.category = -1;
  }
  entries_[id].exists = true;
}

void FigureLibrary::ScanDir(const char* rel_dir, int depth) {
  char pattern[MAX_PATH];
  if (rel_dir[0]) {
    sprintf_s(pattern, sizeof(pattern), "%s\\Figures\\%s\\*", root_, rel_dir);
  } else {
    sprintf_s(pattern, sizeof(pattern), "%s\\Figures\\*", root_);
  }
  WIN32_FIND_DATA fd;
  HANDLE h = FindFirstFile(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) {
    if (rel_dir[0]) ++unreadable_dirs_;  // empty, or couldn't be opened
    return;
  }
  do {
    if (fd.cFileName[0] == '.') continue;  // ".", "..", hidden files
    char rel[kRelLen];
    if (rel_dir[0]) {
      sprintf_s(rel, sizeof(rel), "%s\\%s", rel_dir, fd.cFileName);
    } else {
      strncpy_s(rel, sizeof(rel), fd.cFileName, _TRUNCATE);
    }
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (depth < 5) ScanDir(rel, depth + 1);
    } else if (fd.nFileSizeLow == (DWORD)sp::kFigureSize && fd.nFileSizeHigh == 0 &&
               !HasIgnoredExtension(fd.cFileName)) {
      AddFile(rel);
    }
  } while (FindNextFile(h, &fd));
  FindClose(h);
}

void FigureLibrary::SortBrowse() {
  browse_count_ = 0;
  hidden_count_ = 0;
  older_count_ = 0;
  for (int i = 0; i < count_; ++i) {
    if (!entries_[i].exists) continue;
    if (filter_game_ > 0) {
      const int first = FirstGame(i);
      if (first > filter_game_) {
        ++hidden_count_;
        continue;
      }
    }
    browse_[browse_count_++] = i;
  }
  for (int b = 0; b < browse_count_; ++b) {  // cached for Before and SameSection
    Element(browse_[b]);
    GetCategory(browse_[b]);
  }
  picks_found_ = 0;
  if (view_game_ > 0) {
    // One game's own figures, every release.
    int kept = 0;
    for (int b = 0; b < browse_count_; ++b) {
      if (Game(browse_[b]) == view_game_) browse_[kept++] = browse_[b];
    }
    browse_count_ = kept;
  } else if (picks_mode_) {
    // Each pick's newest release among the Skylanders this game can use (a swapper's two halves
    // each): a pick is a choice, so "NewestOnly" doesn't replace it with a newer figure of another
    // name. Best first: its exact name from the game and series it names ("Trap Team Eon's Elite
    // Chop Chop", "Drobot (Series 2)"); a variant of that name from that series ("Cynder (Series
    // 3)" is Phantom Cynder); its exact name from any game or series ("Legendary Jet-Vac (Series
    // 2)" when there's only the one); then, for a name nothing has, the nearest spelling
    // ("Enchated" -> Enchanted), or else the figure the name ends with ("Legendary Crusher",
    // which was never made, -> Crusher). Everything that isn't a Skylander stays, and so do the
    // unreleased figures (below).
    enum { kSlots = 3 };  // whole figure, top half, bottom half
    static int best[kMaxPicks][kSlots];
    static int rank[kMaxPicks][kSlots];
    static bool missing[kMaxPicks];  // nothing found in the passes before this one
    for (int p = 0; p < pick_count_; ++p) {
      for (int k = 0; k < kSlots; ++k) {
        best[p][k] = -1;
        rank[p][k] = 0;
      }
    }
    for (int pass = 0; pass < 3; ++pass) {  // 0: names as written, 1: spelling, 2: name's end
      if (pass > 0) {
        for (int p = 0; p < pick_count_; ++p) {
          missing[p] = best[p][0] < 0 && best[p][1] < 0 && best[p][2] < 0;
        }
      }
      for (int b = 0; b < browse_count_; ++b) {
        const int i = browse_[b];
        const int c = entries_[i].category;
        if (c != kCatGimmick && c != kCatSkylander) continue;
        char key[kPickLen + 8];
        bool lc = false;
        PickKey(Name(i), key, sizeof(key), &lc);
        const int klen = (int)strlen(key);
        for (int p = 0; p < pick_count_; ++p) {
          const Pick& pk = picks_[p];
          if (pk.lightcore != lc && pass < 2) continue;
          const int len = (int)strlen(pk.name);
          const bool qualified = (!pk.game || Game(i) == pk.game) &&
                                 (!pk.series || SeriesNumber(i) == pk.series);
          int k = -1, r = 0;
          if (pass == 2) {
            if (!missing[p] || klen < 4 || klen >= len || strcmp(pk.name + len - klen, key)) continue;
            k = 0;
            r = klen;  // the longest name it ends with
          } else if (pass == 1) {
            if (!missing[p] || len < 6) continue;
            const int d = Distance(key, pk.name, len >= 10 ? 2 : 1);
            if (d < 0) continue;
            k = 0;
            r = (2 - d) * 2 + (qualified ? 1 : 0);  // nearer, then the game / series asked for
          } else if (!strcmp(key, pk.name)) {
            k = 0;
          } else if (!strncmp(key, pk.name, len) && !strcmp(key + len, "top")) {
            k = 1;
          } else if (!strncmp(key, pk.name, len) && !strcmp(key + len, "bottom")) {
            k = 2;
          } else if ((pk.game || pk.series) && qualified && klen > len &&
                     !strcmp(key + klen - len, pk.name)) {
            k = 0;
            r = 4;  // a variant of the name, from the game / series asked for
          }
          if (k < 0) continue;
          if (pass == 0 && !r) r = qualified ? 5 : 3;
          int& cur = best[p][k];
          if (cur >= 0) {
            if (r < rank[p][k]) continue;
            if (r == rank[p][k]) {  // same rank: the newest game's, a regular release first
              ReadRelease(i);
              ReadRelease(cur);
              const int gi = Game(i), gc = Game(cur);
              if (!(gi > gc || (gi == gc && entries_[i].kind == 'R' && entries_[cur].kind != 'R')))
                continue;
            }
          }
          cur = i;
          rank[p][k] = r;
        }
      }
    }
    static unsigned char keep[kMaxFigures];
    memset(keep, 0, sizeof(keep));
    for (int p = 0; p < pick_count_; ++p) {
      bool found = false;
      for (int k = 0; k < kSlots; ++k) {
        if (best[p][k] >= 0) {
          keep[best[p][k]] = 1;
          found = true;
        }
      }
      picks_[p].found = found;
      if (found) ++picks_found_;
    }
    // Every unreleased figure this game can use, variants and all (Heartbreaker Buckshot, the
    // Special Senseis, the Corrupted and Error Bypass dumps...), each with the regular figure it's
    // a version of: the newest regular release of the same character whose name ends its name
    // ("Instant Super Shot Stealth Elf" -> Super Shot Stealth Elf, "Special Air Strike" -> Air
    // Strike), or else the newest regular release of the character (VVind-Up -> Wind-Up).
    enum { kMaxUnreleased = 256 };
    static int un_char[kMaxUnreleased], un_best[kMaxUnreleased], un_len[kMaxUnreleased];
    static char un_name[kMaxUnreleased][kPickLen];
    int un = 0;
    for (int b = 0; b < browse_count_; ++b) {
      const int i = browse_[b];
      if (!IsUnreleased(i)) continue;
      keep[i] = 1;
      if (un == kMaxUnreleased) continue;
      un_char[un] = CharacterId(i);
      NormalizeBase(Name(i), un_name[un], kPickLen);
      un_best[un] = -1;
      un_len[un] = -1;
      if (un_char[un] > 0) ++un;
    }
    for (int b = 0; b < browse_count_ && un; ++b) {
      const int j = browse_[b];
      ReadRelease(j);
      if (entries_[j].kind != 'R' || IsUnreleased(j)) continue;
      char name[kPickLen + 8];
      name[0] = 0;
      for (int u = 0; u < un; ++u) {
        if (un_char[u] != entries_[j].family) continue;
        if (!name[0]) NormalizeBase(Name(j), name, sizeof(name));
        const int len = EndsWith(un_name[u], name) ? (int)strlen(name) : 0;
        const int cur = un_best[u];
        if (cur < 0 || len > un_len[u] || (len == un_len[u] && Game(j) > Game(cur))) {
          un_best[u] = j;
          un_len[u] = len;
        }
      }
    }
    for (int u = 0; u < un; ++u) {
      if (un_best[u] >= 0) keep[un_best[u]] = 1;
    }
    int kept = 0;
    for (int b = 0; b < browse_count_; ++b) {
      const int i = browse_[b];
      const int c = entries_[i].category;
      if ((c == kCatGimmick || c == kCatSkylander) && !keep[i]) continue;
      browse_[kept++] = i;
    }
    browse_count_ = kept;
  }
  if (newest_only_ && !picks_mode_ && view_game_ <= 0) {
    // The newest regular release of each character family among the figures listed so far (so a
    // newer figure the running game can't use, or one that isn't there, hides nothing).
    enum { kFamilies = 8192 };
    static unsigned char newest[kFamilies];
    memset(newest, 0, sizeof(newest));
    for (int b = 0; b < browse_count_; ++b) {
      const int i = browse_[b];
      ReadRelease(i);
      const Entry& e = entries_[i];
      const int g = Game(i);
      if (e.kind == 'R' && e.family < kFamilies && g > newest[e.family]) newest[e.family] = (unsigned char)g;
    }
    int kept = 0;
    for (int b = 0; b < browse_count_; ++b) {
      const int i = browse_[b];
      const Entry& e = entries_[i];
      if (e.kind != '-' && e.family < kFamilies && Game(i) < newest[e.family]) {
        ++older_count_;
        continue;
      }
      browse_[kept++] = i;
    }
    browse_count_ = kept;
  }
  // Shell sort (n <= 2560): by type, element and name; or folder, element and name.
  for (int gap = browse_count_ / 2; gap > 0; gap /= 2) {
    for (int i = gap; i < browse_count_; ++i) {
      const int v = browse_[i];
      int j = i;
      while (j >= gap && Before(v, browse_[j - gap])) {
        browse_[j] = browse_[j - gap];
        j -= gap;
      }
      browse_[j] = v;
    }
  }
}

bool FigureLibrary::Before(int a, int b) const {
  const Entry& x = entries_[a];
  const Entry& y = entries_[b];
  int c = sort_by_type_ ? x.category - y.category : CompareDir(x.rel, y.rel);
  if (c) return c < 0;
  // Elements in the games' order; figures without one (items, chests) after them.
  const int ex = x.element > 0 ? x.element : 99, ey = y.element > 0 ? y.element : 99;
  if (ex != ey) return ex < ey;
  c = CompareNoCase(Name(a), Name(b));
  if (c) return c < 0;
  if (sort_by_type_) {  // the same character's releases: oldest game first
    const int ga = Game(a), gb = Game(b);
    if (ga != gb) return ga < gb;
  }
  return CompareRel(x.rel, y.rel) < 0;
}

int FigureLibrary::Scan() {
  for (int i = 0; i < count_; ++i) entries_[i].exists = false;
  unreadable_dirs_ = 0;
  ScanDir("", 0);
  SortBrowse();
  return browse_count_;
}

bool FigureLibrary::Load(int id, sp::u8* out) const {
  if (id < 0 || id >= count_) return false;
  char path[MAX_PATH];
  FullPath(id, path, sizeof(path));
  return util::ReadExact(path, out, sp::kFigureSize);
}

bool FigureLibrary::Save(int id, const sp::u8* data) {
  if (id < 0 || id >= count_) return false;
  Entry& e = entries_[id];
  char path[MAX_PATH];
  FullPath(id, path, sizeof(path));

  if (!e.backed_up) {
    static sp::u8 original[sp::kFigureSize];
    if (util::ReadExact(path, original, sp::kFigureSize)) {
      char dir[MAX_PATH];
      sprintf_s(dir, sizeof(dir), "%s\\Backups", root_);
      CreateDirectory(dir, NULL);
      char flat[kRelLen];
      strncpy_s(flat, sizeof(flat), e.rel, _TRUNCATE);
      for (char* p = flat; *p; ++p) {
        if (*p == '\\' || *p == '/' || *p == ':') *p = '_';
      }
      char backup[MAX_PATH];
      sprintf_s(backup, sizeof(backup), "%s\\%s.bak", dir, flat);
      if (!util::FileExists(backup)) util::WriteAll(backup, original, sp::kFigureSize, true);
    }
    e.backed_up = true;
  }
  return util::WriteAll(path, data, sp::kFigureSize, false);
}
