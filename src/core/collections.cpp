// Project Kaos-Byte - nicknames and lists. See collections.h.
#include "collections.h"

#include <string.h>

namespace coll {

namespace {

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

bool SameNoCase(const char* a, const char* b) {
  for (; *a && *b; ++a, ++b) {
    if (Lower(*a) != Lower(*b)) return false;
  }
  return *a == *b;
}

bool Space(char c) { return c == ' ' || c == '\t'; }

// The next line of `text` worth reading (not empty, not a ";" or "#" comment), spaces around it
// taken off, copied into `out` (cut short at cap - 1). False at the end.
bool NextLine(const char* text, int len, int* pos, char* out, int cap) {
  while (*pos < len) {
    int s = *pos, e = s;
    while (e < len && text[e] != '\n' && text[e] != '\r') ++e;
    *pos = e;
    while (*pos < len && (text[*pos] == '\n' || text[*pos] == '\r')) ++(*pos);
    while (s < e && Space(text[s])) ++s;
    while (e > s && Space(text[e - 1])) --e;
    if (s == e || text[s] == ';' || text[s] == '#') continue;
    int n = e - s;
    if (n > cap - 1) n = cap - 1;
    memcpy(out, text + s, n);
    out[n] = 0;
    return true;
  }
  return false;
}

void Trim(char* s) {
  int n = (int)strlen(s);
  while (n > 0 && Space(s[n - 1])) s[--n] = 0;
  int k = 0;
  while (Space(s[k])) ++k;
  if (k) memmove(s, s + k, n - k + 1);
}

void Copy(char* out, int cap, const char* in) {
  int n = (int)strlen(in);
  if (n > cap - 1) n = cap - 1;
  memcpy(out, in, n);
  out[n] = 0;
}

// Text written into a bounded buffer; Over() says it ran out.
class Out {
 public:
  Out(char* buf, int cap) : buf_(buf), cap_(cap), len_(0), over_(cap < 1) {
    if (cap > 0) buf[0] = 0;
  }
  void Put(const char* s) {
    const int n = (int)strlen(s);
    if (over_ || len_ + n + 1 > cap_) {
      over_ = true;
      return;
    }
    memcpy(buf_ + len_, s, n + 1);
    len_ += n;
  }
  int Result() const { return over_ ? -1 : len_; }

 private:
  char* buf_;
  int cap_;
  int len_;
  bool over_;
};

const char kFavorites[] = "Favorites";

}  // namespace

Store::Store() { Clear(); }

void Store::Clear() {
  nick_count_ = 0;
  keep_nick_count_ = 0;
  list_count_ = 1;
  Copy(list_name_[0], sizeof(list_name_[0]), kFavorites);
  list_size_[0] = 0;
  keep_count_ = 0;
}

int Store::Clean(const char* in, char* out, int max_chars, bool list_name) {
  int n = 0;
  bool space = false;
  for (const char* p = in ? in : ""; *p && n < max_chars; ++p) {
    const unsigned char c = (unsigned char)*p;
    if (c == ' ' || c == '\t') {
      space = n > 0;  // leading spaces never count
      continue;
    }
    if (c < 33 || c > 126) continue;
    if (list_name && (c == '[' || c == ']')) continue;
    if (space) {
      if (n + 1 >= max_chars) break;  // no room for the space and a character after it
      out[n++] = ' ';
      space = false;
    }
    out[n++] = (char)c;
  }
  out[n] = 0;
  return n;
}

// ---- nicknames ----

int Store::FindNick(int id) const {
  for (int i = 0; i < nick_count_; ++i) {
    if (nick_id_[i] == id) return i;
  }
  return -1;
}

const char* Store::Nick(int id) const {
  const int i = id >= 0 ? FindNick(id) : -1;
  return i >= 0 ? nick_[i] : "";
}

bool Store::SetNick(int id, const char* text) {
  if (id < 0 || id > 32767) return false;
  char clean[kNickMax + 1];
  Clean(text, clean, kNickMax, false);
  const int i = FindNick(id);
  if (!clean[0]) {  // taken away
    if (i < 0) return false;
    memmove(&nick_id_[i], &nick_id_[i + 1], (nick_count_ - i - 1) * sizeof(nick_id_[0]));
    memmove(nick_[i], nick_[i + 1], (nick_count_ - i - 1) * sizeof(nick_[0]));
    --nick_count_;
    return true;
  }
  if (i >= 0) {
    if (!strcmp(nick_[i], clean)) return false;
    Copy(nick_[i], sizeof(nick_[i]), clean);
    return true;
  }
  if (nick_count_ >= kMaxNicks) return false;
  nick_id_[nick_count_] = (short)id;
  Copy(nick_[nick_count_], sizeof(nick_[0]), clean);
  ++nick_count_;
  return true;
}

void Store::LoadNicknames(const char* text, int len, FindFn find) {
  nick_count_ = 0;
  keep_nick_count_ = 0;
  if (!text) return;
  int pos = 0;
  char line[kRelMax + kNickMax + 64];
  while (NextLine(text, len, &pos, line, sizeof(line))) {
    if (line[0] == '[') continue;  // [Nicknames]
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    char rel[kRelMax + 1];
    Copy(rel, sizeof(rel), line);
    Trim(rel);
    char nick[kNickMax + 1];
    Clean(eq + 1, nick, kNickMax, false);
    if (!rel[0] || !nick[0]) continue;
    const int id = find ? find(rel) : -1;
    if (id >= 0) {
      SetNick(id, nick);
    } else if (keep_nick_count_ < kKeepNicks) {
      Copy(keep_nick_rel_[keep_nick_count_], sizeof(keep_nick_rel_[0]), rel);
      Copy(keep_nick_[keep_nick_count_], sizeof(keep_nick_[0]), nick);
      ++keep_nick_count_;
    }
  }
}

int Store::SaveNicknames(char* out, int cap, RelFn rel) const {
  Out o(out, cap);
  o.Put("; Nicknames - The Static Rift. Give one in the menu on the TV (START on a figure) or on\r\n"
        "; the phone remote. One figure per line: its file (as in Figures) = its nickname.\r\n"
        "[Nicknames]\r\n");
  for (int i = 0; i < nick_count_; ++i) {
    const char* r = rel ? rel(nick_id_[i]) : "";
    if (!r || !r[0]) continue;
    o.Put(r);
    o.Put(" = ");
    o.Put(nick_[i]);
    o.Put("\r\n");
  }
  for (int k = 0; k < keep_nick_count_; ++k) {
    o.Put(keep_nick_rel_[k]);
    o.Put(" = ");
    o.Put(keep_nick_[k]);
    o.Put("\r\n");
  }
  return o.Result();
}

// ---- lists ----

bool Store::InList(int l, int id) const {
  if (l < 0 || l >= list_count_) return false;
  for (int k = 0; k < list_size_[l]; ++k) {
    if (list_ids_[l][k] == id) return true;
  }
  return false;
}

int Store::Add(int l, int id) {
  if (l < 0 || l >= list_count_ || id < 0 || id > 32767) return -1;
  if (InList(l, id)) return 0;
  if (list_size_[l] >= kMaxEntries) return -1;
  list_ids_[l][list_size_[l]++] = (short)id;
  return 1;
}

bool Store::Remove(int l, int id) {
  if (l < 0 || l >= list_count_) return false;
  for (int k = 0; k < list_size_[l]; ++k) {
    if (list_ids_[l][k] != id) continue;
    memmove(&list_ids_[l][k], &list_ids_[l][k + 1], (list_size_[l] - k - 1) * sizeof(short));
    --list_size_[l];
    return true;
  }
  return false;
}

int Store::FindList(const char* name) const {
  for (int l = 0; l < list_count_; ++l) {
    if (SameNoCase(list_name_[l], name)) return l;
  }
  return -1;
}

int Store::NewList(const char* name) {
  char clean[kListNameMax + 1];
  if (!Clean(name, clean, kListNameMax, true)) return kNoName;
  if (FindList(clean) >= 0) return kNameTaken;
  if (list_count_ >= kMaxLists) return kFull;
  const int l = list_count_++;
  Copy(list_name_[l], sizeof(list_name_[l]), clean);
  list_size_[l] = 0;
  return l;
}

int Store::Rename(int l, const char* name) {
  if (l <= 0 || l >= list_count_) return kNoList;
  char clean[kListNameMax + 1];
  if (!Clean(name, clean, kListNameMax, true)) return kNoName;
  const int other = FindList(clean);
  if (other >= 0 && other != l) return kNameTaken;
  Copy(list_name_[l], sizeof(list_name_[l]), clean);
  return 0;
}

bool Store::Delete(int l) {
  if (l <= 0 || l >= list_count_) return false;
  for (int m = l; m + 1 < list_count_; ++m) {
    memcpy(list_name_[m], list_name_[m + 1], sizeof(list_name_[m]));
    memcpy(list_ids_[m], list_ids_[m + 1], sizeof(list_ids_[m]));
    list_size_[m] = list_size_[m + 1];
  }
  --list_count_;
  int kept = 0;  // its figures that aren't there go with it; the later lists' move up
  for (int k = 0; k < keep_count_; ++k) {
    if (keep_list_[k] == l) continue;
    keep_list_[kept] = (signed char)(keep_list_[k] > l ? keep_list_[k] - 1 : keep_list_[k]);
    if (kept != k) memcpy(keep_rel_[kept], keep_rel_[k], sizeof(keep_rel_[k]));
    ++kept;
  }
  keep_count_ = kept;
  return true;
}

void Store::LoadLists(const char* text, int len, FindFn find) {
  list_count_ = 1;
  Copy(list_name_[0], sizeof(list_name_[0]), kFavorites);
  list_size_[0] = 0;
  keep_count_ = 0;
  if (!text) return;
  int pos = 0, cur = 0;
  char line[kRelMax + 64];
  while (NextLine(text, len, &pos, line, sizeof(line))) {
    if (line[0] == '[') {  // [List name]
      char* close = strchr(line, ']');
      if (close) *close = 0;
      char name[kListNameMax + 1];
      Clean(line + 1, name, kListNameMax, true);
      if (!name[0]) {
        cur = -1;  // a nameless section: its lines are skipped
        continue;
      }
      cur = FindList(name);
      if (cur < 0) cur = NewList(name);  // -1 when there are too many lists
      continue;
    }
    if (cur < 0) continue;
    const int id = find ? find(line) : -1;
    if (id >= 0) {
      Add(cur, id);
    } else if (keep_count_ < kKeepEntries) {
      keep_list_[keep_count_] = (signed char)cur;
      Copy(keep_rel_[keep_count_], sizeof(keep_rel_[0]), line);
      ++keep_count_;
    }
  }
}

int Store::SaveLists(char* out, int cap, RelFn rel) const {
  Out o(out, cap);
  o.Put("; Your lists - The Static Rift. Made on the phone remote: Favorites, and lists of your\r\n"
        "; own. The menu on the TV shows them too (R3). One section per list, one figure file (as\r\n"
        "; in Figures) per line, in the list's order.\r\n");
  for (int l = 0; l < list_count_; ++l) {
    o.Put("\r\n[");
    o.Put(list_name_[l]);
    o.Put("]\r\n");
    for (int k = 0; k < list_size_[l]; ++k) {
      const char* r = rel ? rel(list_ids_[l][k]) : "";
      if (!r || !r[0]) continue;
      o.Put(r);
      o.Put("\r\n");
    }
    for (int k = 0; k < keep_count_; ++k) {
      if (keep_list_[k] != l) continue;
      o.Put(keep_rel_[k]);
      o.Put("\r\n");
    }
  }
  return o.Result();
}

}  // namespace coll
