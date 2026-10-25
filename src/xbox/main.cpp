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
#include "util.h"
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

volatile bool g_stop = false;
char g_root[MAX_PATH] = {0};

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
  g_set.newest_only = g_config.GetBool("Menu", "NewestOnly", true);
  g_set.pause_low_memory = g_config.GetBool("Menu", "PauseGameWhenLowOnMemory", true);

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
            "rt=%d extraTitles=%d all=%d",
            g_set.enabled, g_set.notifications, g_set.save_writes, g_set.remember, g_set.speaker,
            g_set.hide_combo, g_set.modifier, g_set.need_lt, g_set.need_rt, g_extra_title_count,
            g_set.patch_all_titles);
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

void SavePortalState() {
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

// Places library entry `id` in the first free slot. Returns the slot or -1.
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
    GroupName(g_library.Rel(out->id), out->group, (int)sizeof(out->group));
    if (!g_library.ImagePath(out->id, out->image, (int)sizeof(out->image))) out->image[0] = 0;
  }
  out->on_portal = out->id >= 0 && g_portal.FindTag(out->id) >= 0;
  out->portal_count = PortalNames(out->portal_names, (int)sizeof(out->portal_names));
}

void Do(Action action, bool toast) {
  const int id = SelectedId();
  bool changed = false;
  switch (action) {
    case kAdd:
      changed = PlaceFigure(id, toast) >= 0;
      break;
    case kRemove:
      changed = RemoveFigure(id, toast);
      break;
    case kSwap:  // everything off, selected figure on
      FlushWrites();
      g_portal.RemoveAll();
      if (PlaceFigure(id, false) >= 0) {
        if (toast) Toast("Swapped in %s", g_library.Name(id));
        else util::Log("Swapped in %s", g_library.Name(id));
      }
      changed = true;
      break;
    case kClear:
      g_portal.RemoveAll();
      if (toast) Toast("All figures taken off the rift");
      else util::Log("All figures taken off the rift");
      changed = true;
      break;
  }
  if (changed) SavePortalState();
}

}  // namespace menu

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
  const int p = g_library.BrowsePos(sel);
  if (p >= 0) g_menu.pos = p;
  if (g_menu.pos >= g_library.BrowseCount()) g_menu.pos = 0;
}

void DrawnRefresh() {
  menu::View view;
  menu::GetView(g_menu.pos, &view);
  overlay::Update(view);
}

void DrawnClose() {
  overlay::Close();
  g_drawn.wait_release = true;
  g_drawn.wait_pad = g_drawn.pad;
  g_drawn.pad = -1;
  util::Log("Menu closed");
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
  if (g_drawn.pad < 0) {
    // Closed: a fresh press of the modifier on any pad opens the menu.
    for (int i = 0; i < 4; ++i) {
      const bool ok = XInputGetState(i, &st) == ERROR_SUCCESS;
      const bool held = ok && ModifierHeld(st.Gamepad);
      if (held && !g_drawn.mod_was_held[i] && !g_drawn.wait_release && g_drawn.pad < 0) {
        if (!overlay::Ready()) {
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
          util::Log("Menu opened (pad %d, %d figures)", i, g_library.BrowseCount());
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
        const int p = g_library.BrowsePos(sel);
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
const DWORD kFlushIntervalMs = 1000;
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

// Finds the SkyPortal folder, opens the log, reads settings and the figure library, and puts the
// remembered/startup figures on the portal. Returns false if the plugin is disabled.
bool Startup() {
  MountAndFindRoot();
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
  util::Log(SKYPORTAL_PRODUCT " (The Static Rift, Dashlaunch plugin) - root %s", g_root);
#endif

  LoadSettings();
  g_library.SetRoot(g_root);
  util::Log(g_library.LoadIndex() ? "Figure index loaded (UI\\figures.idx)"
                                  : "No UI\\figures.idx: names and elements come from the files");
  // The running game (1 = Spyro's Adventure ... 6 = Imaginators): figures only work in the game
  // they came out with and later ones, so the menu lists just those.
  int game = 0;
#if defined(SKYPORTAL_SIDELOAD)
  const DWORD title = g_have_exec_id ? g_exec_id.title_id : XamGetCurrentTitleId();
#else
  const DWORD title = XamGetCurrentTitleId();
#endif
  for (int i = 0; i < (int)(sizeof(kSkylandersTitles) / sizeof(kSkylandersTitles[0])); ++i) {
    if (kSkylandersTitles[i] == title) game = i + 1;
  }
  if (g_set.only_compatible) g_library.SetGameFilter(game);
  g_library.SetNewestOnly(g_set.newest_only);
  const int figures = g_library.Scan();
  g_menu.last_scan = GetTickCount();
  util::Log("%d figure file(s) in Figures: %d listed in %s, %d for later games ([Menu] "
            "OnlyCompatible = 0 lists them), %d older versions of a newer figure ([Menu] "
            "NewestOnly = 0 lists them)",
            figures + g_library.HiddenCount() + g_library.OlderCount(), figures,
            game ? menu::GameName(game) : "this game", g_library.HiddenCount(), g_library.OlderCount());
  if (g_library.UnreadableDirs() > 0) {
    util::Log("%d folder(s) in Figures are empty or couldn't be opened", g_library.UnreadableDirs());
  }

  if (!g_set.enabled) {
    util::Log("Disabled in staticrift.ini ([General] Enabled=0)");
    return false;
  }
  RestoreStartupFigures();
  util::Log("%d figure(s) on the rift at start", g_portal.Count());
  return true;
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
  overlay::Start(g_root, g_set.overlay_swap_rb);
#endif
  Toast("The Static Rift is open - %d on the rift, %d figures", g_portal.Count(),
        g_library.BrowseCount());

  DWORD last_flush = 0;
  DWORD last_report = GetTickCount();
  DWORD reported_reads = 0, reported_writes = 0, reported_lookups = 0;
  int reports = 0;
  while (!g_stop) {
    const DWORD now = GetTickCount();
    PollController(true);
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
        util::Log("Game portal use: %u read(s), %u write(s); %d figure(s) on the rift", reads,
                  writes, g_portal.Count());
        hooks::LogLookups();
#if defined(SKYPORTAL_MENU_DRAWN)
        overlay::LogStatus();
#endif
        reported_reads = reads;
        reported_writes = writes;
        reported_lookups = lookups;
      }
    }
    Sleep(20);
  }
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

DWORD WINAPI WorkerThread(LPVOID) {
  if (!Startup()) return 0;
  hooks::Init(&g_bridge);
  hooks::LogExports();

  DWORD last_title = 0;
  PLDR_DATA_TABLE_ENTRY last_module = NULL;
  bool hooked = false;
  DWORD last_title_check = 0;
  DWORD last_flush = 0;

  while (!g_stop) {
    const DWORD now = GetTickCount();

    if (now - last_title_check >= 250) {
      last_title_check = now;
      const DWORD title = XamGetCurrentTitleId();
      PLDR_DATA_TABLE_ENTRY module = util::TitleModule();
      if (title != last_title || module != last_module) {
        if (hooked) {
          FlushWrites();
          util::Log("Title %08X ended (reads=%u writes=%u)", last_title, hooks::ReadCalls(),
                    hooks::WriteCalls());
        }
        last_title = title;
        last_module = module;
        hooked = false;
        g_portal.ResetSession();
        g_bridge.Reset();
        util::Log("Title now %08X%s", title, TitleAllowed(title) ? " (Skylanders)" : "");
      }
      if (TitleAllowed(title) && module) {
        const int patched = hooks::PatchTitle(module, g_set.hide_combo);
        if (patched > 0) {
          util::Log("Hooked %d import(s) in title %08X", patched, title);
          if (!hooked) {
            Toast("The Static Rift is open - %d on the rift, %d figures", g_portal.Count(),
                  g_library.BrowseCount());
          }
          hooked = true;
        }
      }
    }

    PollController(hooked);

    if (now - last_flush >= kFlushIntervalMs) {
      last_flush = now;
      FlushWrites();
    }

    Sleep(20);
  }
  FlushWrites();
  return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HANDLE module, DWORD reason, LPVOID reserved) {
  (void)module;
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    HANDLE thread = NULL;
    DWORD thread_id = 0;
    ExCreateThread(&thread, 0x10000, &thread_id, (PVOID)XapiThreadStartup,
                   (LPTHREAD_START_ROUTINE)WorkerThread, NULL,
                   EX_CREATE_FLAG_SYSTEM | EX_CREATE_FLAG_SUSPENDED);
    if (thread) {
      XSetThreadProcessor(thread, 4);
      ResumeThread(thread);
      CloseHandle(thread);
    }
  } else if (reason == DLL_PROCESS_DETACH) {
    g_stop = true;
  }
  return TRUE;
}

#endif  // SKYPORTAL_SIDELOAD
