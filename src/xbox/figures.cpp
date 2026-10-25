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
      index_hash_(NULL), index_off_(NULL), index_lines_(0) {
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

int FigureLibrary::GroupJump(int pos, int dir) const {
  const int n = browse_count_;
  if (n <= 1) return pos;
  // A figure's group is its folder (path up to the last backslash).
  struct Dir {
    static int Len(const char* rel) {
      const char* s = strrchr(rel, '\\');
      return s ? (int)(s - rel) : 0;
    }
    static bool Same(const char* a, const char* b) {
      const int la = Len(a), lb = Len(b);
      return la == lb && !_strnicmp(a, b, la);
    }
  };
  const char* here = entries_[browse_[pos]].rel;
  int p = pos;
  if (dir > 0) {
    for (int k = 0; k < n; ++k) {
      p = (p + 1) % n;
      if (!Dir::Same(entries_[browse_[p]].rel, here)) return p;
    }
    return pos;
  }
  // Back: to the start of this group if not already there, else the start of the previous one.
  int start = pos;
  while (start > 0 && Dir::Same(entries_[browse_[start - 1]].rel, here)) --start;
  if (start != pos) return start;
  p = (pos - 1 + n) % n;
  const char* prev = entries_[browse_[p]].rel;
  while (p > 0 && Dir::Same(entries_[browse_[p - 1]].rel, prev)) --p;
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
  if (newest_only_) {
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
  // Shell sort by relative path, folder by folder (n <= 2560).
  for (int gap = browse_count_ / 2; gap > 0; gap /= 2) {
    for (int i = gap; i < browse_count_; ++i) {
      const int v = browse_[i];
      int j = i;
      while (j >= gap && CompareRel(entries_[browse_[j - gap]].rel, entries_[v].rel) > 0) {
        browse_[j] = browse_[j - gap];
        j -= gap;
      }
      browse_[j] = v;
    }
  }
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
