// SkyPortal360 - the XAM hooks. See hooks.h.
//
// Argument order of the raw non-controller API follows Xenia Canary's XAM implementation
// (src/xenia/kernel/xam/xam_input.cc), which runs the Xbox 360 Skylanders games:
//   XamInputNonControllerGetRaw  (0x4A1): (WORD* state, DWORD* length, BYTE* buffer)
//   XamInputNonControllerSetRaw  (0x4A2): (DWORD length, BYTE* buffer)
//   XamInputNonControllerGetRawEx(0x52A): (DWORD device, BYTE* buffer, DWORD* length, WORD* state)
//   XamInputNonControllerSetRawEx(0x52B): (DWORD device, BYTE* buffer, DWORD length)
// Device ids 5 and 6 are the portal; buffers are at most 0x20 bytes.
#include "hooks.h"

#include "util.h"

// How the console's code reaches the hooks, and how the hooks call the real XAM functions.
//  - XDK build: everything is MSVC code, so plain function pointers both ways.
//  - Open build (SKYPORTAL_XRT, clang): the game enters each hook through a trampoline in
//    xrt/entry.S (<hook>_guest) that keeps the console's registers whole, and calls into XAM go
//    through xrt_call_guest, which calls by the console's convention.
#if defined(SKYPORTAL_XRT)
extern "C" {
void sp_hook_get_raw_guest();
void sp_hook_set_raw_guest();
void sp_hook_get_raw_ex_guest();
void sp_hook_set_raw_ex_guest();
void sp_hook_get_state_guest();
void sp_hook_get_proc_guest();
DWORD xrt_call_guest(DWORD fn, DWORD a, DWORD b, DWORD c, DWORD d);
// The plugin's own XexGetProcedureAddress import slot (xrt/gen_imports.py): the loader writes
// the real export's address there.
extern const DWORD xrt_imp_XexGetProcedureAddress;
}
#define SP_HOOK_ADDRESS(name) ((DWORD)&name##_guest)
#define SP_CALL3(fn, type, a, b, c) xrt_call_guest((fn), (DWORD)(a), (DWORD)(b), (DWORD)(c), 0)
#define SP_CALL4(fn, type, a, b, c, d) \
  xrt_call_guest((fn), (DWORD)(a), (DWORD)(b), (DWORD)(c), (DWORD)(d))
#else
#define SP_HOOK_ADDRESS(name) ((DWORD)&name)
#define SP_CALL3(fn, type, a, b, c) ((type)(fn))((a), (b), (c))
#define SP_CALL4(fn, type, a, b, c, d) ((type)(fn))((a), (b), (c), (d))
#endif

namespace hooks {

namespace {

const DWORD kPortalBufferSize = 0x20;

sp::XamBridge* g_bridge = NULL;

typedef DWORD (*XamInputGetStateFn)(DWORD user_index, DWORD flags, PXINPUT_STATE state);
typedef DWORD (*GetRawFn)(WORD* state, DWORD* length, BYTE* buffer);
typedef DWORD (*SetRawFn)(DWORD length, BYTE* buffer);
typedef DWORD (*GetRawExFn)(DWORD device, BYTE* buffer, DWORD* length, WORD* state);
typedef DWORD (*SetRawExFn)(DWORD device, BYTE* buffer, DWORD length);
DWORD g_real_get_state = 0;
DWORD g_real_get_raw = 0;
DWORD g_real_set_raw = 0;
DWORD g_real_get_raw_ex = 0;
DWORD g_real_set_raw_ex = 0;
DWORD g_real_get_proc = 0;  // xboxkrnl XexGetProcedureAddress
HANDLE g_xam_handle = NULL;

// Run-time export lookups the game makes (XexGetProcedureAddress). Games that must run on
// dashboards without the portal API look its functions up instead of importing them.
volatile DWORD g_proc_calls = 0;
volatile DWORD g_proc_redirects = 0;
const int kLookupLog = 32;
DWORD g_lookups[kLookupLog];  // (module tag << 16) | ordinal, distinct, in call order
volatile LONG g_lookup_count = 0;

// When set, the portal hooks forward to the real XAM functions (a real portal can be used).
volatile bool g_passthrough = false;
// A Skylanders game is running. The Dashlaunch build clears it between games, so a hook reached
// some other way (xam's export table, below) hands other titles the real functions.
volatile bool g_active = true;

// What the current game reaches the hooks through (Dashlaunch build): its portal imports, its
// XexGetProcedureAddress import. When another plugin hooked that import first (RGH "stealth"
// plugins patch it as the game loads), the hook found there is what lookups chain to.
int g_title_portal = 0;
int g_title_proc = 0;
DWORD g_chain_get_proc = 0;

// xam's export table entries for the portal functions, while they point at the hooks.
struct ExportPatch {
  DWORD* slot;
  DWORD saved;
};
ExportPatch g_export_patches[4];
int g_export_patch_count = 0;

volatile WORD g_combo_modifier = 0;
volatile bool g_combo_need_lt = false;
volatile bool g_combo_need_rt = false;
volatile WORD g_combo_hide = 0;
volatile bool g_block_input = false;
const BYTE kTriggerPulled = 0x80;

volatile DWORD g_read_calls = 0;
volatile DWORD g_write_calls = 0;

bool IsPortalDevice(DWORD device) { return device == 5 || device == 6; }

DWORD GetRawCommon(DWORD device, BYTE* buffer, DWORD* length, WORD* state) {
  if (!IsPortalDevice(device) || !buffer || !length || !state) return sp::kXamInvalidParameter;
  if (*length == 0 || *length > kPortalBufferSize) return sp::kXamInvalidParameter;
  if (!g_bridge) return sp::kXamDeviceNotConnected;
  InterlockedIncrement((LONG*)&g_read_calls);
  sp::u32 bytes = 0;
  sp::u16 st = 0;
  const sp::u32 result = g_bridge->Read(buffer, *length, &bytes, &st);
  if (result == sp::kXamSuccess) {
    *length = bytes;
    *state = st;
  }
  return result;
}

DWORD SetRawCommon(DWORD device, const BYTE* buffer, DWORD length) {
  if (!IsPortalDevice(device) || !buffer || !length || length > kPortalBufferSize) {
    return sp::kXamInvalidParameter;
  }
  if (!g_bridge) return sp::kXamDeviceNotConnected;
  InterlockedIncrement((LONG*)&g_write_calls);
  return g_bridge->Write(buffer, length);
}

}  // namespace

// ---- The hook entry points the title's import stubs jump to (C linkage: entry.S names them) ----

extern "C" DWORD sp_hook_get_raw(WORD* state, DWORD* length, BYTE* buffer) {
  if ((g_passthrough || !g_active) && g_real_get_raw) {
    return SP_CALL3(g_real_get_raw, GetRawFn, state, length, buffer);
  }
  return GetRawCommon(5, buffer, length, state);
}

extern "C" DWORD sp_hook_set_raw(DWORD length, BYTE* buffer) {
  if ((g_passthrough || !g_active) && g_real_set_raw) {
#if defined(SKYPORTAL_XRT)
    return xrt_call_guest(g_real_set_raw, length, (DWORD)buffer, 0, 0);
#else
    return ((SetRawFn)g_real_set_raw)(length, buffer);
#endif
  }
  return SetRawCommon(5, buffer, length);
}

extern "C" DWORD sp_hook_get_raw_ex(DWORD device, BYTE* buffer, DWORD* length, WORD* state) {
  if ((g_passthrough || !g_active) && g_real_get_raw_ex) {
    return SP_CALL4(g_real_get_raw_ex, GetRawExFn, device, buffer, length, state);
  }
  return GetRawCommon(device, buffer, length, state);
}

extern "C" DWORD sp_hook_set_raw_ex(DWORD device, BYTE* buffer, DWORD length) {
  if ((g_passthrough || !g_active) && g_real_set_raw_ex) {
    return SP_CALL3(g_real_set_raw_ex, SetRawExFn, device, buffer, length);
  }
  return SetRawCommon(device, buffer, length);
}

// Lets the game see the controller as usual, minus the portal-menu buttons while the menu
// modifier is held, so browsing figures doesn't also move the character. While the full-screen
// menu is open the game sees an idle controller.
extern "C" DWORD sp_hook_get_state(DWORD user_index, DWORD flags, PXINPUT_STATE state) {
  const DWORD result = SP_CALL3(g_real_get_state, XamInputGetStateFn, user_index, flags, state);
  if (result == ERROR_SUCCESS && state && g_block_input) {
    memset(&state->Gamepad, 0, sizeof(state->Gamepad));
    return result;
  }
  const WORD modifier = g_combo_modifier;
  if (result == ERROR_SUCCESS && state && modifier) {
    XINPUT_GAMEPAD& pad = state->Gamepad;
    const bool held = (pad.wButtons & modifier) == modifier &&
                      (!g_combo_need_lt || pad.bLeftTrigger >= kTriggerPulled) &&
                      (!g_combo_need_rt || pad.bRightTrigger >= kTriggerPulled);
    if (held) pad.wButtons &= (WORD)~g_combo_hide;
  }
  return result;
}

namespace {

// The hook the game should get instead of `addr` (a real XAM export), or 0.
DWORD HookFor(DWORD addr) {
  if (!addr) return 0;
  if (addr == g_real_get_raw) return SP_HOOK_ADDRESS(sp_hook_get_raw);
  if (addr == g_real_set_raw) return SP_HOOK_ADDRESS(sp_hook_set_raw);
  if (addr == g_real_get_raw_ex) return SP_HOOK_ADDRESS(sp_hook_get_raw_ex);
  if (addr == g_real_set_raw_ex) return SP_HOOK_ADDRESS(sp_hook_set_raw_ex);
  if (addr == g_real_get_state) return SP_HOOK_ADDRESS(sp_hook_get_state);
  return 0;
}

void RecordLookup(HANDLE module, DWORD ordinal) {
  const DWORD tag = module == g_xam_handle ? 1 : 2;  // 1 = xam, 2 = anything else
  const DWORD entry = tag << 16 | (ordinal & 0xFFFF);
  const LONG n = g_lookup_count;
  for (LONG i = 0; i < n && i < kLookupLog; ++i) {
    if (g_lookups[i] == entry) return;
  }
  const LONG slot = InterlockedIncrement(&g_lookup_count) - 1;
  if (slot < kLookupLog) g_lookups[slot] = entry;
}

}  // namespace

// The game's XexGetProcedureAddress import: real lookup, then the portal functions are swapped
// for the hooks, exactly as their imports are.
extern "C" DWORD sp_hook_get_proc(HANDLE module, DWORD ordinal, DWORD* out) {
  const DWORD real = g_chain_get_proc ? g_chain_get_proc : g_real_get_proc;
#if defined(SKYPORTAL_XRT)
  const DWORD result = xrt_call_guest(real, (DWORD)module, ordinal, (DWORD)out, 0);
#else
  typedef DWORD (*GetProcFn)(HANDLE, DWORD, DWORD*);
  const DWORD result = ((GetProcFn)real)(module, ordinal, out);
#endif
  InterlockedIncrement((LONG*)&g_proc_calls);
  RecordLookup(module, ordinal);
  if (result == 0 && out) {
    const DWORD hook = HookFor(*out);
    if (hook) {
      *out = hook;
      InterlockedIncrement((LONG*)&g_proc_redirects);
    }
  }
  return result;
}

void Init(sp::XamBridge* bridge) {
  g_bridge = bridge;
#if defined(SKYPORTAL_XRT)
  g_real_get_proc = xrt_imp_XexGetProcedureAddress;
#else
  g_real_get_proc = util::ResolveExport("xboxkrnl.exe", 0x197);
#endif
  XexGetModuleHandle((PSZ) "xam.xex", &g_xam_handle);
  g_real_get_state = util::ResolveExport("xam.xex", XAM_ORD_XamInputGetState);
  g_real_get_raw = util::ResolveExport("xam.xex", XAM_ORD_XamInputNonControllerGetRaw);
  g_real_set_raw = util::ResolveExport("xam.xex", XAM_ORD_XamInputNonControllerSetRaw);
  g_real_get_raw_ex = util::ResolveExport("xam.xex", XAM_ORD_XamInputNonControllerGetRawEx);
  g_real_set_raw_ex = util::ResolveExport("xam.xex", XAM_ORD_XamInputNonControllerSetRawEx);
}

void LogExports() {
  util::Log("XAM exports: GetState=%08X GetRaw=%08X SetRaw=%08X GetRawEx=%08X SetRawEx=%08X",
            g_real_get_state, g_real_get_raw, g_real_set_raw, g_real_get_raw_ex,
            g_real_set_raw_ex);
}

namespace {

// Patches the title's imports of one function: those still holding the address this plugin was
// given, and those holding what the export resolves to now, if something has redirected it
// since (*now gets that address, 0 if the same).
int PatchFunction(PLDR_DATA_TABLE_ENTRY title, const char* module, DWORD ordinal, DWORD real,
                  DWORD hook, DWORD* now) {
  int n = util::PatchModuleImport(title, real, hook);
  *now = 0;
  const DWORD current = util::ResolveExport(module, ordinal);
  if (current && current != real && current != hook) {
    const int m = util::PatchModuleImport(title, current, hook);
    if (m) *now = current;
    n += m;
  }
  return n;
}

}  // namespace

int PatchTitle(PLDR_DATA_TABLE_ENTRY title, bool hide_combo) {
  if (!title) return 0;
  DWORD now = 0;
  int portal = 0;
  portal += PatchFunction(title, "xam.xex", XAM_ORD_XamInputNonControllerGetRaw, g_real_get_raw,
                          SP_HOOK_ADDRESS(sp_hook_get_raw), &now);
  portal += PatchFunction(title, "xam.xex", XAM_ORD_XamInputNonControllerSetRaw, g_real_set_raw,
                          SP_HOOK_ADDRESS(sp_hook_set_raw), &now);
  portal += PatchFunction(title, "xam.xex", XAM_ORD_XamInputNonControllerGetRawEx,
                          g_real_get_raw_ex, SP_HOOK_ADDRESS(sp_hook_get_raw_ex), &now);
  portal += PatchFunction(title, "xam.xex", XAM_ORD_XamInputNonControllerSetRawEx,
                          g_real_set_raw_ex, SP_HOOK_ADDRESS(sp_hook_set_raw_ex), &now);
  int n = portal;
  if (hide_combo && g_real_get_state) {
    n += util::PatchModuleImport(title, g_real_get_state, SP_HOOK_ADDRESS(sp_hook_get_state));
  }
  if (g_real_get_proc) {
    const int proc = PatchFunction(title, "xboxkrnl.exe", 0x197, g_real_get_proc,
                                   SP_HOOK_ADDRESS(sp_hook_get_proc), &now);
    if (now) g_chain_get_proc = now;  // keep whatever hooked it in the chain
    g_title_proc += proc;
    n += proc;
  }
  g_title_portal += portal;
  return n;
}

void ResetTitle() {
  g_title_portal = 0;
  g_title_proc = 0;
  g_chain_get_proc = 0;
}

bool TitleReachesHooks() { return g_title_portal > 0 || g_title_proc > 0; }

void SetActive(bool active) { g_active = active; }

namespace {

// xam's export table entry for `ordinal` (the HV export table its XEX security info points at:
// address = (ImageBaseAddress << 16) + entry), or NULL. *base gets ImageBaseAddress << 16.
DWORD* XamExportEntry(DWORD ordinal, DWORD* base) {
  const PLDR_DATA_TABLE_ENTRY xam = (PLDR_DATA_TABLE_ENTRY)g_xam_handle;
  if (!xam || !MmIsAddressValid(&xam->XexHeaderBase) || !xam->XexHeaderBase) return NULL;
  const BYTE* header = (const BYTE*)xam->XexHeaderBase;
  if (!MmIsAddressValid((PVOID)header) || *(const DWORD*)header != 0x58455832) return NULL;  // XEX2
  const BYTE* security = header + *(const DWORD*)(header + 0x10);
  if (!MmIsAddressValid((PVOID)(security + 0x160))) return NULL;
  DWORD* table = *(DWORD* const*)(security + 0x160);
  if (!table || !MmIsAddressValid(table) || table[0] != 0x48000000 || table[1] != 0x00485645) {
    return NULL;  // not the HV export table layout
  }
  const DWORD count = table[9], first = table[10];
  if (ordinal < first || ordinal - first >= count) return NULL;
  DWORD* entry = &table[11 + (ordinal - first)];
  if (!MmIsAddressValid(entry)) return NULL;
  *base = table[8] << 16;
  return entry;
}

}  // namespace

bool RedirectXamExports(bool on) {
  if (!on) {
    for (int i = 0; i < g_export_patch_count; ++i) {
      *g_export_patches[i].slot = g_export_patches[i].saved;
      util::FlushRange(g_export_patches[i].slot, 4);
    }
    if (g_export_patch_count) util::Log("xam's portal exports restored");
    g_export_patch_count = 0;
    return true;
  }
  if (g_export_patch_count) return true;
  const DWORD ordinals[4] = {XAM_ORD_XamInputNonControllerGetRaw,
                             XAM_ORD_XamInputNonControllerSetRaw,
                             XAM_ORD_XamInputNonControllerGetRawEx,
                             XAM_ORD_XamInputNonControllerSetRawEx};
  const DWORD reals[4] = {g_real_get_raw, g_real_set_raw, g_real_get_raw_ex, g_real_set_raw_ex};
  const DWORD hooks[4] = {SP_HOOK_ADDRESS(sp_hook_get_raw), SP_HOOK_ADDRESS(sp_hook_set_raw),
                          SP_HOOK_ADDRESS(sp_hook_get_raw_ex), SP_HOOK_ADDRESS(sp_hook_set_raw_ex)};
  DWORD* entries[4];
  DWORD bases[4];
  // Every entry first, each checked against the address it should give, before any is changed.
  for (int i = 0; i < 4; ++i) {
    entries[i] = NULL;
    if (!reals[i]) continue;
    entries[i] = XamExportEntry(ordinals[i], &bases[i]);
    if (!entries[i] || bases[i] + *entries[i] != reals[i]) {
      util::Log("xam's export table isn't as expected (ordinal %X): left alone", ordinals[i]);
      return false;
    }
  }
  for (int i = 0; i < 4; ++i) {
    if (!entries[i]) continue;
    g_export_patches[g_export_patch_count].slot = entries[i];
    g_export_patches[g_export_patch_count].saved = *entries[i];
    ++g_export_patch_count;
    *entries[i] = hooks[i] - bases[i];
    util::FlushRange(entries[i], 4);
  }
  // Lookups now give the hooks?
  for (int i = 0; i < 4; ++i) {
    if (reals[i] && util::ResolveExport("xam.xex", ordinals[i]) != hooks[i]) {
      util::Log("xam's export table: lookups still give the real functions; restored");
      RedirectXamExports(false);
      return false;
    }
  }
  util::Log("xam's portal exports now lead to the Static Rift (while this game runs)");
  return true;
}

void LogTitleImports(PLDR_DATA_TABLE_ENTRY title) {
  // Names the XAM and lookup functions the game imports, by ordinal: shows how it reaches the
  // portal on this console. Reverse-maps import slot values through xam's export table.
  static DWORD xam_exports[0x700];
  if (!g_xam_handle) return;
  for (DWORD ord = 1; ord < 0x700; ++ord) {
    DWORD addr = 0;
    xam_exports[ord] = XexGetProcedureAddress(g_xam_handle, ord, &addr) == 0 ? addr : 0;
  }
  char line[400];
  int len = 0;
  int count = 0;
  bool proc = false;
  static DWORD slots[2048];
  const int nslots = util::CollectImportSlots(title, slots, 2048);
  for (int i = 0; i < nslots; ++i) {
    const DWORD value = slots[i];
    if (value == SP_HOOK_ADDRESS(sp_hook_get_proc)) proc = true;
    DWORD ord = 0;
    if (value == SP_HOOK_ADDRESS(sp_hook_get_state)) ord = XAM_ORD_XamInputGetState;
    if (value == SP_HOOK_ADDRESS(sp_hook_get_raw)) ord = XAM_ORD_XamInputNonControllerGetRaw;
    if (value == SP_HOOK_ADDRESS(sp_hook_set_raw)) ord = XAM_ORD_XamInputNonControllerSetRaw;
    if (value == SP_HOOK_ADDRESS(sp_hook_get_raw_ex)) ord = XAM_ORD_XamInputNonControllerGetRawEx;
    if (value == SP_HOOK_ADDRESS(sp_hook_set_raw_ex)) ord = XAM_ORD_XamInputNonControllerSetRawEx;
    for (DWORD o = 1; !ord && o < 0x700; ++o) {
      if (xam_exports[o] == value) ord = o;
    }
    if (!ord) continue;
    ++count;
    if (len < (int)sizeof(line) - 8) {
      len += sprintf_s(line + len, sizeof(line) - len, " %X", ord);
    }
  }
  line[len] = 0;
  util::Log("Game imports %d XAM function(s), ordinals (hex):%s", count, len ? line : " none");
  util::Log("Game imports XexGetProcedureAddress: %s", proc ? "yes (hooked)" : "no");
  if (proc || TitleReachesHooks()) return;
  // Not reachable: show which imports something else has redirected (another plugin's hooks).
  HANDLE krnl = NULL;
  static DWORD krnl_exports[0x300];
  if (XexGetModuleHandle((PSZ) "xboxkrnl.exe", &krnl) == 0 && krnl) {
    for (DWORD ord = 1; ord < 0x300; ++ord) {
      DWORD addr = 0;
      krnl_exports[ord] = XexGetProcedureAddress(krnl, ord, &addr) == 0 ? addr : 0;
    }
  }
  len = 0;
  int foreign = 0;
  for (int i = 0; i < nslots; ++i) {
    const DWORD value = slots[i];
    if (value < 0x80000000u) continue;  // the first word of a call stub (lis r11, ...)
    bool known = false;
    for (DWORD o = 1; !known && o < 0x700; ++o) known = xam_exports[o] == value;
    for (DWORD o = 1; !known && o < 0x300; ++o) known = krnl_exports[o] == value;
    // (this plugin's own stubs - the portal's and the menu's - are all close to its hooks)
    const DWORD own = SP_HOOK_ADDRESS(sp_hook_get_raw);
    if (known || (value >= own - 0x400000 && value < own + 0x400000)) continue;
    ++foreign;
    if (len < (int)sizeof(line) - 12) len += sprintf_s(line + len, sizeof(line) - len, " %08X", value);
  }
  line[len] = 0;
  util::Log("XexGetProcedureAddress: the Static Rift's %08X, the kernel's now %08X; %d game "
            "import(s) lead somewhere else (another plugin's hooks?):%s",
            g_real_get_proc, util::ResolveExport("xboxkrnl.exe", 0x197), foreign,
            len ? line : " none");
}

void LogLookups() {
  const LONG n = g_lookup_count;
  if (!n) {
    util::Log("Game run-time export lookups: none");
    return;
  }
  char line[400];
  int len = 0;
  for (LONG i = 0; i < n && i < kLookupLog && len < (int)sizeof(line) - 12; ++i) {
    len += sprintf_s(line + len, sizeof(line) - len, " %s:%X",
                       (g_lookups[i] >> 16) == 1 ? "xam" : "other", g_lookups[i] & 0xFFFF);
  }
  line[len] = 0;
  util::Log("Game run-time export lookups (%u calls, %u redirected to the portal):%s",
            g_proc_calls, g_proc_redirects, line);
}

DWORD ProcLookups() { return g_proc_calls; }

void SetInputBlocked(bool blocked) { g_block_input = blocked; }

void SetComboFilter(WORD modifier, bool need_lt, bool need_rt, WORD hide) {
  g_combo_hide = hide;
  g_combo_need_lt = need_lt;
  g_combo_need_rt = need_rt;
  g_combo_modifier = modifier;
}

void SetPassthrough(bool passthrough) { g_passthrough = passthrough; }

DWORD ReadCalls() { return g_read_calls; }
DWORD WriteCalls() { return g_write_calls; }

}  // namespace hooks
