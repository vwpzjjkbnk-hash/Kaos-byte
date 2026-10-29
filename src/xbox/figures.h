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

  // What a figure is, for the menu's order by type and for "Swap in".
  enum Category {
    kCatGimmick,    // Giants, Swappers, Trap Masters, SuperChargers, Senseis (and their variants)
    kCatCrystal,    // Creation Crystals and premade creations
    kCatSkylander,  // every other Skylander (series, Legendary, Eon's Elite, variants, minis, ...)
    kCatVehicle,
    kCatTrap,       // traps and trapped villains
    kCatAdventure,  // adventure packs, expansion packs, the battle arena
    kCatItem,       // magic items, treasure chests, promo items
    kCatCount
  };
  static const char* CategoryName(int category);  // "Gimmick Skylanders", ...
  int GetCategory(int id);  // cached
  // true (the default): browse by type in the order above, then element, then name, wherever the
  // figure's file is; false: folder by folder as the files are laid out. Takes effect at Resort().
  void SetSortByType(bool on) { sort_by_type_ = on; }
  bool SortByType() const { return sort_by_type_; }
  // "My Picks": the menu's second list (R3). `names` are figure names as the menu shows them
  // (a swapper's name without Top/Bottom picks both halves), optionally with a game in front
  // ("Trap Team Eon's Elite Chop Chop") or a series ("Drobot (Series 2)") to pick that release;
  // the list shows each pick's newest such release the game can use, plus every crystal,
  // vehicle, trap, adventure pack and magic item, and every unreleased figure with the regular
  // figure it's a version of.
  void SetPicks(const char* const* names, int count);
  int PickCount() const { return pick_count_; }
  void SetPicksMode(bool on) { picks_mode_ = on && pick_count_ > 0; }
  bool PicksMode() const { return picks_mode_; }
  int PicksFound() const { return picks_found_; }  // picks matched at the last sort (picks mode)
  const char* PickText(int p) const { return p >= 0 && p < pick_count_ ? picks_[p].text : ""; }
  bool PickFound(int p) const { return p >= 0 && p < pick_count_ && picks_[p].found; }
  // Another list R3 can show: only the figures from one game (its own releases, by the game
  // folder they're in: "Imaginators" shows Imaginators' Senseis, crystals, chests...), every
  // release of them (NewestOnly doesn't apply). 1-6; 0 = off. Takes effect at Resort().
  void SetViewGame(int game) { view_game_ = game; }
  int ViewGame() const { return view_game_; }
  // How many figures the game filter lets through from game `game`'s own releases.
  int CountFromGame(int game);
  void Resort() { SortBrowse(); }  // after SetSortByType / SetPicksMode / SetViewGame
  // The menu's section heading for a figure in the type order ("Skylanders / Magic"); false
  // when browsing by folder.
  bool SectionLabel(int id, char* out, int cap);
  int UnreadableDirs() const { return unreadable_dirs_; }  // empty or unopenable folders at the last Scan()

  // Browsing order: folder by folder; within a folder by element (Air, Earth, Fire, Water, Magic,
  // Tech, Life, Undead, Light, Dark, then none), then by name. Only files that exist and are
  // 1024 bytes.
  int BrowseCount() const { return browse_count_; }
  int BrowseAt(int pos) const;      // entry id, or -1
  int BrowsePos(int id) const;      // position of an entry id, or -1
  // Browse position of the first figure in the next (+1) / previous (-1) section: a folder, or
  // one element's figures within a folder.
  int GroupJump(int pos, int dir) const;

  const char* Name(int id) const;  // display name (index), else the file name without extension
  const char* Rel(int id) const;   // path relative to Figures, e.g. "2. Giants\\1) Giants\\Tree Rex.dump"
  int FindByRel(const char* rel) const;    // case-insensitive, -1 if unknown
  int FindByName(const char* name) const;  // case-insensitive display/file-name match, -1 if none

  // The figure's element (elements::Element): index, folder name, or dump contents; cached.
  int Element(int id);
  int Game(int id) const;                                // 1-6 (index or top folder), 0 = unknown
  void Series(int id, char* out, int cap) const;         // "Series 2", "Legendary", ... or ""
  int CharacterId(int id) const;                         // figures.idx character (toy) id, 0 = none
  bool IsUnreleased(int id) const;                       // in an "Unreleased" folder or so named
  int SeriesNumber(int id) const;                        // 2 for "Series 2", else 0
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
    signed char category;    // Category, -1 = not worked out yet
  };
  void ReadRelease(int id);
  bool Before(int a, int b) const;        // browse order (SortBrowse)
  bool SameSection(int a, int b) const;   // same folder and element (GroupJump)
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

  bool sort_by_type_;
  enum { kMaxPicks = 64, kPickLen = 48 };
  struct Pick {
    char text[kPickLen];  // as written in picks.ini (for the log)
    char name[kPickLen];  // normalized, without "LightCore", the game or the series
    signed char game;     // "Trap Team Eon's Elite Chop Chop": that game's release; 0 = any
    signed char series;   // "Drobot (Series 2)": that series; 0 = any
    bool lightcore;       // "LightCore Smolderdash" = "Smolderdash LightCore"
    bool found;           // at the last sort
  };
  Pick picks_[kMaxPicks];
  int pick_count_;
  int view_game_;
  bool picks_mode_;
  int picks_found_;
};
