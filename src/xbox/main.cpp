// SkyPortal360 - virtual Portal of Power for the Xbox 360 Skylanders games.
//
// When a Skylanders title runs, SkyPortal patches that title's imports of XAM's raw
// non-controller API (how the games talk to the USB portal) to a software portal, and lets you
// put figure dumps from SkyPortal\Figures on that portal with a controller combo.
//
// Two builds share this file:
//
//   Dashlaunch plugin (default; RGH/JTAG, BadUpdate + XeUnshackle)
//     A system DLL loaded at boot. DllMain starts a system thread that watches for a Skylanders
//     title, patches its imports (idempotent, every 250 ms), runs the figure menu and saves.
//
//   Peer Pressure sideload (SKYPORTAL_SIDELOAD defined)
//     A title DLL Peer Pressure loads into the game after the game xex is in memory and before
//     its entry point runs. DllMain patches the game's imports right away (only the game's own
//     import table: nothing in the kernel or XAM is touched, as Peer Pressure requires), then a
//     title thread runs the figure menu and saves. The DLL unloads with the game.
#include "stdafx.h"

#include <stdarg.h>
#include <stdlib.h>

#include "config.h"
#include "elements.h"
#include "figures.h"
#include "hooks.h"
#include "menu_api.h"
#include "remote.h"
#include "util.h"
#include "../core/collections.h"
#include "../core/remote_http.h"
#include "keyboard.h"
#if defined(SKYPORTAL_MENU_POP)
#include "menu_pop.h"
#endif
#if defined(SKYPORTAL_MENU_DRAWN)
#include "menu_pop.h"  // the fallback when there's no memory for the drawn menu
#include "overlay.h"
#endif

namespace {

// --------------------------------------------------------------------------------------------
// State
// --------------------------------------------------------------------------------------------

sp::VirtualPortal g_portal;
sp::XamBridge g_bridge(&g_portal);
FigureLibrary g_library;
Config g_config;
// Nicknames and lists (Favorites, and lists made on the phone): nicknames.ini, lists.ini.
coll::Store g_coll;
#if defined(SKYPORTAL_MENU_DRAWN) || defined(SKYPORTAL_SIDELOAD)
LONG g_meta_rev = 1;  // goes up whenever a nickname or a list changes (the phones refetch them)
#endif

volatile bool g_stop = false;
char g_root[MAX_PATH] = {0};
int g_game = 0;  // the running game: 1 Spyro's Adventure ... 6 Imaginators, 0 unknown

struct Settings {
  bool enabled;
  bool notifications;
  bool save_writes;
  bool remember;
  bool speaker;
  bool hide_combo;
  bool patch_all_titles;
  WORD modifier;
  bool need_lt;
  bool need_rt;
  bool overlay_swap_rb;       // skydrawn: [Menu] OverlaySwapRedBlue = 1 if colours come out wrong
  bool only_compatible;       // [Menu] OnlyCompatible: list only figures this game can use
  bool newest_only;           // [Menu] NewestOnly: list only each character's newest release
  bool pause_low_memory;      // [Menu] PauseGameWhenLowOnMemory
  bool pause_game;            // [Menu] PauseGame: stop the game while the menu is open
  bool full_screen;           // [Menu] FullScreenMenu: 0 = always the console's pop-up menu
  bool sort_by_type;          // [Menu] SortByType: 0 = folder by folder, as the files are laid out
  bool remote;                // [Remote] Enabled: the phone remote (a web page the Xbox serves)
  int remote_port;            // [Remote] Port (80; 8080 is tried when it's taken)
  int swap_gap_ms;            // [General] SwapGapMs: Swap in's pause between off and on
};
Settings g_set;

#if defined(SKYPORTAL_MENU_POP)
const char* const kMenuStyle = "pop-up menu (skypop)";
#elif defined(SKYPORTAL_MENU_DRAWN)
const char* const kMenuStyle = "drawn menu (skydrawn)";
#else
const char* const kMenuStyle = "notifications";
#endif

// Title IDs of the Xbox 360 Skylanders games (consolemods.org title ID list).
const DWORD kSkylandersTitles[] = {
    0x415608AB,  // Skylanders: Spyro's Adventure
    0x415608DA,  // Skylanders: Giants
    0x415608E9,  // Skylanders: Swap Force
    0x41560911,  // Skylanders: Trap Team
    0x4156091F,  // Skylanders: SuperChargers
    0x41560930,  // Skylanders: Imaginators
};
enum { kMaxExtraTitles = 16 };
DWORD g_extra_titles[kMaxExtraTitles];
int g_extra_title_count = 0;

// Buttons the portal menu uses (hidden from the game while the modifier is held).
const WORD kMenuButtons = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
                          XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT |
                          XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X |
                          XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_START;

// --------------------------------------------------------------------------------------------
// Settings
// --------------------------------------------------------------------------------------------

struct ButtonName {
  const char* name;
  WORD mask;
};
const ButtonName kButtonNames[] = {
    {"LB", XINPUT_GAMEPAD_LEFT_SHOULDER}, {"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER},
    {"LS", XINPUT_GAMEPAD_LEFT_THUMB},    {"RS", XINPUT_GAMEPAD_RIGHT_THUMB},
    {"BACK", XINPUT_GAMEPAD_BACK},        {"START", XINPUT_GAMEPAD_START},
    {"A", XINPUT_GAMEPAD_A},              {"B", XINPUT_GAMEPAD_B},
    {"X", XINPUT_GAMEPAD_X},              {"Y", XINPUT_GAMEPAD_Y},
    {"UP", XINPUT_GAMEPAD_DPAD_UP},       {"DOWN", XINPUT_GAMEPAD_DPAD_DOWN},
    {"LEFT", XINPUT_GAMEPAD_DPAD_LEFT},   {"RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
};

// "LB+RB", "BACK+LT", ... -> button mask + trigger flags.
void ParseModifier(const char* text, WORD* mask, bool* lt, bool* rt) {
  *mask = 0;
  *lt = false;
  *rt = false;
  char buf[64];
  strncpy_s(buf, sizeof(buf), text, _TRUNCATE);
  char* ctx = NULL;
  for (char* tok = strtok_s(buf, "+ ,", &ctx); tok; tok = strtok_s(NULL, "+ ,", &ctx)) {
    if (!_stricmp(tok, "LT")) {
      *lt = true;
      continue;
    }
    if (!_stricmp(tok, "RT")) {
      *rt = true;
      continue;
    }
    for (size_t i = 0; i < sizeof(kButtonNames) / sizeof(kButtonNames[0]); ++i) {
      if (!_stricmp(tok, kButtonNames[i].name)) *mask |= kButtonNames[i].mask;
    }
  }
  if (!*mask && !*lt && !*rt) *mask = XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER;
}

void LoadSettings() {
  char path[MAX_PATH];
  // staticrift.ini (skyportal.ini, its old name, still works).
  sprintf_s(path, sizeof(path), "%s\\staticrift.ini", g_root);
  bool loaded = g_config.Load(path);
  if (!loaded) {
    sprintf_s(path, sizeof(path), "%s\\skyportal.ini", g_root);
    loaded = g_config.Load(path);
  }
  if (!loaded) util::Log("No staticrift.ini, using defaults");

  g_set.enabled = g_config.GetBool("General", "Enabled", true);
  g_set.notifications = g_config.GetBool("General", "Notifications", true);
  g_set.save_writes = g_config.GetBool("General", "SaveWrites", true);
  g_set.remember = g_config.GetBool("General", "RememberPortal", true);
  g_set.speaker = g_config.GetBool("General", "Speaker", true);
  g_set.hide_combo = g_config.GetBool("Controls", "HideComboFromGame", true);
  g_set.patch_all_titles = g_config.GetBool("Titles", "PatchAllTitles", false);
  ParseModifier(g_config.Get("Controls", "Modifier", "LB+RB"), &g_set.modifier, &g_set.need_lt,
                &g_set.need_rt);
  g_set.overlay_swap_rb = g_config.GetBool("Menu", "OverlaySwapRedBlue", false);
  g_set.only_compatible = g_config.GetBool("Menu", "OnlyCompatible", true);
  g_set.newest_only = g_config.GetBool("Menu", "NewestOnly", false);
  g_set.pause_low_memory = g_config.GetBool("Menu", "PauseGameWhenLowOnMemory", true);
  g_set.pause_game = g_config.GetBool("Menu", "PauseGame", true);
  g_set.full_screen = g_config.GetBool("Menu", "FullScreenMenu", true);
  g_set.sort_by_type = g_config.GetBool("Menu", "SortByType", true);
  g_set.remote = g_config.GetBool("Remote", "Enabled", true);
  g_set.remote_port = g_config.GetInt("Remote", "Port", 80);
  g_set.swap_gap_ms = g_config.GetInt("General", "SwapGapMs", 150);
  if (g_set.swap_gap_ms < 0) g_set.swap_gap_ms = 0;
  if (g_set.swap_gap_ms > 5000) g_set.swap_gap_ms = 5000;

  g_extra_title_count = 0;
  const char* key;
  const char* value;
  for (int i = 0; g_config.Value("Titles", i, &key, &value); ++i) {
    if (!_stricmp(key, "PatchAllTitles")) continue;
    const DWORD id = strtoul(value, NULL, 16);
    if (id && g_extra_title_count < kMaxExtraTitles) g_extra_titles[g_extra_title_count++] = id;
  }

  g_portal.SetSpeaker(g_set.speaker);
  hooks::SetComboFilter(g_set.hide_combo ? g_set.modifier : 0, g_set.need_lt, g_set.need_rt,
                        (WORD)(kMenuButtons & ~g_set.modifier));
  util::Log("Settings: enabled=%d notify=%d save=%d remember=%d speaker=%d hide=%d mod=%04X lt=%d "
            "rt=%d extraTitles=%d all=%d swapGap=%dms",
            g_set.enabled, g_set.notifications, g_set.save_writes, g_set.remember, g_set.speaker,
            g_set.hide_combo, g_set.modifier, g_set.need_lt, g_set.need_rt, g_extra_title_count,
            g_set.patch_all_titles, g_set.swap_gap_ms);
}

bool TitleAllowed(DWORD id) {
  if (!id || id == 0xFFFE07D1) return false;  // nothing / the dashboard
  if (g_set.patch_all_titles) return true;
  for (size_t i = 0; i < sizeof(kSkylandersTitles) / sizeof(kSkylandersTitles[0]); ++i) {
    if (kSkylandersTitles[i] == id) return true;
  }
  for (int i = 0; i < g_extra_title_count; ++i) {
    if (g_extra_titles[i] == id) return true;
  }
  return false;
}

// --------------------------------------------------------------------------------------------
// Portal state <-> files
// --------------------------------------------------------------------------------------------

void Toast(const char* fmt, ...) {
  char text[200];
  va_list ap;
  va_start(ap, fmt);
  _vsnprintf_s(text, sizeof(text), _TRUNCATE, fmt, ap);
  va_end(ap);
  util::Log("Toast: %s", text);
  if (g_set.notifications) util::NotifyA("%s", text);
}

// The rift changed (every change comes through here): the phones see it at once, and
// rift_state.txt is written a second later, when it has settled, so writing it never holds up a
// figure going on or coming off.
DWORD g_state_due = 0;  // when rift_state.txt is to be written; 0 = it's up to date

void SavePortalState() {
  remote::Changed();
  if (g_set.remember) g_state_due = (GetTickCount() + 1000) | 1;
}

void WritePortalState() {
  g_state_due = 0;
  if (!g_set.remember) return;
  static char buf[sp::kMaxSlots * (FigureLibrary::kRelLen + 2) + 64];
  int len = sprintf_s(buf, sizeof(buf), "; figures on the rift (The Static Rift)\r\n");
  for (int slot = 0; slot < sp::kMaxSlots; ++slot) {
    const int id = g_portal.Tag(slot);
    if (id < 0) continue;
    len += sprintf_s(buf + len, sizeof(buf) - len, "%s\r\n", g_library.Rel(id));
  }
  char path[MAX_PATH];
  sprintf_s(path, sizeof(path), "%s\\rift_state.txt", g_root);
  util::WriteAll(path, buf, (DWORD)len, false);
}

void ServiceStateFile() {
  if (g_state_due && (int)(GetTickCount() - g_state_due) >= 0) WritePortalState();
}

// --------------------------------------------------------------------------------------------
// Nicknames and lists <-> files (nicknames.ini, lists.ini next to staticrift.ini)
// --------------------------------------------------------------------------------------------

int CollFind(const char* rel) { return g_library.FindByRel(rel); }

char g_coll_buf[64 * 1024];  // both files are read and written through this (worker thread only)

void CollPath(const char* file, char* out, int cap) { sprintf_s(out, cap, "%s\\%s", g_root, file); }

void LoadCollections() {
  char path[MAX_PATH];
  CollPath("nicknames.ini", path, sizeof(path));
  int n = util::ReadAll(path, g_coll_buf, (int)sizeof(g_coll_buf) - 1);
  g_coll.LoadNicknames(n > 0 ? g_coll_buf : NULL, n > 0 ? n : 0, CollFind);
  CollPath("lists.ini", path, sizeof(path));
  n = util::ReadAll(path, g_coll_buf, (int)sizeof(g_coll_buf) - 1);
  g_coll.LoadLists(n > 0 ? g_coll_buf : NULL, n > 0 ? n : 0, CollFind);
  int listed = 0;
  for (int l = 0; l < g_coll.ListCount(); ++l) listed += g_coll.ListSize(l);
  util::Log("Nicknames: %d; lists: Favorites (%d) and %d of your own, %d figure(s) in all",
            g_coll.NickCount(), g_coll.ListSize(0), g_coll.ListCount() - 1, listed);
}

#if defined(SKYPORTAL_MENU_DRAWN) || defined(SKYPORTAL_SIDELOAD)  // (the menu's keyboard, the phone)
const char* CollRel(int id) { return g_library.Rel(id); }

bool SaveCollection(bool nicknames) {
  const int n = nicknames ? g_coll.SaveNicknames(g_coll_buf, sizeof(g_coll_buf), CollRel)
                          : g_coll.SaveLists(g_coll_buf, sizeof(g_coll_buf), CollRel);
  const char* file = nicknames ? "nicknames.ini" : "lists.ini";
  if (n < 0) {
    util::Log("%s: too much to write (over %d KB), not saved", file, (int)(sizeof(g_coll_buf) / 1024));
    return false;
  }
  char path[MAX_PATH];
  CollPath(file, path, sizeof(path));
  if (!util::WriteAll(path, g_coll_buf, (DWORD)n, false)) {
    util::Log("%s: couldn't write %s", file, path);
    return false;
  }
  return true;
}

void ListsChanged(int list, bool deleted);  // the menu and the phones follow (defined with the menus)

// A nickname or a list changed: written straight away, and the phones hear about it.
void MetaChanged(bool nicknames, int list, bool deleted) {
  SaveCollection(nicknames);
  InterlockedIncrement(&g_meta_rev);
  if (!nicknames) ListsChanged(list, deleted);
  remote::Changed();
}
#endif

// Places library entry `id` in the first free slot. Returns the slot or -1.
void FlushWrites();

int PlaceFigure(int id, bool announce) {
  if (id < 0) return -1;
  const int existing = g_portal.FindTag(id);
  if (existing >= 0) {
    if (announce) Toast("%s is already on the rift", g_library.Name(id));
    return existing;
  }
  const int slot = g_portal.FirstFreeSlot();
  if (slot < 0) {
    if (announce) Toast("The rift is full (16 figures)");
    return -1;
  }
  // Only when the game wrote to it and it was taken off a moment ago does its file need saving
  // first (else it would come back without that progress); otherwise there's nothing to wait for.
  if (g_portal.HasUnsaved(id)) FlushWrites();
  static sp::u8 data[sp::kFigureSize];
  if (!g_library.Load(id, data)) {
    if (announce) Toast("Could not read %s", g_library.Rel(id));
    util::Log("Load failed: %s", g_library.Rel(id));
    return -1;
  }
  g_portal.Place(slot, data, id);
  util::Log("Placed '%s' in slot %d", g_library.Rel(id), slot);
  if (announce) Toast("+ %s placed on the rift", g_library.Name(id));
  return slot;
}

bool RemoveFigure(int id, bool announce) {
  const int slot = g_portal.FindTag(id);
  if (slot < 0) {
    if (announce) Toast("%s is not on the rift", g_library.Name(id));
    return false;
  }
  g_portal.Remove(slot);
  util::Log("Removed '%s' from slot %d", g_library.Rel(id), slot);
  if (announce) Toast("- %s taken off the rift", g_library.Name(id));
  return true;
}

sp::Lock g_flush_lock;  // keeps saves one at a time

// What kind of figure: "Swap in" only replaces figures of the same kind (a trap swaps the trap, a
// Skylander the Skylander(s), a vehicle the vehicle), and leaves the rest on the rift.
enum FigureKind { kKindCharacter, kKindTop, kKindBottom, kKindTrap, kKindVehicle, kKindItem };

FigureKind KindOf(int id) {
  switch (g_library.GetCategory(id)) {
    case FigureLibrary::kCatTrap: return kKindTrap;
    case FigureLibrary::kCatVehicle: return kKindVehicle;
    case FigureLibrary::kCatAdventure:
    case FigureLibrary::kCatItem: return kKindItem;
    default: break;
  }
  // A Skylander: a swapper's half by its character id (1000s bottoms, 2000s tops).
  int toy = g_library.CharacterId(id);
  if (toy <= 0) {
    static sp::u8 dump[sp::kFigureSize];
    if (g_library.Load(id, dump)) toy = elements::ToyId(dump);
  }
  if (toy >= 1000 && toy < 2000) return kKindBottom;
  if (toy >= 2000 && toy < 3000) return kKindTop;
  return kKindCharacter;
}

// Does putting on a figure of kind `incoming` take a figure of kind `on` off? Skylanders replace
// Skylanders, including both Swap Force halves; a swapper half replaces the same half (and a
// whole Skylander), so you can swap just the top or just the bottom.
bool SwapReplaces(FigureKind incoming, FigureKind on) {
  switch (incoming) {
    case kKindCharacter: return on == kKindCharacter || on == kKindTop || on == kKindBottom;
    case kKindTop: return on == kKindTop || on == kKindCharacter;
    case kKindBottom: return on == kKindBottom || on == kKindCharacter;
    default: return on == incoming;
  }
}

// "Swap in" the way it goes on a real portal: everything comes off, the game sees the empty
// portal, and only then (a moment later) does the new figure go on. Removing and adding in the
// same instant left Giants stuck on its loading screen (it sees both in one status report, all
// the more when the menu had the game paused). The figure waits here until the game has been
// told about the removals and has polled the portal a few more times, [General] SwapGapMs later
// (150 ms: a quick hand on a real portal; Giants gets at least 400 ms).
struct PendingSwap {
  int id;               // figure to put on, -1 = none
  bool removed;         // something was taken off for it (else it goes straight on)
  bool removal_seen;    // the game has had the status report saying the figures were removed
  DWORD seen_tick;
  sp::u32 seen_reports;
  bool toast;           // announce it with a toast (menus without a screen of their own)
};
PendingSwap g_swap = {-1, false, false, 0, 0, false};

void MenuChanged();  // the menu on screen shows the rift as it is now (defined with the menus)
DWORD SwapGapMs() { return (g_game == 2 && g_set.swap_gap_ms < 400) ? 400u : (DWORD)g_set.swap_gap_ms; }
sp::u32 SwapGapReports() { return g_game == 2 ? 8u : 3u; }

void CancelSwap() { g_swap.id = -1; }

// Called often from the worker thread: puts the waiting figure on once it's time.
void ServiceSwap() {
  if (g_swap.id < 0) return;
  bool now = false;
  if (!g_swap.removed || !g_portal.Active()) {
    now = true;  // nothing came off, or the game isn't using the portal: nothing to see in between
  } else if (!g_swap.removal_seen) {
    if (!g_portal.RemovalPending()) {
      g_swap.removal_seen = true;
      g_swap.seen_tick = GetTickCount();
      g_swap.seen_reports = g_portal.StatusReports();
    }
  } else if (g_portal.StatusReports() - g_swap.seen_reports >= SwapGapReports() &&
             GetTickCount() - g_swap.seen_tick >= SwapGapMs()) {
    now = true;
  }
  if (!now) return;
  const int id = g_swap.id;
  g_swap.id = -1;
  if (PlaceFigure(id, false) >= 0) {
    if (g_swap.toast) {
      Toast("Swapped in %s", g_library.Name(id));
    } else {
      util::Log("Swapped in %s", g_library.Name(id));
    }
  }
  SavePortalState();
  MenuChanged();
}

void FlushWrites() {
  sp::Guard guard(g_flush_lock);
  static sp::u8 data[sp::kFigureSize];
  int id = -1;
  while (g_portal.TakeDirty(&id, data)) {
    if (!g_set.save_writes || id < 0) continue;
    if (g_library.Save(id, data)) {
      util::Log("Saved game progress to '%s'", g_library.Rel(id));
    } else {
      util::Log("SAVE FAILED for '%s'", g_library.Rel(id));
      Toast("Could not save %s!", g_library.Name(id));
    }
  }
}

// Loads one line from portal_state.txt or a [Startup] value: a relative path or a figure name.
int ResolveFigureRef(const char* ref) {
  int id = g_library.FindByRel(ref);
  if (id < 0) id = g_library.FindByName(ref);
  return id;
}

void RestoreStartupFigures() {
  if (g_set.remember) {
    static char buf[8 * 1024];
    char path[MAX_PATH];
    sprintf_s(path, sizeof(path), "%s\\rift_state.txt", g_root);
    int n = util::ReadAll(path, buf, sizeof(buf) - 1);
    if (n <= 0) {  // the old name
      sprintf_s(path, sizeof(path), "%s\\portal_state.txt", g_root);
      n = util::ReadAll(path, buf, sizeof(buf) - 1);
    }
    if (n > 0) {
      buf[n] = 0;
      char* ctx = NULL;
      for (char* line = strtok_s(buf, "\r\n", &ctx); line; line = strtok_s(NULL, "\r\n", &ctx)) {
        if (*line == ';' || *line == '#' || !*line) continue;
        PlaceFigure(ResolveFigureRef(line), false);
      }
    }
  }
  const char* key;
  const char* value;
  for (int i = 0; g_config.Value("Startup", i, &key, &value); ++i) {
    if (*value) PlaceFigure(ResolveFigureRef(value), false);
  }
}

// --------------------------------------------------------------------------------------------
// Controller menu
// --------------------------------------------------------------------------------------------

struct Menu {
  int pos;              // browse position of the selected figure
  int active_pad;       // pad holding the modifier, -1 when none
  WORD prev_buttons;
  DWORD repeat_at;      // tick of the next auto-repeat while a D-pad direction is held
  DWORD toast_at;       // tick to show the pending "selected" toast (0 = none)
  DWORD last_scan;
  DWORD held_since;     // tick the modifier went down
  bool tap;             // modifier held with no other button pressed (skypop opens on release)
};
Menu g_menu = {0, -1, 0, 0, 0, 0, 0, false};

// The nickname keyboard of the full-screen menu (START on a figure).
struct Keyboard {
  bool open;
  int id;                            // the figure being named
  char text[coll::kNickMax + 1];
  int row, col;                      // the key picked
  bool caps;
  bool caps_once;                    // capitals for the first letter only (until CAPS is pressed)
  DWORD repeat_at;                   // D-pad auto-repeat
};
Keyboard g_kb;

#if defined(SKYPORTAL_MENU_DRAWN)
const bool kBrowseToasts = false;  // the drawn panel shows the selection and portal state
#else
const bool kBrowseToasts = true;
#endif

bool ModifierHeld(const XINPUT_GAMEPAD& pad) {
  return (pad.wButtons & g_set.modifier) == g_set.modifier &&
         (!g_set.need_lt || pad.bLeftTrigger >= 0x80) &&
         (!g_set.need_rt || pad.bRightTrigger >= 0x80);
}

int SelectedId() { return g_library.BrowseAt(g_menu.pos); }

void ShowSelection() {
  const int id = SelectedId();
  if (id < 0) {
    Toast("No figures found - copy dumps to %s\\Figures", g_root);
    return;
  }
  Toast("%s%s  (%d/%d)", g_library.Name(id), g_portal.FindTag(id) >= 0 ? " [on the rift]" : "",
        g_menu.pos + 1, g_library.BrowseCount());
}

int PortalNames(char* text, int cap) {
  int len = 0;
  int count = 0;
  text[0] = 0;
  for (int slot = 0; slot < sp::kMaxSlots; ++slot) {
    const int id = g_portal.Tag(slot);
    if (id < 0) continue;
    if (len < cap - 8) {
      len += sprintf_s(text + len, cap - len, "%s%s", count ? ", " : "", g_library.Name(id));
    }
    ++count;
  }
  // A figure being swapped in counts as on (it goes on in a moment).
  if (g_swap.id >= 0 && g_portal.FindTag(g_swap.id) < 0) {
    if (len < cap - 8) {
      len += sprintf_s(text + len, cap - len, "%s%s", count ? ", " : "", g_library.Name(g_swap.id));
    }
    ++count;
  }
  return count;
}

void ShowPortalContents() {
  char text[200];
  const int count = PortalNames(text, 150);
  if (!count) {
    Toast("The rift is empty");
  } else {
    Toast("On the rift (%d): %s", count, text);
  }
}

void MoveSelection(int delta) {
  const int n = g_library.BrowseCount();
  if (n <= 0) return;
  g_menu.pos = ((g_menu.pos + delta) % n + n) % n;
  if (kBrowseToasts) g_menu.toast_at = GetTickCount() + 350;  // toast once the selection settles
}

// "2. Giants\4) LightCores\Hex LightCore.dump" -> "Giants / LightCores" (folders only, without
// their "2. " / "4) " ordering prefixes). "My Figures" for a dump at the top of Figures.
void GroupName(const char* rel, char* out, int cap) {
  int len = 0;
  out[0] = 0;
  if (!strchr(rel, '\\')) {
    strncpy_s(out, cap, "My Figures", _TRUNCATE);
    return;
  }
  const char* p = rel;
  for (;;) {
    const char* slash = strchr(p, '\\');
    if (!slash) break;
    const char* q = p;
    while (q < slash && *q >= '0' && *q <= '9') ++q;
    if (q > p && q < slash && (*q == '.' || *q == ')')) {
      ++q;
      while (q < slash && *q == ' ') ++q;
    } else {
      q = p;
    }
    if (len && len < cap - 4) {
      strcpy(out + len, " / ");
      len += 3;
    }
    while (q < slash && len < cap - 1) out[len++] = *q++;
    out[len] = 0;
    p = slash + 1;
  }
}

}  // namespace

// ---- menu_api.h: what the pop-up and drawn menus use ----
namespace menu {

const char* GameName(int game) {
  static const char* const kNames[] = {"", "Spyro's Adventure", "Giants", "Swap Force",
                                       "Trap Team", "SuperChargers", "Imaginators"};
  return game >= 1 && game <= 6 ? kNames[game] : "";
}

int Pos() { return g_menu.pos; }

void SetPos(int pos) {
  const int n = g_library.BrowseCount();
  g_menu.pos = n > 0 ? ((pos % n) + n) % n : 0;
}

// The lists: everything, My Picks (from picks.ini), each game's own figures, and the lists made
// on the phone (Favorites first).
enum { kViewAll = 0, kViewPicks = 1, kViewGame1 = 2, kViewList0 = 8, kViewCount = 8 + coll::kMaxLists };

int ListOfView(int view) {  // the list a view shows (0 = Favorites), or -1
  return view >= kViewList0 && view < kViewCount && view - kViewList0 < g_coll.ListCount() ? view - kViewList0 : -1;
}

int CurrentView() {
  if (g_library.ListTag() >= 0) return kViewList0 + g_library.ListTag();
  if (g_library.PicksMode()) return kViewPicks;
  if (g_library.ViewGame() > 0) return kViewGame1 + g_library.ViewGame() - 1;
  return kViewAll;
}

// Shows `view` in the library (the menu's list, or the phone's for a moment).
void ApplyView(int view) {
  const int list = ListOfView(view);
  g_library.SetPicksMode(view == kViewPicks);
  g_library.SetViewGame(view >= kViewGame1 && view < kViewList0 ? view - kViewGame1 + 1 : 0);
  if (list >= 0) {
    g_library.SetListView(g_coll.ListIds(list), g_coll.ListSize(list), list);
  } else {
    g_library.SetListView(NULL, -1, -1);
  }
  g_library.Resort();
}

// The order R3 goes through them: everything, Favorites and your lists (those with figures in
// them), My Picks (when the game has picks), then each game's own figures (for the games that
// have figures in the list), and back to everything.
int NextView(int cur) {
  int order[kViewCount];
  int n = 0;
  order[n++] = kViewAll;
  for (int l = 0; l < g_coll.ListCount(); ++l) {
    if (g_coll.ListSize(l) > 0) order[n++] = kViewList0 + l;
  }
  if (g_library.PickCount() > 0) order[n++] = kViewPicks;
  for (int g = 1; g <= 6; ++g) {
    if (g_library.CountFromGame(g) > 0) order[n++] = kViewGame1 + g - 1;
  }
  for (int k = 0; k < n; ++k) {
    if (order[k] == cur) return order[(k + 1) % n];
  }
  return kViewAll;  // (a list that has just been emptied or deleted)
}

// "all figures", "My Picks", "Giants figures", "Favorites" (log, section label); `upper`: the
// hint's form.
void ViewName(int view, char* out, int cap, bool upper = false) {
  const int list = ListOfView(view);
  if (list >= 0) {
    if (upper) {
      int i = 0;
      for (const char* p = g_coll.ListName(list); *p && i < cap - 1; ++p) {
        out[i++] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 'a' + 'A') : *p;
      }
      out[i] = 0;
    } else {
      strncpy_s(out, cap, g_coll.ListName(list), _TRUNCATE);
    }
  } else if (view == kViewPicks) {
    strncpy_s(out, cap, upper ? "MY PICKS" : "My Picks", _TRUNCATE);
  } else if (view >= kViewGame1 && view < kViewList0) {
    static const char* const kShort[] = {"SPYRO'S ADV.", "GIANTS", "SWAP FORCE", "TRAP TEAM",
                                         "SUPERCHARGERS", "IMAGINATORS"};
    if (upper) strncpy_s(out, cap, kShort[view - kViewGame1], _TRUNCATE);
    else sprintf_s(out, cap, "%s figures", GameName(view - kViewGame1 + 1));
  } else {
    strncpy_s(out, cap, upper ? "ALL" : "all figures", _TRUNCATE);
  }
}

// The section of the figure at browse position `pos`: "Skylanders / Magic" by type, or its folder
// and element ("Giants / LightCores / Life") when browsing folder by folder.
void PlainSection(int pos, int id, char* out, int cap) {
  if (g_library.SectionLabel(pos, out, cap)) return;
  GroupName(g_library.Rel(id), out, cap);
  // The element section (unless the folder is already named after it, like a trap's).
  const char* el = elements::Name((elements::Element)g_library.Element(id));
  if (el[0]) {
    const char* last = strrchr(out, '/');
    last = last ? last + 2 : out;
    if (_stricmp(last, el)) {
      const int len = (int)strlen(out);
      sprintf_s(out + len, cap - len, " / %s", el);
    }
  }
}

void GetView(int pos, View* out) {
  const int n = g_library.BrowseCount();
  memset(out, 0, sizeof(*out));
  out->count = n;
  out->pos = n > 0 ? ((pos % n) + n) % n : 0;
  out->id = g_library.BrowseAt(out->pos);
  out->name = out->id >= 0 ? g_library.Name(out->id) : "";
  out->element = out->id >= 0 ? g_library.Element(out->id) : elements::kElementUnknown;
  if (out->id >= 0) {
    out->game = g_library.Game(out->id);
    g_library.Series(out->id, out->series, (int)sizeof(out->series));
    char section[96];
    char list[48] = "";
    const int roster = g_library.RosterAt(out->pos);
    if (CurrentView() == kViewPicks && roster > 0) {
      sprintf_s(list, sizeof(list), "%s Roster / ", GameName(roster));  // My Picks: whose roster
    } else if (CurrentView() != kViewAll) {
      ViewName(CurrentView(), list, sizeof(list));
      sprintf_s(list + strlen(list), sizeof(list) - strlen(list), " / ");
    }
    PlainSection(out->pos, out->id, section, (int)sizeof(section));
    sprintf_s(out->group, sizeof(out->group), "%s%s", list, section);
    if (!g_library.ImagePath(out->id, out->image, (int)sizeof(out->image))) out->image[0] = 0;
  }
  out->on_portal = out->id >= 0 && (g_portal.FindTag(out->id) >= 0 || out->id == g_swap.id);
  out->portal_count = PortalNames(out->portal_names, (int)sizeof(out->portal_names));
  out->picks_available = g_library.PickCount() > 0;
  out->picks_mode = g_library.PicksMode();
  const int next = NextView(CurrentView());
  if (next != CurrentView()) ViewName(next, out->next_list, (int)sizeof(out->next_list), true);
  strncpy_s(out->remote, sizeof(out->remote), remote::Address(), _TRUNCATE);
  if (out->id >= 0) strncpy_s(out->nickname, sizeof(out->nickname), g_coll.Nick(out->id), _TRUNCATE);
  out->kb_open = g_kb.open;
  if (g_kb.open) {
    strncpy_s(out->kb_text, sizeof(out->kb_text), g_kb.text, _TRUNCATE);
    out->kb_row = g_kb.row;
    out->kb_col = g_kb.col;
    out->kb_caps = g_kb.caps;
    if (g_kb.id >= 0) {  // the keyboard names the figure it was opened on
      out->name = g_library.Name(g_kb.id);
    }
  }
#if defined(SKYPORTAL_MENU_DRAWN)
  out->start_hint = out->id >= 0 && !(g_set.modifier & XINPUT_GAMEPAD_START);
#endif
}

// Acts on library entry `id` (the menu's selection, or the figure the phone picked).
void DoOn(int id, Action action, bool toast) {
  bool changed = false;
  switch (action) {
    case kAdd:
      if (id >= 0 && id == g_swap.id) break;  // already going on
      changed = PlaceFigure(id, toast) >= 0;
      break;
    case kRemove:
      if (id >= 0 && id == g_swap.id) {  // was about to be swapped in: not any more
        CancelSwap();
        util::Log("Swap of %s cancelled", g_library.Name(id));
        changed = true;
        break;
      }
      changed = RemoveFigure(id, toast);
      break;
    case kSwap: {  // figures of the same kind off now; the selected one on once the game saw that
      if (id < 0 || g_portal.FindTag(id) >= 0) break;  // already on
      // (No saving first: the portal keeps what the game wrote to the figures taken off, and the
      // worker saves it in the background while the new one waits for its moment.)
      const FigureKind kind = KindOf(id);
      int removed = 0;
      for (int slot = 0; slot < sp::kMaxSlots; ++slot) {
        const int on = g_portal.Tag(slot);
        if (on >= 0 && SwapReplaces(kind, KindOf(on))) {
          util::Log("Swap: taking %s off", g_library.Name(on));
          g_portal.Remove(slot);
          ++removed;
        }
      }
      g_swap.id = id;
      g_swap.removed = removed > 0;
      g_swap.removal_seen = false;
      g_swap.toast = toast;
      if (removed) {
        util::Log("Swapping in %s: it goes on once the game has seen the %d figure(s) come off",
                  g_library.Name(id), removed);
      }
      ServiceSwap();  // straight away if nothing came off or the game isn't using the portal
      changed = true;
      break;
    }
    case kClear:
      CancelSwap();
      g_portal.RemoveAll();
      if (toast) Toast("All figures taken off the rift");
      else util::Log("All figures taken off the rift");
      changed = true;
      break;
  }
  if (changed) SavePortalState();
}

void Do(Action action, bool toast) { DoOn(SelectedId(), action, toast); }

}  // namespace menu

#if defined(SKYPORTAL_MENU_DRAWN) || defined(SKYPORTAL_SIDELOAD)
namespace {

// A list changed (or went): the menu on the TV, if it's showing that list, shows it as it is now
// (on the same figure), and moves on to everything when the list is gone or empty.
void ListsChanged(int list, bool deleted) {
  const int shown = g_library.ListTag();
  if (shown < 0) return;
  int view = menu::kViewList0 + shown;
  if (deleted && shown == list) view = menu::kViewAll;
  if (deleted && shown > list) view = menu::kViewList0 + shown - 1;
  const int l = menu::ListOfView(view);
  if (l >= 0 && g_coll.ListSize(l) == 0) view = menu::kViewAll;
  const int sel = SelectedId();
  menu::ApplyView(view);
  const int p = g_library.BrowsePos(sel);
  g_menu.pos = p >= 0 ? p : (g_menu.pos < g_library.BrowseCount() ? g_menu.pos : 0);
  MenuChanged();
}

}  // namespace
#endif

namespace {

void HandleMenuPress(WORD pressed) {
  if (pressed & XINPUT_GAMEPAD_DPAD_RIGHT) MoveSelection(+1);
  if (pressed & XINPUT_GAMEPAD_DPAD_LEFT) MoveSelection(-1);
  if (pressed & XINPUT_GAMEPAD_DPAD_DOWN) MoveSelection(+10);
  if (pressed & XINPUT_GAMEPAD_DPAD_UP) MoveSelection(-10);

  // With the drawn panel on screen the result is visible there, so no toast.
  const bool toast = kBrowseToasts;
  if (pressed & XINPUT_GAMEPAD_A) {
    g_menu.toast_at = 0;
    menu::Do(menu::kAdd, toast);
  }
  if (pressed & XINPUT_GAMEPAD_B) {
    g_menu.toast_at = 0;
    menu::Do(menu::kRemove, toast);
  }
  if (pressed & XINPUT_GAMEPAD_Y) {
    g_menu.toast_at = 0;
    menu::Do(menu::kSwap, toast);
  }
  if (pressed & XINPUT_GAMEPAD_X) {
    g_menu.toast_at = 0;
    menu::Do(menu::kClear, toast);
  }
  if ((pressed & XINPUT_GAMEPAD_START) && !(g_set.modifier & XINPUT_GAMEPAD_START)) {
    g_menu.toast_at = 0;
    ShowPortalContents();
  }
}

#if defined(SKYPORTAL_MENU_DRAWN)

// ---- skydrawn: tap the modifier to open the full-screen menu, tap it again to close ----

const DWORD kRescanAfterMs = 120000;  // re-read the Figures folder on open after 2 minutes

struct Drawn {
  int pad;                 // pad driving the open menu, -1 when closed
  WORD prev_buttons;
  bool prev_lt, prev_rt;
  bool mod_was_held[4];    // modifier state per pad at the last poll (opens on the press)
  bool wait_release;       // closed; the game gets the controller back once all is released
  int wait_pad;
};
Drawn g_drawn = {-1, 0, false, false, {false, false, false, false}, false, -1};

void DrawnRescanIfStale(DWORD now) {
  if (now - g_menu.last_scan < kRescanAfterMs) return;
  const int sel = SelectedId();
  g_library.Scan();
  g_menu.last_scan = now;
  const int p = g_library.BrowseAt(g_menu.pos) == sel ? g_menu.pos : g_library.BrowsePos(sel);
  if (p >= 0) g_menu.pos = p;
  if (g_menu.pos >= g_library.BrowseCount()) g_menu.pos = 0;
}

void DrawnRefresh() {
  menu::View view;
  menu::GetView(g_menu.pos, &view);
  overlay::Update(view);
}

void DrawnClose() {
  g_kb.open = false;  // a nickname being typed is let go
  overlay::Close();
  g_drawn.wait_release = true;
  g_drawn.wait_pad = g_drawn.pad;
  g_drawn.pad = -1;
  util::Log("Menu closed");
}

// ---- the nickname keyboard (START on a figure) ----

void KeyboardOpen() {
  const int id = SelectedId();
  if (id < 0) return;
  memset(&g_kb, 0, sizeof(g_kb));
  g_kb.open = true;
  g_kb.id = id;
  strncpy_s(g_kb.text, sizeof(g_kb.text), g_coll.Nick(id), _TRUNCATE);
  g_kb.caps = g_kb.caps_once = !g_kb.text[0];  // a capital first letter
  util::Log("Menu: nickname keyboard for %s", g_library.Name(id));
}

void KeyboardClose(bool save) {
  if (save && g_coll.SetNick(g_kb.id, g_kb.text)) {
    const char* nick = g_coll.Nick(g_kb.id);
    if (nick[0]) util::Log("Nickname: %s is \"%s\"", g_library.Rel(g_kb.id), nick);
    else util::Log("Nickname: %s has none now", g_library.Rel(g_kb.id));
    MetaChanged(true, -1, false);
  }
  g_kb.open = false;
}

void KeyboardType(char c) {
  const int len = (int)strlen(g_kb.text);
  if (len >= coll::kNickMax || !c) return;
  if (c == ' ' && (len == 0 || g_kb.text[len - 1] == ' ')) return;
  g_kb.text[len] = c;
  g_kb.text[len + 1] = 0;
  if (g_kb.caps_once && c != ' ') g_kb.caps = g_kb.caps_once = false;
}

void KeyboardDelete() {
  const int len = (int)strlen(g_kb.text);
  if (len > 0) g_kb.text[len - 1] = 0;
  if (len <= 1 && !g_kb.caps) g_kb.caps = g_kb.caps_once = true;  // starting again: a capital
}

void KeyboardMove(int dr, int dc) {
  int first, last;
  if (dr) {
    g_kb.row = (g_kb.row + dr + kbd::kRows) % kbd::kRows;
    kbd::KeySpan(g_kb.row, g_kb.col, &first, &last);
    g_kb.col = first;
  }
  if (dc) {
    kbd::KeySpan(g_kb.row, g_kb.col, &first, &last);
    g_kb.col = dc > 0 ? (last + 1) % kbd::kCols : (first + kbd::kCols - 1) % kbd::kCols;
    kbd::KeySpan(g_kb.row, g_kb.col, &first, &last);  // a wide key: its first column
    g_kb.col = first;
  }
}

// A: the key picked. B: delete. X: space. Y: capitals. START: save. BACK: cancel. Returns true
// when something changed.
bool KeyboardHandle(WORD pressed, WORD buttons, DWORD now) {
  const WORD kDirs = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT |
                     XINPUT_GAMEPAD_DPAD_RIGHT;
  WORD dirs = (WORD)(pressed & kDirs);
  if (dirs) {
    g_kb.repeat_at = now + 400;
  } else if ((buttons & kDirs) && (int)(now - g_kb.repeat_at) >= 0) {  // held: auto-repeat
    dirs = (WORD)(buttons & kDirs);
    g_kb.repeat_at = now + 120;
  }
  bool changed = dirs != 0;
  if (dirs & XINPUT_GAMEPAD_DPAD_UP) KeyboardMove(-1, 0);
  if (dirs & XINPUT_GAMEPAD_DPAD_DOWN) KeyboardMove(+1, 0);
  if (dirs & XINPUT_GAMEPAD_DPAD_LEFT) KeyboardMove(0, -1);
  if (dirs & XINPUT_GAMEPAD_DPAD_RIGHT) KeyboardMove(0, +1);
  if (pressed & XINPUT_GAMEPAD_BACK) {
    KeyboardClose(false);
    return true;
  }
  if (pressed & XINPUT_GAMEPAD_START) {
    KeyboardClose(true);
    return true;
  }
  if (pressed & XINPUT_GAMEPAD_A) {
    switch (kbd::KeyAt(g_kb.row, g_kb.col)) {
      case kbd::kChar: KeyboardType(kbd::CharAt(g_kb.row, g_kb.col, g_kb.caps)); break;
      case kbd::kSpace: KeyboardType(' '); break;
      case kbd::kDelete: KeyboardDelete(); break;
      case kbd::kCaps: g_kb.caps = !g_kb.caps; g_kb.caps_once = false; break;
      case kbd::kDone: KeyboardClose(true); return true;
    }
    changed = true;
  }
  if (pressed & XINPUT_GAMEPAD_B) { KeyboardDelete(); changed = true; }
  if (pressed & XINPUT_GAMEPAD_X) { KeyboardType(' '); changed = true; }
  if (pressed & XINPUT_GAMEPAD_Y) { g_kb.caps = !g_kb.caps; g_kb.caps_once = false; changed = true; }
  return changed;
}

bool PadIdle(const XINPUT_GAMEPAD& g) {
  return g.wButtons == 0 && g.bLeftTrigger < 0x30 && g.bRightTrigger < 0x30;
}

void PollDrawn(DWORD now) {
  XINPUT_STATE st;
  // The console's own pop-up menu, used when the game leaves too little memory for the
  // full-screen one: it owns the controller while it's up.
  popmenu::Poll();
  if (popmenu::IsOpen()) return;
  // The Xbox Guide (or another system screen) is drawn through the game's own frames, and it
  // takes the controller: the menu must never hold the game while it's up.
  const bool system_ui = XamIsUIActive() != 0;
  if (g_drawn.pad < 0) {
    // Closed: a fresh press of the modifier on any pad opens the menu (not over the Guide).
    for (int i = 0; i < 4; ++i) {
      const bool ok = XInputGetState(i, &st) == ERROR_SUCCESS;
      const bool held = ok && ModifierHeld(st.Gamepad);
      if (held && !g_drawn.mod_was_held[i] && !g_drawn.wait_release && g_drawn.pad < 0 &&
          !system_ui) {
        if (!g_set.full_screen) {
          util::Log("Menu: pop-up menu ([Menu] FullScreenMenu = 0), pad %d", i);
          popmenu::Open(i);
        } else if (!overlay::Ready()) {
          Toast("The Static Rift: the menu isn't ready yet (no game frames seen)");
        } else {
          DrawnRescanIfStale(now);
          hooks::SetInputBlocked(true);
          menu::View view;
          menu::GetView(g_menu.pos, &view);
          overlay::Open(view);
          g_drawn.pad = i;
          g_drawn.prev_buttons = st.Gamepad.wButtons;  // buttons already down don't count
          g_drawn.prev_lt = st.Gamepad.bLeftTrigger >= 0x80;
          g_drawn.prev_rt = st.Gamepad.bRightTrigger >= 0x80;
          g_menu.repeat_at = now + 400;
          util::Log("Menu opened (pad %d, %d figures, on %s)", i, g_library.BrowseCount(),
                    view.name && view.name[0] ? view.name : "none");
        }
      }
      g_drawn.mod_was_held[i] = held;
    }
    // After closing, keep the game's controller idle until everything is let go, so the buttons
    // used in the menu (and the closing combo) never reach the game.
    if (g_drawn.wait_release) {
      const bool idle = g_drawn.wait_pad < 0 || XInputGetState(g_drawn.wait_pad, &st) != ERROR_SUCCESS ||
                        PadIdle(st.Gamepad);
      if (idle) {
        g_drawn.wait_release = false;
        hooks::SetInputBlocked(false);
      }
    }
    return;
  }

  // Open: this pad drives the menu.
  const int pad = g_drawn.pad;
  if (system_ui) {  // the Guide button: close at once so the game carries on and draws the Guide
    DrawnClose();
    util::Log("Menu: closed for the Xbox Guide (or another system screen)");
    return;
  }
  if (!overlay::IsOpen()) {
    // The full-screen menu couldn't get its memory (the overlay gave up): the pop-up instead.
    g_drawn.pad = -1;
    g_drawn.wait_release = false;
    hooks::SetInputBlocked(false);
    util::Log("Menu: not enough memory for the full-screen menu here, opened the pop-up menu");
    popmenu::Open(pad);
    return;
  }
  if (XInputGetState(pad, &st) != ERROR_SUCCESS) {  // controller gone
    DrawnClose();
    return;
  }
  const XINPUT_GAMEPAD& g = st.Gamepad;
  const bool held = ModifierHeld(g);
  const bool mod_pressed = held && !g_drawn.mod_was_held[pad];
  g_drawn.mod_was_held[pad] = held;
  const WORD buttons = g.wButtons;
  const WORD pressed = (WORD)(buttons & ~g_drawn.prev_buttons);
  const bool lt = g.bLeftTrigger >= 0x80, rt = g.bRightTrigger >= 0x80;
  const bool lt_pressed = lt && !g_drawn.prev_lt, rt_pressed = rt && !g_drawn.prev_rt;
  g_drawn.prev_buttons = buttons;
  g_drawn.prev_lt = lt;
  g_drawn.prev_rt = rt;

  if (g_kb.open) {  // the nickname keyboard has the controller
    if (mod_pressed) {
      KeyboardClose(false);
      DrawnClose();
      return;
    }
    if (KeyboardHandle((WORD)(pressed & ~g_set.modifier), buttons, now)) DrawnRefresh();
    return;
  }
  if (mod_pressed || ((pressed & XINPUT_GAMEPAD_BACK) && !(g_set.modifier & XINPUT_GAMEPAD_BACK))) {
    DrawnClose();
    return;
  }
  bool changed = false;
  const WORD menu_pressed = (WORD)(pressed & ~g_set.modifier);
  if (menu_pressed & XINPUT_GAMEPAD_DPAD_RIGHT) { MoveSelection(+1); changed = true; }
  if (menu_pressed & XINPUT_GAMEPAD_DPAD_LEFT) { MoveSelection(-1); changed = true; }
  if (menu_pressed & XINPUT_GAMEPAD_DPAD_DOWN) {  // next folder
    g_menu.pos = g_library.GroupJump(g_menu.pos, +1);
    changed = true;
  }
  if (menu_pressed & XINPUT_GAMEPAD_DPAD_UP) {  // previous folder
    g_menu.pos = g_library.GroupJump(g_menu.pos, -1);
    changed = true;
  }
  if (lt_pressed && !g_set.need_lt) { MoveSelection(-10); changed = true; }
  if (rt_pressed && !g_set.need_rt) { MoveSelection(+10); changed = true; }
  if (menu_pressed & XINPUT_GAMEPAD_A) { menu::Do(menu::kAdd, false); changed = true; }
  if (menu_pressed & XINPUT_GAMEPAD_B) { menu::Do(menu::kRemove, false); changed = true; }
  if (menu_pressed & XINPUT_GAMEPAD_Y) { menu::Do(menu::kSwap, false); changed = true; }
  if (menu_pressed & XINPUT_GAMEPAD_X) { menu::Do(menu::kClear, false); changed = true; }
  if ((menu_pressed & XINPUT_GAMEPAD_START) && SelectedId() >= 0) {  // a nickname for this figure
    KeyboardOpen();
    changed = true;
  }
  if (menu_pressed & XINPUT_GAMEPAD_RIGHT_THUMB) {  // R3: the next list
    const int cur = menu::CurrentView(), next = menu::NextView(cur);
    if (next == cur) {
      util::Log("Menu: R3 - no other list for this game");
    } else {
      const int sel = SelectedId();
      menu::ApplyView(next);
      const int p = g_library.BrowsePos(sel);
      g_menu.pos = p >= 0 ? p : 0;
      char name[48];
      menu::ViewName(next, name, sizeof(name));
      util::Log("Menu: %s (%d figures)", name, g_library.BrowseCount());
      if (g_library.PicksMode()) {
        util::Log("Menu: %d of %d picks found for this game", g_library.PicksFound(),
                  g_library.PickCount());
        for (int i = 0; i < g_library.PickCount(); ++i) {
          if (!g_library.PickFound(i)) {
            util::Log("Menu: no figure for the pick \"%s\"", g_library.PickText(i));
          }
        }
      }
      changed = true;
    }
  }
  if (menu_pressed & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) {
    g_menu.repeat_at = now + 400;
  } else {
    // Auto-repeat while D-pad left/right stays held.
    const WORD dirs = buttons & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT);
    if (dirs && (int)(now - g_menu.repeat_at) >= 0) {
      MoveSelection((dirs & XINPUT_GAMEPAD_DPAD_RIGHT) ? +1 : -1);
      g_menu.repeat_at = now + 140;
      changed = true;
    }
  }
  if (changed) DrawnRefresh();
}

#endif  // SKYPORTAL_MENU_DRAWN

void MenuChanged() {
#if defined(SKYPORTAL_MENU_DRAWN)
  if (g_drawn.pad >= 0 && overlay::IsOpen()) DrawnRefresh();
#endif
}

void PollController(bool title_hooked) {
  const DWORD now = GetTickCount();
  if (!title_hooked) {
    g_menu.active_pad = -1;
    g_menu.toast_at = 0;
    return;
  }
#if defined(SKYPORTAL_MENU_DRAWN)
  PollDrawn(now);
  return;
#endif
#if defined(SKYPORTAL_MENU_POP)
  popmenu::Poll();
  if (popmenu::IsOpen()) {  // the console's dialog owns the controller
    g_menu.active_pad = -1;
    g_menu.tap = false;
    return;
  }
#endif

  XINPUT_STATE st;
  int pad = g_menu.active_pad;
  // Find the pad holding the modifier (stick with it while it stays held).
  if (pad < 0 || XInputGetState(pad, &st) != ERROR_SUCCESS || !ModifierHeld(st.Gamepad)) {
    pad = -1;
    for (int i = 0; i < 4; ++i) {
      if (XInputGetState(i, &st) == ERROR_SUCCESS && ModifierHeld(st.Gamepad)) {
        pad = i;
        break;
      }
    }
  }

  if (pad < 0) {
#if defined(SKYPORTAL_MENU_POP)
    // Modifier released: a quick tap with nothing else pressed opens the pop-up menu.
    if (g_menu.active_pad >= 0 && g_menu.tap && now - g_menu.held_since < 800) {
      popmenu::Open(g_menu.active_pad);
    }
#endif
    g_menu.active_pad = -1;
    g_menu.tap = false;
  } else {
    const WORD buttons = st.Gamepad.wButtons;
    if (g_menu.active_pad != pad) {  // modifier just went down
      g_menu.active_pad = pad;
      g_menu.prev_buttons = buttons;  // ignore buttons already held
      g_menu.held_since = now;
      g_menu.tap = (buttons & ~g_set.modifier) == 0;
      if (now - g_menu.last_scan > 10000) {
        const int sel = SelectedId();
        g_library.Scan();
        g_menu.last_scan = now;
        const int p = g_library.BrowseAt(g_menu.pos) == sel ? g_menu.pos : g_library.BrowsePos(sel);
        if (p >= 0) g_menu.pos = p;
        if (g_menu.pos >= g_library.BrowseCount()) g_menu.pos = 0;
      }
#if !defined(SKYPORTAL_MENU_POP)
      g_menu.toast_at = now + 1;  // show the current selection
#endif
    }
    const WORD pressed = (WORD)(buttons & ~g_menu.prev_buttons & ~g_set.modifier);
    if (pressed) {
      g_menu.tap = false;
      HandleMenuPress(pressed);
      g_menu.repeat_at = now + 400;
    } else {
      // Auto-repeat for a held D-pad left/right.
      const WORD dirs = buttons & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT);
      if (dirs && (int)(now - g_menu.repeat_at) >= 0) {
        MoveSelection((dirs & XINPUT_GAMEPAD_DPAD_RIGHT) ? +1 : -1);
        g_menu.repeat_at = now + 140;
      }
    }
    g_menu.prev_buttons = buttons;
  }

  if (g_menu.toast_at && (int)(now - g_menu.toast_at) >= 0) {
    g_menu.toast_at = 0;
    ShowSelection();
  }
}

// --------------------------------------------------------------------------------------------
// Startup + worker thread
// --------------------------------------------------------------------------------------------

#if defined(SKYPORTAL_SIDELOAD)
const bool kSystemDll = false;  // title DLL: drive links are local to the game
const int kRootWaitSeconds = 5; // the game is already running, drives are up
// A title DLL is unloaded the moment the game exits, so figure writes are saved quickly.
const DWORD kFlushIntervalMs = 250;
#else
const bool kSystemDll = true;   // system DLL loaded at boot
const int kRootWaitSeconds = 60;  // USB sticks can take a while to show up after boot
// Saved as quickly as the Peer Pressure build: the game can exit any moment after a write.
const DWORD kFlushIntervalMs = 250;
#endif

#if defined(SKYPORTAL_SIDELOAD)
// Filled in DllMain, before the game runs.
util::ExecutionId g_exec_id = {0, 0, 0};
bool g_have_exec_id = false;
int g_dllmain_patched = 0;
#endif

// The data folder: "The Static Rift" on the hard drive or a USB stick; a folder still called
// "SkyPortal" (its old name) is used when there's no "The Static Rift".
const char kRootName[] = "The Static Rift";

bool FindRoot() {
  static const char* kDrives[] = {"SKYHDD:", "SKYUSB0:", "SKYUSB1:", "SKYUSB2:"};
  static const char* kNames[] = {kRootName, "SkyPortal"};
  for (size_t n = 0; n < sizeof(kNames) / sizeof(kNames[0]); ++n) {
    for (size_t i = 0; i < sizeof(kDrives) / sizeof(kDrives[0]); ++i) {
      char root[MAX_PATH];
      sprintf_s(root, sizeof(root), "%s\\%s", kDrives[i], kNames[n]);
      if (util::DirExists(root)) {
        strncpy_s(g_root, sizeof(g_root), root, _TRUNCATE);
        return true;
      }
    }
  }
  return false;
}

void MountAndFindRoot() {
  util::MountDrive("SKYHDD:", "\\Device\\Harddisk0\\Partition1", kSystemDll);
  util::MountDrive("SKYUSB0:", "\\Device\\Mass0", kSystemDll);
  util::MountDrive("SKYUSB1:", "\\Device\\Mass1", kSystemDll);
  util::MountDrive("SKYUSB2:", "\\Device\\Mass2", kSystemDll);
  for (int tries = 0; tries < kRootWaitSeconds && !g_stop; ++tries) {
    if (FindRoot()) return;
    Sleep(1000);
  }
  // Nothing found: create the folder on the hard drive so the user knows where to put files.
  sprintf_s(g_root, sizeof(g_root), "SKYHDD:\\%s", kRootName);
  CreateDirectory(g_root, NULL);
}

void ScanForTitle(DWORD title);

// Finds the SkyPortal folder, opens the log, reads settings and the figure library, and puts the
// remembered/startup figures on the portal. Returns false if the plugin is disabled.
bool Startup() {
  util::Trace("looking for the data folder");
  MountAndFindRoot();
  util::Trace("data folder %s", g_root);
  char path[MAX_PATH];
  sprintf_s(path, sizeof(path), "%s\\Figures", g_root);
  CreateDirectory(path, NULL);
  sprintf_s(path, sizeof(path), "%s\\staticrift.log", g_root);
  util::LogOpen(path);
#if defined(SKYPORTAL_SIDELOAD)
  util::Log(SKYPORTAL_PRODUCT " (The Static Rift, Peer Pressure sideload, %s) - root %s",
            kMenuStyle, g_root);
  if (g_have_exec_id) {
    const DWORD v = g_exec_id.version;
    // Printed in sideload .ini syntax so it can be pasted into the game's .ini if needed.
    util::Log("Game: TitleID = 0x%08X  MediaID = 0x%08X  ModuleVersion = \"%u.%u.%u.%u\"",
              g_exec_id.title_id, g_exec_id.media_id, (v >> 28) & 0xF, (v >> 24) & 0xF,
              (v >> 8) & 0xFFFF, v & 0xFF);
  } else {
    util::Log("Game: execution ID not readable (title %08X)", XamGetCurrentTitleId());
  }
#else
  util::Log(SKYPORTAL_PRODUCT " (The Static Rift, Dashlaunch plugin, %s) - root %s", kMenuStyle,
            g_root);
#endif

  LoadSettings();
  g_library.SetRoot(g_root);
  util::Log(g_library.LoadIndex() ? "Figure index loaded (UI\\figures.idx)"
                                  : "No UI\\figures.idx: names and elements come from the files");
#if defined(SKYPORTAL_SIDELOAD)
  ScanForTitle(g_have_exec_id ? g_exec_id.title_id : XamGetCurrentTitleId());
  if (!g_set.enabled) {
    util::Log("Disabled in staticrift.ini ([General] Enabled=0)");
    return false;
  }
  RestoreStartupFigures();
  util::Log("%d figure(s) on the rift at start", g_portal.Count());
  return true;
#else
  // Dashlaunch: the boot is done; each Skylanders game gets its scan when it starts.
  if (!g_set.enabled) {
    util::Log("Disabled in staticrift.ini ([General] Enabled=0)");
    return false;
  }
  return true;
#endif
}

// Lists the figures the game `title` can use: figures only work in the game they came out with
// (1 = Spyro's Adventure ... 6 = Imaginators) and later ones, so the menu lists just those.
int GameNumber(DWORD title) {
  int game = 0;
  for (int i = 0; i < (int)(sizeof(kSkylandersTitles) / sizeof(kSkylandersTitles[0])); ++i) {
    if (kSkylandersTitles[i] == title) game = i + 1;
  }
  return game;
}

// "My Picks" (R3 in the full-screen menu): <root>\picks.ini, one section per game, any keys
// (the element names are just labels), values: figure names separated by commas.
void LoadPicks(int game) {
  static Config picks;  // big: kept off the stack
  static char names_buf[6144];
  enum { kMaxNames = 192 };
  static const char* names[kMaxNames];
  static signed char rosters[kMaxNames];
  int count = 0, rosters_used = 0;
  char path[MAX_PATH];
  sprintf_s(path, sizeof(path), "%s\\picks.ini", g_root);
  if (game && picks.Load(path)) {
    int used = 0;
    // The running game's roster and every earlier game's (a game can use its own figures and
    // the earlier games'); My Picks lists them newest game first.
    for (int g = game; g >= 1; --g) {
      const int before = count;
      const char* key;
      const char* value;
      for (int i = 0; picks.Value(menu::GameName(g), i, &key, &value); ++i) {
        // Split "Bouncer, Drobot" into names.
        for (const char* p = value; *p && count < kMaxNames;) {
          while (*p == ' ' || *p == ',') ++p;
          const char* start = p;
          while (*p && *p != ',') ++p;
          const char* end = p;
          while (end > start && end[-1] == ' ') --end;
          const int len = (int)(end - start);
          if (len > 0 && used + len + 1 < (int)sizeof(names_buf)) {
            memcpy(names_buf + used, start, len);
            names_buf[used + len] = 0;
            rosters[count] = (signed char)g;
            names[count++] = names_buf + used;
            used += len + 1;
          }
        }
      }
      if (count > before) ++rosters_used;
    }
  }
  g_library.SetPicks(names, count, rosters);
  g_library.SetPicksMode(false);  // each game starts on the full list
  g_library.SetViewGame(0);
  if (game) {
    util::Log("My Picks for %s: %d name(s) from %d roster(s) in picks.ini%s", menu::GameName(game),
              count, rosters_used, count ? " (R3 in the menu shows them)" : "");
  }
}

void ScanForTitle(DWORD title) {
  const int game = GameNumber(title);
  g_game = game;
  g_library.SetGameFilter(g_set.only_compatible ? game : 0);
  g_library.SetNewestOnly(g_set.newest_only);
  g_library.SetSortByType(g_set.sort_by_type);
  LoadPicks(game);
  g_library.SetListView(NULL, -1, -1);  // each game starts on the full list
  const int figures = g_library.Scan();
  g_menu.last_scan = GetTickCount();
  LoadCollections();  // (after the scan: they name figures by their files)
  util::Log("%d figure file(s) in Figures: %d listed in %s, %d for later games ([Menu] "
            "OnlyCompatible = 0 lists them), %d older versions of a newer figure ([Menu] "
            "NewestOnly = 0 lists them)",
            figures + g_library.HiddenCount() + g_library.OlderCount(), figures,
            game ? menu::GameName(game) : "this game", g_library.HiddenCount(), g_library.OlderCount());
  if (g_library.UnreadableDirs() > 0) {
    util::Log("%d folder(s) in Figures are empty or couldn't be opened", g_library.UnreadableDirs());
  }
  util::Log(g_set.sort_by_type ? "Menu order: by type (gimmick Skylanders, Skylanders, crystals, "
                                 "traps, vehicles, magic items, adventure packs), element, name"
                               : "Menu order: folder by folder ([Menu] SortByType = 0)");
}

// --------------------------------------------------------------------------------------------
// The phone remote's API (src/xbox/remote.cpp hands each call to this, the worker thread)
// --------------------------------------------------------------------------------------------
#if defined(SKYPORTAL_REMOTE)  // (the Peer Pressure builds; the Dashlaunch build with the drawn menu)

// The server waits this long after the game starts, so the game sets up its networking first.
const DWORD kRemoteDelayMs = 8000;

bool MenuOpen() {
#if defined(SKYPORTAL_MENU_DRAWN)
  return g_drawn.pad >= 0 && overlay::IsOpen();
#else
  return false;
#endif
}

// A figure's picture as a path under the data folder ("Images/Cards 1/Spyro.qoi"), or "".
void RemotePicture(rh::Json& j, int id) {
  char image[MAX_PATH];
  const int root_len = (int)strlen(g_root);
  if (g_library.ImagePath(id, image, sizeof(image)) && (int)strlen(image) > root_len + 1) {
    j.PathStr(image + root_len + 1);
  } else {
    j.Str("");
  }
}

// [id, "name", element, "series", game, "picture"]
void RemoteFigure(rh::Json& j, int id) {
  char series[40];
  g_library.Series(id, series, sizeof(series));
  j.Char('[');
  j.Int(id);
  j.Char(',');
  j.Str(g_library.Name(id));
  j.Char(',');
  j.Int(g_library.Element(id));
  j.Char(',');
  j.Str(series);
  j.Char(',');
  j.Int(g_library.Game(id));
  j.Char(',');
  RemotePicture(j, id);
  j.Char(']');
}

bool OnRift(int id) { return id >= 0 && (g_portal.FindTag(id) >= 0 || id == g_swap.id); }

// What's on the rift, the running game and the lists the page can show.
int RemoteState(char* out, int cap) {
  rh::Json j(out, cap);
  j.Raw("{\"product\":");
  j.Str(SKYPORTAL_PRODUCT);
  j.Raw(",\"game\":");
  j.Int(g_game);
  j.Raw(",\"gameName\":");
  j.Str(g_game ? menu::GameName(g_game) : "Skylanders");
  j.Raw(",\"menu\":");
  j.Bool(MenuOpen());
  j.Raw(",\"slots\":");
  j.Int(sp::kMaxSlots);
  j.Raw(",\"rev\":");
  j.Int((int)remote::Revision());
  j.Raw(",\"nrev\":");  // nicknames and lists (GET /api/meta when it changes)
  j.Int((int)g_meta_rev);
  j.Raw(",\"rift\":[");
  int n = 0;
  for (int slot = 0; slot < sp::kMaxSlots; ++slot) {
    const int id = g_portal.Tag(slot);
    if (id < 0) continue;
    if (n++) j.Char(',');
    RemoteFigure(j, id);
  }
  if (g_swap.id >= 0 && g_portal.FindTag(g_swap.id) < 0) {  // going on in a moment
    if (n++) j.Char(',');
    RemoteFigure(j, g_swap.id);
  }
  // The lists: everything, Favorites and your own lists (empty ones too: you add to them here),
  // My Picks, each game's figures.
  j.Raw("],\"views\":[[0,\"All\"]");
  for (int l = 0; l < g_coll.ListCount(); ++l) {
    j.Raw(",[");
    j.Int(menu::kViewList0 + l);
    j.Char(',');
    j.Str(g_coll.ListName(l));
    j.Char(']');
  }
  if (g_library.PickCount() > 0) j.Raw(",[1,\"My Picks\"]");
  for (int g = 1; g <= 6; ++g) {
    if (g_library.CountFromGame(g) <= 0) continue;
    j.Raw(",[");
    j.Int(menu::kViewGame1 + g - 1);
    j.Char(',');
    j.Str(menu::GameName(g));
    j.Char(']');
  }
  j.Raw("],\"elements\":[");
  for (int e = 0; e < elements::kElementCount; ++e) {
    if (e) j.Char(',');
    j.Str(elements::Name((elements::Element)e));
  }
  j.Raw("]}");
  return j.Overflow() ? -1 : j.Len();
}

// A list, built once per phone request for its first page and handed out a page at a time:
// entry ids in the menu's order, My Picks' roster of each, and its section (label pool).
struct RemoteList {
  int view;
  DWORD scan;     // g_menu.last_scan it was built from
  LONG meta;      // g_meta_rev it was built from (a list of your own changes with it)
  int count;
  short id[FigureLibrary::kMaxFigures];
  signed char roster[FigureLibrary::kMaxFigures];
  short section[FigureLibrary::kMaxFigures];
  enum { kPool = 8192, kSections = 600 };
  char pool[kPool];
  int pool_len;
  short offset[kSections];
  int sections;
};
RemoteList g_rlist;

void BuildRemoteList(int view) {
  const int had = menu::CurrentView();
  const int selected = SelectedId();
  const bool change = view != had || menu::ListOfView(view) >= 0;  // (a list may have changed)
  if (change) menu::ApplyView(view);  // the list the phone asked for, for a moment
  const int list = menu::ListOfView(view);
  g_rlist.count = 0;
  g_rlist.pool_len = 0;
  g_rlist.sections = 0;
  char last[160] = "";
  const int n = g_library.BrowseCount();
  for (int pos = 0; pos < n && g_rlist.count < FigureLibrary::kMaxFigures; ++pos) {
    const int id = g_library.BrowseAt(pos);
    char plain[96], label[160];
    menu::PlainSection(pos, id, plain, sizeof(plain));
    const int roster = g_library.PicksMode() ? g_library.RosterAt(pos) : 0;
    if (list >= 0) {
      strncpy_s(label, sizeof(label), g_coll.ListName(list), _TRUNCATE);  // one section: the list
    } else if (roster > 0) {
      sprintf_s(label, sizeof(label), "%s Roster / %s", menu::GameName(roster), plain);
    } else {
      strncpy_s(label, sizeof(label), plain, _TRUNCATE);
    }
    if (strcmp(label, last) || !g_rlist.sections) {
      const int len = (int)strlen(label) + 1;
      if (g_rlist.sections < RemoteList::kSections && g_rlist.pool_len + len <= RemoteList::kPool) {
        memcpy(g_rlist.pool + g_rlist.pool_len, label, len);
        g_rlist.offset[g_rlist.sections++] = (short)g_rlist.pool_len;
        g_rlist.pool_len += len;
      }
      strncpy_s(last, sizeof(last), label, _TRUNCATE);
    }
    const int k = g_rlist.count++;
    g_rlist.id[k] = (short)id;
    g_rlist.roster[k] = (signed char)roster;
    g_rlist.section[k] = (short)(g_rlist.sections - 1);
  }
  if (change) {  // and back to the menu's list, on the same figure
    menu::ApplyView(had);
    if (g_library.BrowseAt(g_menu.pos) != selected) {
      const int p = g_library.BrowsePos(selected);
      if (p >= 0) g_menu.pos = p;
    }
  }
  g_rlist.view = view;
  g_rlist.scan = g_menu.last_scan;
  g_rlist.meta = g_meta_rev;
}

// {"v":0,"total":1150,"from":0,"next":180,"items":[[id,"name",element,"series",game,"picture",
//  roster,"section" (when it starts, and on a page's first figure) or 0,on the rift], ...]}
int RemoteListPage(int view, int from, char* out, int cap) {
  if (view < 0 || view >= menu::kViewCount) view = menu::kViewAll;
  if (from <= 0 || g_rlist.view != view || g_rlist.scan != g_menu.last_scan ||
      (menu::ListOfView(view) >= 0 && g_rlist.meta != g_meta_rev)) {
    BuildRemoteList(view);
    if (from < 0) from = 0;
  }
  rh::Json j(out, cap);
  j.Raw("{\"v\":");
  j.Int(view);
  j.Raw(",\"total\":");
  j.Int(g_rlist.count);
  j.Raw(",\"from\":");
  j.Int(from);
  j.Raw(",\"items\":[");
  int i = from;
  int last_section = -1;
  for (; i < g_rlist.count; ++i) {
    if (j.Room() < 700) break;
    const int id = g_rlist.id[i];
    char series[40];
    g_library.Series(id, series, sizeof(series));
    if (i > from) j.Char(',');
    j.Char('[');
    j.Int(id);
    j.Char(',');
    j.Str(g_library.Name(id));
    j.Char(',');
    j.Int(g_library.Element(id));
    j.Char(',');
    j.Str(series);
    j.Char(',');
    j.Int(g_library.Game(id));
    j.Char(',');
    RemotePicture(j, id);
    j.Char(',');
    j.Int(g_rlist.roster[i]);
    j.Char(',');
    const int sec = g_rlist.section[i];
    if (sec != last_section && sec >= 0) {
      j.Str(g_rlist.pool + g_rlist.offset[sec]);
    } else {
      j.Int(0);
    }
    last_section = sec;
    j.Char(',');
    j.Bool(OnRift(id));
    j.Char(']');
  }
  j.Raw("],\"next\":");
  j.Int(i < g_rlist.count ? i : -1);
  j.Char('}');
  return j.Overflow() ? -1 : j.Len();
}

// a=add|swap|remove|clear, i=entry id: the menu's A / Y / B / X for that figure.
int RemoteDo(const char* query, char* out, int cap) {
  char action[16] = "";
  rh::QueryValue(query, "a", action, sizeof(action));
  const int id = rh::QueryInt(query, "i", -1);
  const bool known = id >= 0 && id < FigureLibrary::kMaxFigures && g_library.Rel(id)[0];
  const bool toast = !MenuOpen();  // on the TV when the menu isn't up
  const char* name = known ? g_library.Name(id) : "";
  bool ok = false;
  char msg[160] = "";
  if (!strcmp(action, "clear")) {
    menu::DoOn(-1, menu::kClear, toast);
    ok = true;
    strncpy_s(msg, sizeof(msg), "The rift is clear", _TRUNCATE);
  } else if (!known) {
    strncpy_s(msg, sizeof(msg), "That figure isn't in the list any more", _TRUNCATE);
  } else if (!strcmp(action, "add")) {
    if (OnRift(id)) {
      sprintf_s(msg, sizeof(msg), "%s is already on the rift", name);
    } else if (g_portal.FirstFreeSlot() < 0) {
      strncpy_s(msg, sizeof(msg), "The rift is full (16 figures)", _TRUNCATE);
    } else {
      menu::DoOn(id, menu::kAdd, toast);
      ok = OnRift(id);
      sprintf_s(msg, sizeof(msg), ok ? "%s is on the rift" : "Couldn't read %s's file", name);
    }
  } else if (!strcmp(action, "swap")) {
    if (OnRift(id)) {
      sprintf_s(msg, sizeof(msg), "%s is already on the rift", name);
    } else {
      menu::DoOn(id, menu::kSwap, toast);
      ok = OnRift(id);
      sprintf_s(msg, sizeof(msg), g_portal.FindTag(id) >= 0 ? "%s is on the rift" : "Swapping in %s", name);
    }
  } else if (!strcmp(action, "remove")) {
    if (OnRift(id)) {
      menu::DoOn(id, menu::kRemove, toast);
      ok = true;
      sprintf_s(msg, sizeof(msg), "%s is off the rift", name);
    } else {
      sprintf_s(msg, sizeof(msg), "%s isn't on the rift", name);
    }
  } else {
    strncpy_s(msg, sizeof(msg), "Unknown action", _TRUNCATE);
  }
  if (ok) {
    util::Log("Phone remote: %s %s", action, known ? g_library.Rel(id) : "");
    MenuChanged();
  }
  rh::Json j(out, cap);
  j.Raw("{\"ok\":");
  j.Bool(ok);
  j.Raw(",\"msg\":");
  j.Str(msg);
  j.Char('}');
  return j.Len();
}

// Nicknames and lists: {"nrev":3,"lists":[[8,"Favorites",[12,40]],[9,"Speedrun",[]]],
// "nicks":[[12,"Choppy"],...],"more":false}  (every figure in a list, whether this game can use
// it or not; "more": nicknames left out for room)
int RemoteMeta(char* out, int cap) {
  rh::Json j(out, cap);
  j.Raw("{\"nrev\":");
  j.Int((int)g_meta_rev);
  j.Raw(",\"lists\":[");
  for (int l = 0; l < g_coll.ListCount(); ++l) {
    if (l) j.Char(',');
    j.Char('[');
    j.Int(menu::kViewList0 + l);
    j.Char(',');
    j.Str(g_coll.ListName(l));
    j.Raw(",[");
    for (int k = 0; k < g_coll.ListSize(l); ++k) {
      if (k) j.Char(',');
      j.Int(g_coll.ListIds(l)[k]);
    }
    j.Raw("]]");
  }
  j.Raw("],\"nicks\":[");
  bool more = false;
  for (int i = 0; i < g_coll.NickCount(); ++i) {
    if (j.Room() < 96) {
      more = true;
      break;
    }
    if (i) j.Char(',');
    j.Char('[');
    j.Int(g_coll.NickId(i));
    j.Char(',');
    j.Str(g_coll.NickText(i));
    j.Char(']');
  }
  j.Raw("],\"more\":");
  j.Bool(more);
  j.Char('}');
  return j.Overflow() ? -1 : j.Len();
}

int RemoteAnswer(char* out, int cap, bool ok, const char* msg, int view, const char* nick) {
  rh::Json j(out, cap);
  j.Raw("{\"ok\":");
  j.Bool(ok);
  j.Raw(",\"msg\":");
  j.Str(msg);
  if (view >= 0) {
    j.Raw(",\"view\":");
    j.Int(view);
  }
  if (nick) {
    j.Raw(",\"nick\":");
    j.Str(nick);
  }
  j.Char('}');
  return j.Len();
}

bool KnownFigure(int id) { return id >= 0 && id < FigureLibrary::kMaxFigures && g_library.Rel(id)[0]; }

// i=entry id, n=nickname ("" takes it away).
int RemoteNick(const char* query, char* out, int cap) {
  const int id = rh::QueryInt(query, "i", -1);
  char text[96] = "";
  rh::QueryValue(query, "n", text, sizeof(text));
  if (!KnownFigure(id)) return RemoteAnswer(out, cap, false, "That figure isn't in the list any more", -1, NULL);
  char msg[160];
  if (g_coll.SetNick(id, text)) {
    const char* nick = g_coll.Nick(id);
    if (nick[0]) {
      util::Log("Phone remote: nickname %s is \"%s\"", g_library.Rel(id), nick);
      sprintf_s(msg, sizeof(msg), "%s is now \"%s\"", g_library.Name(id), nick);
    } else {
      util::Log("Phone remote: nickname of %s taken away", g_library.Rel(id));
      sprintf_s(msg, sizeof(msg), "%s has no nickname now", g_library.Name(id));
    }
    MetaChanged(true, -1, false);
    MenuChanged();
  } else if (g_coll.Nick(id)[0] || !text[0]) {
    sprintf_s(msg, sizeof(msg), g_coll.Nick(id)[0] ? "%s is already \"%s\"" : "%s has no nickname",
              g_library.Name(id), g_coll.Nick(id));
  } else {
    return RemoteAnswer(out, cap, false, "Too many nicknames (400)", -1, NULL);
  }
  return RemoteAnswer(out, cap, true, msg, -1, g_coll.Nick(id));
}

// a=add|remove (i=entry id) | new (n=name, and i= to put a figure in it) | rename (n=) | delete;
// l=the list's view number (8 = Favorites).
int RemoteLists(const char* query, char* out, int cap) {
  char action[16] = "", name[96] = "";
  rh::QueryValue(query, "a", action, sizeof(action));
  rh::QueryValue(query, "n", name, sizeof(name));
  const int id = rh::QueryInt(query, "i", -1);
  const int view = rh::QueryInt(query, "l", -1);
  const int list = menu::ListOfView(view);
  char msg[160];
  if (!strcmp(action, "new")) {
    const int l = g_coll.NewList(name);
    if (l == coll::Store::kNoName) return RemoteAnswer(out, cap, false, "Give the list a name", -1, NULL);
    if (l == coll::Store::kNameTaken) return RemoteAnswer(out, cap, false, "There's a list with that name already", -1, NULL);
    if (l < 0) return RemoteAnswer(out, cap, false, "That's as many lists as there can be (12)", -1, NULL);
    if (KnownFigure(id)) g_coll.Add(l, id);
    util::Log("Phone remote: new list \"%s\"", g_coll.ListName(l));
    if (KnownFigure(id)) sprintf_s(msg, sizeof(msg), "Made %s, with %s in it", g_coll.ListName(l), g_library.Name(id));
    else sprintf_s(msg, sizeof(msg), "Made the list %s", g_coll.ListName(l));
    MetaChanged(false, l, false);
    return RemoteAnswer(out, cap, true, msg, menu::kViewList0 + l, NULL);
  }
  if (list < 0) return RemoteAnswer(out, cap, false, "That list isn't there any more", -1, NULL);
  char lname[coll::kListNameMax + 1];
  strncpy_s(lname, sizeof(lname), g_coll.ListName(list), _TRUNCATE);
  if (!strcmp(action, "add") || !strcmp(action, "remove")) {
    if (!KnownFigure(id)) return RemoteAnswer(out, cap, false, "That figure isn't in the list any more", -1, NULL);
    const char* fig = g_library.Name(id);
    if (action[0] == 'a') {
      const int r = g_coll.Add(list, id);
      if (r < 0) {
        sprintf_s(msg, sizeof(msg), "%s is full (%d figures)", lname, (int)coll::kMaxEntries);
        return RemoteAnswer(out, cap, false, msg, view, NULL);
      }
      sprintf_s(msg, sizeof(msg), r ? "%s added to %s" : "%s is in %s already", fig, lname);
      if (r) {
        util::Log("Phone remote: %s added to %s", g_library.Rel(id), lname);
        MetaChanged(false, list, false);
      }
    } else {
      const bool was = g_coll.Remove(list, id);
      sprintf_s(msg, sizeof(msg), was ? "%s taken out of %s" : "%s isn't in %s", fig, lname);
      if (was) {
        util::Log("Phone remote: %s taken out of %s", g_library.Rel(id), lname);
        MetaChanged(false, list, false);
      }
    }
    return RemoteAnswer(out, cap, true, msg, view, NULL);
  }
  if (!strcmp(action, "rename")) {
    const int r = g_coll.Rename(list, name);
    if (r == coll::Store::kNoList) return RemoteAnswer(out, cap, false, "Favorites keeps its name", view, NULL);
    if (r == coll::Store::kNoName) return RemoteAnswer(out, cap, false, "Give the list a name", view, NULL);
    if (r == coll::Store::kNameTaken) return RemoteAnswer(out, cap, false, "There's a list with that name already", view, NULL);
    util::Log("Phone remote: list \"%s\" renamed \"%s\"", lname, g_coll.ListName(list));
    sprintf_s(msg, sizeof(msg), "%s is now called %s", lname, g_coll.ListName(list));
    MetaChanged(false, list, false);
    return RemoteAnswer(out, cap, true, msg, view, NULL);
  }
  if (!strcmp(action, "delete")) {
    if (!g_coll.Delete(list)) return RemoteAnswer(out, cap, false, "Favorites can't be deleted", view, NULL);
    util::Log("Phone remote: list \"%s\" deleted", lname);
    sprintf_s(msg, sizeof(msg), "Deleted the list %s", lname);
    MetaChanged(false, list, true);
    return RemoteAnswer(out, cap, true, msg, -1, NULL);
  }
  return RemoteAnswer(out, cap, false, "Unknown action", -1, NULL);
}

int RemoteApi(const char* path, const char* query, char* out, int cap) {
  if (!strcmp(path, "/api/state")) return RemoteState(out, cap);
  if (!strcmp(path, "/api/meta")) return RemoteMeta(out, cap);
  if (!strcmp(path, "/api/nick")) return RemoteNick(query, out, cap);
  if (!strcmp(path, "/api/lists")) return RemoteLists(query, out, cap);
  if (!strcmp(path, "/api/list")) {
    return RemoteListPage(rh::QueryInt(query, "v", 0), rh::QueryInt(query, "from", 0), out, cap);
  }
  if (!strcmp(path, "/api/do")) return RemoteDo(query, out, cap);
  return -1;
}

// Worker loop: answers the phone, and says where to find it once it's listening (once a game).
bool g_remote_told = false;
void ServiceRemote() {
  remote::Service(RemoteApi);
  if (!g_remote_told && remote::GetState() == remote::kListening) {
    g_remote_told = true;
    Toast("Phone remote: open http://%s", remote::Address());
    MenuChanged();  // the address shows in the menu too
  }
}
#endif  // SKYPORTAL_REMOTE

// The worker's pause between rounds: up to `ms`, but it wakes within 2 ms when the phone has asked
// for something or a Swap in is waiting for its moment, so taps from the phone go through at once.
void Nap(DWORD ms) {
#if defined(SKYPORTAL_REMOTE)
  const bool phone = remote::GetState() == remote::kListening;
#else
  const bool phone = false;
#endif
  if (!phone && g_swap.id < 0) {
    Sleep(ms);
    return;
  }
  const DWORD start = GetTickCount();
  do {
    Sleep(2);
#if defined(SKYPORTAL_REMOTE)
    if (remote::Waiting()) return;
#endif
    if (g_swap.id >= 0) return;  // (it checks the game's polls of the portal every round)
  } while (GetTickCount() - start < ms);
}

#if defined(SKYPORTAL_XRT)
extern "C" LONG KeSetBasePriorityThread(DWORD thread, LONG increment);
#endif

// Once a game runs, the worker gets a little ahead of the game's ordinary threads (it sleeps
// nearly all the time; when it has a figure to put on, that shouldn't wait for the game to yield).
// On the Dashlaunch build it is a system thread, which otherwise queues behind the game's.
void BoostWorker() {
  static bool done = false;
  if (done) return;
  done = true;
#if defined(SKYPORTAL_XRT)
  DWORD kthread = 0;
  __asm__ volatile("lwz %0, 0x100(13)" : "=r"(kthread));  // KeGetCurrentThread(): PCR->CurrentThread
  if (kthread) KeSetBasePriorityThread(kthread, 1);
#else
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
}

#if defined(SKYPORTAL_SIDELOAD)

// ---- Peer Pressure: title DLL living inside the Skylanders game ----

DWORD WINAPI SideloadThread(LPVOID) {
  util::Trace("worker thread running");
  const bool started = Startup();
  util::Trace("startup %s, root %s", started ? "done" : "says disabled", g_root);
  if (!started) {
    hooks::SetPassthrough(true);  // a real portal (if any) works as normal
    return 0;
  }
  hooks::LogExports();
  hooks::LogTitleImports(util::TitleModule());
  const DWORD title = g_have_exec_id ? g_exec_id.title_id : XamGetCurrentTitleId();
  util::Log("Hooked %d import(s) before the game started; %u read(s), %u write(s) by the game "
            "so far", g_dllmain_patched, hooks::ReadCalls(), hooks::WriteCalls());
  if (!TitleAllowed(title)) {
    util::Log("Note: title %08X isn't one of the built-in Skylanders title IDs; the sideload .ini "
              "chose to load The Static Rift into it anyway", title);
  }
  if (g_dllmain_patched == 0) {
    // Nothing to patch when DllMain ran (unexpected: imports are bound when the xex loads).
    // Try again for a few seconds. This rewrites import stubs while the game is running, which
    // is why it is only a fallback.
    util::Log("WARNING: no portal imports found in the game at load time, retrying");
    for (int i = 0; i < 40 && !g_stop && g_dllmain_patched == 0; ++i) {
      g_dllmain_patched = hooks::PatchTitle(util::TitleModule(), true);
      if (!g_dllmain_patched) Sleep(250);
    }
    util::Log("Retry patched %d import(s)", g_dllmain_patched);
  }
#if defined(SKYPORTAL_MENU_DRAWN)
  overlay::SetAllowPause(g_set.pause_low_memory);
  overlay::SetAlwaysPause(g_set.pause_game);
  overlay::Start(g_root, g_set.overlay_swap_rb);
#endif
  Toast("The Static Rift is open - %d on the rift, %d figures", g_portal.Count(),
        g_library.BrowseCount());
  if (g_set.remote) {
    remote::Start(g_root, g_set.remote_port, kRemoteDelayMs);
  } else {
    util::Log("Phone remote: off ([Remote] Enabled = 0)");
  }

  DWORD last_flush = 0;
  DWORD last_report = GetTickCount();
  DWORD reported_reads = 0, reported_writes = 0, reported_lookups = 0;
  int reports = 0;
  BoostWorker();
  while (!g_stop) {
    const DWORD now = GetTickCount();
    ServiceRemote();  // (first: a tap from the phone is what woke it)
    PollController(true);
    ServiceSwap();
    ServiceStateFile();
    if (now - last_flush >= kFlushIntervalMs) {
      last_flush = now;
      FlushWrites();
    }
    // How the game is using the portal: every 5 s at first, then every minute (at most 40 lines),
    // only while something changed.
    if (now - last_report >= (reports < 3 ? 5000u : 60000u) && reports < 40) {
      last_report = now;
      const DWORD reads = hooks::ReadCalls(), writes = hooks::WriteCalls();
      const DWORD lookups = hooks::ProcLookups();
      if (reports == 0 || reads != reported_reads || writes != reported_writes ||
          lookups != reported_lookups) {
        ++reports;
        util::Log("Game portal use: %u read(s), %u write(s); %d figure(s) on the rift; phone "
                  "remote: %u request(s)", reads, writes, g_portal.Count(), remote::Requests());
        hooks::LogLookups();
#if defined(SKYPORTAL_MENU_DRAWN)
        overlay::LogStatus();
#endif
        reported_reads = reads;
        reported_writes = writes;
        reported_lookups = lookups;
      }
    }
    Nap(20);
  }
  WritePortalState();
  return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HANDLE module, DWORD reason, LPVOID reserved) {
  (void)module;
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    // The game xex is loaded but hasn't started: point its imports at us now, before any game
    // code runs, so no import stub is ever rewritten while the game might be executing it.
    // Everything is patched here, including XamInputGetState: settings aren't read yet, but the
    // controller filter starts disabled (modifier 0 = plain passthrough) and LoadSettings turns
    // it on, and Enabled=0 switches the portal hooks to passthrough.
    util::Trace(SKYPORTAL_PRODUCT ": DllMain attach (module %p)", module);
    hooks::Init(&g_bridge);
    util::Trace("XAM resolved");
    PLDR_DATA_TABLE_ENTRY game = util::TitleModule();
    g_have_exec_id = util::ReadExecutionId(game, &g_exec_id);
    util::Trace("game module %p, title %08X media %08X version %08X", game, g_exec_id.title_id,
                g_exec_id.media_id, g_exec_id.version);
    g_dllmain_patched = hooks::PatchTitle(game, true);
#if defined(SKYPORTAL_MENU_DRAWN)
    overlay::Init();
    const int vdswap = overlay::PatchTitle(game);
    util::Trace("display hooks: %d (VdSwap, graphics interrupt handler)", vdswap);
    g_dllmain_patched += vdswap;
#endif
    util::Trace("patched %d import(s)", g_dllmain_patched);
    DWORD thread_id = 0;
    HANDLE thread = CreateThread(NULL, 0x10000, SideloadThread, NULL, 0, &thread_id);
    util::Trace("worker thread %s", thread ? "created" : "NOT created");
    if (thread) CloseHandle(thread);
  } else if (reason == DLL_PROCESS_DETACH) {
    // The game is exiting. Saves already happen within a second of the game writing a figure;
    // no file I/O here, since the worker thread may already be gone mid-save.
    g_stop = true;
#if defined(SKYPORTAL_MENU_DRAWN)
    overlay::Stop();
#endif
  }
  return TRUE;
}

#else  // Dashlaunch plugin

// ---- Dashlaunch: system DLL watching for Skylanders titles ----
//
// Loaded once at boot (RGH / JTAG, or BadUpdate + XeUnshackle) and never unloaded. A system
// thread checks every 10 ms which program is running. When a Skylanders game starts, its imports
// (the portal calls, the controller, VdSwap and the graphics interrupt callback) are pointed at
// the plugin straight away, while the game is still loading: the same hooks Peer Pressure's
// build installs before the game's first instruction. Then the figures this game can use are
// scanned, the remembered figures go back on the rift, and the menu works as in the Peer Pressure
// build. When the game exits, unsaved progress is saved and everything is reset for the next one.

// Resets the controller menu for the next game (it may have exited with the menu up).
void ResetMenuState() {
  g_menu.active_pad = -1;
  g_menu.toast_at = 0;
  g_menu.tap = false;
#if defined(SKYPORTAL_MENU_DRAWN)
  Drawn fresh = {-1, 0, false, false, {false, false, false, false}, false, -1};
  g_drawn = fresh;
#endif
#if defined(SKYPORTAL_MENU_DRAWN) || defined(SKYPORTAL_MENU_POP)
  popmenu::Forget();  // a pop-up box left open went with the game
#endif
  hooks::SetInputBlocked(false);
}

// A Skylanders game has just been hooked: its figures, the remembered rift, the menu.
void StartGame(DWORD title, PLDR_DATA_TABLE_ENTRY module, int patched) {
  const int game = GameNumber(title);
  util::Log("---- %s (title %08X): hooked %d import(s) while it loads ----",
            game ? menu::GameName(game) : "Game", title, patched);
#if !defined(SKYPORTAL_SIDELOAD)
  hooks::SetActive(true);
  if (!hooks::TitleReachesHooks()) {
    // Another plugin got to the game's XexGetProcedureAddress first: its portal lookups would
    // miss the hooks. Done before the scan below, well before the game looks the portal up.
    util::Log("The game's portal lookups don't pass through the Static Rift (another plugin's "
              "hooks?): pointing xam's exports at it instead");
    hooks::RedirectXamExports(true);
  }
#endif
  LoadSettings();  // staticrift.ini changes apply at the next game, no restart needed
  hooks::SetPassthrough(!g_set.enabled);
  ScanForTitle(title);
  g_menu.pos = 0;
  g_menu.last_scan = GetTickCount();
  RestoreStartupFigures();
  util::Log("%d figure(s) on the rift at start", g_portal.Count());
  hooks::LogTitleImports(module);
#if defined(SKYPORTAL_MENU_DRAWN)
  overlay::SetAllowPause(g_set.pause_low_memory);
  overlay::SetAlwaysPause(g_set.pause_game);
#endif
  Toast("The Static Rift is open - %d on the rift, %d figures", g_portal.Count(),
        g_library.BrowseCount());
#if defined(SKYPORTAL_REMOTE)
  // The phone remote, with this game (its server is one of the game's threads, gone with it).
  g_remote_told = false;
  if (g_set.remote) {
    remote::Start(g_root, g_set.remote_port, kRemoteDelayMs);
  } else {
    util::Log("Phone remote: off ([Remote] Enabled = 0)");
  }
#endif
  BoostWorker();  // (after the figure scan: that ran at the ordinary priority)
}

DWORD WINAPI WorkerThread(LPVOID) {
  util::Trace("worker thread running (system thread)");
  const bool started = Startup();
  util::Trace("startup %s; log at %s\\staticrift.log", started ? "done" : "says disabled", g_root);
  if (!started) {
    hooks::SetPassthrough(true);
    return 0;
  }
  hooks::LogExports();
  hooks::SetActive(false);  // until a Skylanders game is hooked
#if defined(SKYPORTAL_MENU_DRAWN)
  overlay::Start(g_root, g_set.overlay_swap_rb);  // idle until a game presents frames
#endif

  DWORD last_title = 0xFFFFFFFF;
  PLDR_DATA_TABLE_ENTRY last_module = NULL;
  bool hooked = false;
  DWORD hooked_title = 0;
  DWORD last_repatch = 0;
  DWORD last_flush = 0;
  DWORD last_report = 0;
  DWORD reported_reads = 0, reported_writes = 0;
  int reports = 0;

  while (!g_stop) {
    const DWORD now = GetTickCount();
    const DWORD title = XamGetCurrentTitleId();
    PLDR_DATA_TABLE_ENTRY module = util::TitleModule();

    if (title != last_title || module != last_module) {
      if (hooked) {
        // The game's threads (which call the portal hooks and log) are gone: first make their
        // locks free again, in case one ended while holding one.
        g_portal.RecoverLock();
        g_bridge.RecoverLock();
        util::LogRecoverLock();
        FlushWrites();
        WritePortalState();
#if defined(SKYPORTAL_MENU_DRAWN)
        if (overlay::TitleEnded()) util::Log("The game exited with the menu open");
        popmenu::Poll();
#endif
#if defined(SKYPORTAL_REMOTE)
        remote::TitleEnded();
#endif
        util::Log("Title %08X ended (%u read(s), %u write(s) by the game)", hooked_title,
                  hooks::ReadCalls(), hooks::WriteCalls());
        hooks::SetActive(false);
        hooks::RedirectXamExports(false);
      }
      ResetMenuState();
      CancelSwap();
      g_portal.ResetSession();
      g_bridge.Reset();
      hooked = false;
      last_title = title;
      last_module = module;
      if (TitleAllowed(title)) util::Log("Title now %08X (Skylanders)", title);
    }

    if (!hooked && TitleAllowed(title) && module) {
      // As early as possible: before the game sets up Direct3D and the portal.
      hooks::ResetTitle();
      int patched = hooks::PatchTitle(module, g_set.hide_combo);
#if defined(SKYPORTAL_MENU_DRAWN)
      patched += overlay::PatchTitle(module);
#endif
      if (patched > 0) {
        hooked = true;
        hooked_title = title;
        last_repatch = now;
        reports = 0;
        last_report = now;
        reported_reads = reported_writes = 0;
        StartGame(title, module, patched);
      }
    } else if (hooked && now - last_repatch >= 250) {
      // Idempotent: catches anything the game binds later.
      last_repatch = now;
      int more = hooks::PatchTitle(module, g_set.hide_combo);
#if defined(SKYPORTAL_MENU_DRAWN)
      more += overlay::PatchTitle(module);
#endif
      if (more > 0) util::Log("Hooked %d more import(s)", more);
    }

#if defined(SKYPORTAL_REMOTE)
    if (hooked) ServiceRemote();  // (first: a tap from the phone is what woke it)
#endif
    PollController(hooked);
    ServiceSwap();
    ServiceStateFile();

    if (now - last_flush >= kFlushIntervalMs) {
      last_flush = now;
      FlushWrites();
    }
    if (hooked && now - last_report >= (reports < 3 ? 5000u : 60000u) && reports < 40) {
      last_report = now;
      const DWORD reads = hooks::ReadCalls(), writes = hooks::WriteCalls();
      if (reports == 0 || reads != reported_reads || writes != reported_writes) {
        ++reports;
        util::Log("Game portal use: %u read(s), %u write(s); %d figure(s) on the rift; phone "
                  "remote: %u request(s)", reads, writes, g_portal.Count(), remote::Requests());
        hooks::LogLookups();
#if defined(SKYPORTAL_MENU_DRAWN)
        overlay::LogStatus();
#endif
        reported_reads = reads;
        reported_writes = writes;
      }
    }
    // Quick checks until a game is hooked (to catch it while it loads); a game's menu polls at
    // the same 20 ms as the Peer Pressure build.
    Nap(hooked ? 20 : 10);
  }
  FlushWrites();
  WritePortalState();
  return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HANDLE module, DWORD reason, LPVOID reserved) {
  (void)module;
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    util::Trace(SKYPORTAL_PRODUCT ": Dashlaunch plugin attach (module %p), running program %08X",
                module, XamGetCurrentTitleId());
    hooks::Init(&g_bridge);
#if defined(SKYPORTAL_MENU_DRAWN)
    overlay::Init();
#endif
    const bool started = util::StartThread(WorkerThread, 0x10000);  // a system thread
    util::Trace("worker thread %s", started ? "created" : "NOT created");
  } else if (reason == DLL_PROCESS_DETACH) {
    g_stop = true;
#if defined(SKYPORTAL_MENU_DRAWN)
    overlay::Stop();
#endif
  }
  return TRUE;
}

#endif  // SKYPORTAL_SIDELOAD
