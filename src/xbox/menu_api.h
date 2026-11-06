// SkyPortal360 - what the figure menus (pop-up and drawn) need from the plugin. Implemented in
// main.cpp; called from the worker thread only.
#pragma once

namespace menu {

struct View {
  int pos;            // browse position, 0-based
  int count;          // figures that can be browsed
  int id;             // library id of the figure at `pos`, -1 when there are none
  const char* name;   // its display name ("" when none)
  int element;        // elements::Element
  int game;           // 1 Spyro's Adventure ... 6 Imaginators, 0 unknown
  char series[40];    // "Series 2", "Legendary", "Trap Master", ... or ""
  char group[96];     // the folder it is in, for the menu ("Giants / LightCores")
  char image[260];    // full path of its picture, "" when there is none
  bool on_portal;
  int portal_count;   // figures on the portal
  char portal_names[160];  // "Spyro, Bash" (truncated)
  bool picks_available;   // the game has "My Picks" (R3 switches lists)
  bool picks_mode;        // showing My Picks
  char next_list[24];     // what R3 shows next ("MY PICKS", "GIANTS", "ALL"), "" = nothing else
  char remote[32];        // the phone remote's address ("192.168.1.23"), "" while it's off
  char nickname[24];      // the nickname given to this figure ("" when none)
  // The nickname keyboard (START on a figure), while it's up:
  bool kb_open;
  char kb_text[24];       // what's typed so far
  int kb_row, kb_col;     // the key picked (keyboard.h)
  bool kb_caps;
  bool start_hint;        // START gives the figure a nickname (the drawn menu)
};

enum Action { kAdd, kSwap, kRemove, kClear };

int Pos();
void SetPos(int pos);                 // wraps around
void GetView(int pos, View* out);     // pos wraps around
void Do(Action action, bool toast);   // acts on the figure at Pos()

// Game names for display, 1-6 ("" otherwise).
const char* GameName(int game);

}  // namespace menu
