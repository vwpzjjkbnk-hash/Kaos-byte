// SkyPortal360 - skydrawn: the full-screen figure menu.
//
// The game's VdSwap import (the kernel call Direct3D makes to present each frame) is pointed at a
// small stub (xrt/entry.S, sp_vdswap_hook). The stub notes the game's frame (front buffer address,
// size, tiling, format) and jumps to the real VdSwap. While the menu is open it also swaps in the
// plugin's own frame: VdSwap is handed a copy of the game's texture fetch constant and front
// buffer address that point at a menu frame the plugin drew, so the display shows the menu
// instead of the game's picture. The game keeps running and presenting as usual; nothing is drawn
// into the game's buffers, so the menu can't flicker or be overwritten. Two menu frames are used
// when there is memory for them (one shown, one being drawn), so changes appear in one step.
//
// The frames, the background and the pictures are allocated when the menu opens and freed a few
// frames after it closes.
#pragma once

#include "stdafx.h"
#include "menu_api.h"

namespace overlay {

void Init();                                    // DllMain: knows the real VdSwap
int PatchTitle(PLDR_DATA_TABLE_ENTRY title);    // points the game's VdSwap import at the stub
// Worker startup: starts the drawing thread. root: "SKYHDD:\\SkyPortal" (UI\ and Images\ live
// there). swap_rb: swap red and blue when writing pixels (OverlaySwapRedBlue).
void Start(const char* root, bool swap_rb);

void Open(const menu::View& v);   // shows the menu (if a frame has been presented)
void Update(const menu::View& v); // new content while open
void Close();
bool IsOpen();
// May the menu pause the game and draw into its own front buffers when there's no memory for
// frames of its own ([Menu] PauseGameWhenLowOnMemory)?
void SetAllowPause(bool allow);                    // open or opening (input goes to the menu)
bool Ready();                     // the game has presented frames (the menu can open)

// Dashlaunch build: the game this was drawing over has exited (and its threads with it, the menu's
// drawing and scan-out threads among them); forgets everything of that game's, ready for the
// next. Returns true if the menu was up.
bool TitleEnded();

void Stop();
void LogStatus();

}  // namespace overlay
