// SkyPortal360 - the XAM hooks patched into the running Skylanders title.
#pragma once

#include "stdafx.h"

namespace hooks {

// Resolves the real XAM functions. Call once before PatchTitle. Safe in DllMain (no file I/O).
void Init(sp::XamBridge* bridge);
// Writes the resolved XAM addresses to the log (call once the log is open).
void LogExports();

// Patches the title's imports of the raw non-controller API (and, if hide_combo, of
// XamInputGetState). Only the title's own import table is changed; XAM itself is untouched.
// Idempotent. Returns how many imports were newly patched (> 0 means a fresh title launch was
// just hooked). Rewrites the title's import stubs, so it's safest before the title runs
// (Peer Pressure: from DllMain); the Dashlaunch build calls it right after a title loads.
int PatchTitle(PLDR_DATA_TABLE_ENTRY title, bool hide_combo);

// Button combo hidden from the game while held: when (buttons & modifier) == modifier and
// the triggers named by need_lt/need_rt are pulled, `hide` buttons are cleared before the game
// sees the pad state. Set modifier 0 to disable.
void SetComboFilter(WORD modifier, bool need_lt, bool need_rt, WORD hide);
// true: the game sees an idle controller (the full-screen menu has the controls).
void SetInputBlocked(bool blocked);

// true: the portal hooks forward to the real XAM functions (virtual portal off, real portal on).
void SetPassthrough(bool passthrough);

// Diagnostics: number of raw reads/writes the game has made through the hooks.
DWORD ReadCalls();
DWORD WriteCalls();
DWORD ProcLookups();

// Diagnostics for the log: the XAM functions the title imports, and the exports it looked up at
// run time through XexGetProcedureAddress.
void LogTitleImports(PLDR_DATA_TABLE_ENTRY title);
void LogLookups();

}  // namespace hooks
