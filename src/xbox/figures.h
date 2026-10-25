// SkyPortal360 - the figure library: raw 1 KB figure dumps in SkyPortal\Figures (any depth up to
// five folders), loaded on demand and saved back when the game writes to a figure.
//
// Entry ids are stable for the life of the plugin (rescans only append), so they can be used as
// the portal's slot tags and a save always goes back to the file the figure came from. Browsing
// follows folder order (game, then category), so figures from one game and category stay
// together.
//
// SkyPortal\UI\figures.idx (made on the PC by tools/mkassets.py) describes the dumps of the
// Skylanders NFC pack layout: display name, game, element, series and image file. Dumps it
// doesn't know still work; they show their file name and an element worked out from the dump.
#pragma once

#include "stdafx.h"

class FigureLibrary {
 public:
  enum { kMaxFigures = 2560, kRelLen = 160, kNameLen = 72 };

  FigureLibrary();

  // root: e.g. "SKYHDD:\\SkyPortal". Figures live in <root>\Figures, backups in <root>\Backups.
  void SetRoot(const char* root);
  const char* Root() const { return root_; }
  bool LoadIndex();  // <root>\UI\figures.idx; false if missing
  int Scan();        // (re)reads the folder; returns how many figures can be browsed
  // Only figures that work in `game` (1-6, the running Skylanders game) are browsed; 0 = all.
  // Takes effect at the next Scan().
  void SetGameFilter(int game) { filter_game_ = game; }
  int HiddenCount() const { return hidden_count_; }  // left out by the filter at the last Scan()
  // Only a character's newest release (and that release's variants) is browsed: Tidal Wave Gill
  // Grunt (Series 4) replaces Series 1-3, their chase variants and so on. Traps, crystals,
  // vehicles and items are never left out. Takes effect at the next Scan().
  void SetNewestOnly(bool on) { newest_only_ = on; }
  int OlderCount() const { return older_count_; }  // older releases left out at the last Scan()
  int UnreadableDirs() const { return unreadable_dirs_; }  // empty or unopenable folders at the last Scan()

  // Browsing order (folder order, only files that exist and are 1024 bytes).
  int BrowseCount() const { return browse_count_; }
  int BrowseAt(int pos) const;      // entry id, or -1
  int BrowsePos(int id) const;      // position of an entry id, or -1
  // Browse position of the first figure in the next (+1) / previous (-1) folder.
  int GroupJump(int pos, int dir) const;

  const char* Name(int id) const;  // display name (index), else the file name without extension
  const char* Rel(int id) const;   // path relative to Figures, e.g. "2. Giants\\1) Giants\\Tree Rex.dump"
  int FindByRel(const char* rel) const;    // case-insensitive, -1 if unknown
  int FindByName(const char* name) const;  // case-insensitive display/file-name match, -1 if none

  // The figure's element (elements::Element): index, folder name, or dump contents; cached.
  int Element(int id);
  int Game(int id) const;                                // 1-6 (index or top folder), 0 = unknown
  void Series(int id, char* out, int cap) const;         // "Series 2", "Legendary", ... or ""
  // The first game the figure works in (1-6), from figures.idx (by figure, or by the character
  // id in the dump), else the top folder; 0 = unknown (always shown).
  int FirstGame(int id);
  bool ImagePath(int id, char* out, int cap) const;      // <root>\Images\..., false if none

  bool Load(int id, sp::u8* out /* sp::kFigureSize */) const;
  // Saves the game's changes. The first save of a figure keeps a backup of the file as it was
  // ("<root>\Backups\..."), never overwriting an older backup.
  bool Save(int id, const sp::u8* data /* sp::kFigureSize */);

 private:
  struct Entry {
    char rel[kRelLen];
    char name[kNameLen];
    int index_line;       // offset of its figures.idx line, -1 if none
    bool exists;
    bool backed_up;
    signed char element;  // -1 = not read yet
    signed char first_game;  // -1 = not worked out yet
    signed char kind;        // figures.idx release kind: 'R' regular, 'V' variant, '-' other; 0 = not read
    int family;              // figures.idx character family (0 = none)
  };
  void ReadRelease(int id);
  void ScanDir(const char* rel_dir, int depth);
  void AddFile(const char* rel);
  void FullPath(int id, char* out, size_t cap) const;
  void SortBrowse();
  int FindIndexLine(const char* rel) const;
  int FindKeyLine(const char* key) const;  // the line whose key field is exactly `key`, or -1
  // Copies tab-separated field `field` of the index line at `off` into `out`.
  bool IndexField(int off, int field, char* out, int cap) const;

  char root_[MAX_PATH];
  Entry* entries_;
  int count_;
  int* browse_;
  int browse_count_;
  int filter_game_;
  int hidden_count_;
  bool newest_only_;
  int older_count_;
  int unreadable_dirs_;

  // figures.idx: the whole file, plus (hash, line offset) pairs sorted by hash.
  char* index_;
  int index_size_;
  unsigned int* index_hash_;
  int* index_off_;
  int index_lines_;
};
