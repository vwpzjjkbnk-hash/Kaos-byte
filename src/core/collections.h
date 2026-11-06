// Project Kaos-Byte - nicknames and lists (portable: no Xbox headers, C++03).
//
// Nicknames are names you give your figures, shown under the figure's real name in the menu on
// the TV and on the phone remote. Lists are Favorites plus lists of your own, made on the phone
// and shown on both. Neither touches the figure files: they live in two small text files next to
// staticrift.ini, written by the plugin, and name each figure by its file (the path relative to
// Figures, which never changes for a figure):
//
//   nicknames.ini                          lists.ini
//   [Nicknames]                            [Favorites]
//   Skylanders\...\Chop Chop.dump = Choppy Skylanders\...\Chop Chop.dump
//                                          [Speedrun]
//                                          Skylanders\...\Spyro.dump
//
// A line naming a figure that isn't in the Figures folder right now is kept as it is and written
// back, so taking a figure out for a while doesn't lose its nickname or its place in a list.
// Everything here is the pure part (reading, writing, editing); the plugin does the file I/O.
#pragma once

namespace coll {

enum {
  kMaxLists = 13,     // Favorites (list 0) and up to 12 lists of your own
  kMaxEntries = 160,  // figures per list
  kListNameMax = 24,  // characters in a list's name
  kMaxNicks = 400,
  kNickMax = 20,      // characters in a nickname
  kRelMax = 159,      // characters in a figure's file path
  kKeepNicks = 16,    // nicknames of figures that aren't there right now
  kKeepEntries = 32   // list lines naming figures that aren't there right now
};

// The figure library: a figure's id from its file (-1 when there's no such figure), and back
// ("" for an unknown id).
typedef int (*FindFn)(const char* rel);
typedef const char* (*RelFn)(int id);

class Store {
 public:
  Store();
  void Clear();  // no nicknames, just an empty Favorites

  // ---- nicknames ----
  void LoadNicknames(const char* text, int len, FindFn find);
  // The file's text, NUL-terminated; its length, or -1 when it doesn't fit in `cap`.
  int SaveNicknames(char* out, int cap, RelFn rel) const;
  const char* Nick(int id) const;  // "" when it has none
  // Gives figure `id` a nickname (cleaned up with Clean; "" takes it away). True if it changed.
  bool SetNick(int id, const char* text);
  int NickCount() const { return nick_count_; }
  int NickId(int i) const { return i >= 0 && i < nick_count_ ? nick_id_[i] : -1; }
  const char* NickText(int i) const { return i >= 0 && i < nick_count_ ? nick_[i] : ""; }

  // ---- lists (0 = Favorites) ----
  void LoadLists(const char* text, int len, FindFn find);
  int SaveLists(char* out, int cap, RelFn rel) const;
  int ListCount() const { return list_count_; }
  const char* ListName(int l) const { return l >= 0 && l < list_count_ ? list_name_[l] : ""; }
  int ListSize(int l) const { return l >= 0 && l < list_count_ ? list_size_[l] : 0; }
  const short* ListIds(int l) const { return l >= 0 && l < list_count_ ? list_ids_[l] : 0; }
  bool InList(int l, int id) const;
  int Add(int l, int id);       // 1 added (at the end), 0 already in it, -1 full or no such list
  bool Remove(int l, int id);   // true if it was in it
  // A new, empty list at the end: its number; kFull, kNoName or kNameTaken.
  enum { kFull = -1, kNoName = -2, kNameTaken = -3, kNoList = -4 };
  int NewList(const char* name);
  int Rename(int l, const char* name);  // 0, or kNoName, kNameTaken, kNoList (Favorites can't be)
  bool Delete(int l);                   // the lists after it move up one; Favorites can't be
  int FindList(const char* name) const; // case doesn't matter; -1 when there's none

  // Cleans up text typed for a nickname or a list name: only printable ASCII (what the menu's
  // font has), no runs of spaces, nothing around it, at most `max_chars` characters; a list's
  // name also loses '[' and ']'. Returns the length of `out` (which holds max_chars + 1).
  static int Clean(const char* in, char* out, int max_chars, bool list_name);

 private:
  int FindNick(int id) const;

  short nick_id_[kMaxNicks];
  char nick_[kMaxNicks][kNickMax + 1];
  int nick_count_;
  char keep_nick_rel_[kKeepNicks][kRelMax + 1];
  char keep_nick_[kKeepNicks][kNickMax + 1];
  int keep_nick_count_;

  char list_name_[kMaxLists][kListNameMax + 1];
  short list_ids_[kMaxLists][kMaxEntries];
  int list_size_[kMaxLists];
  int list_count_;
  signed char keep_list_[kKeepEntries];
  char keep_rel_[kKeepEntries][kRelMax + 1];
  int keep_count_;
};

}  // namespace coll
