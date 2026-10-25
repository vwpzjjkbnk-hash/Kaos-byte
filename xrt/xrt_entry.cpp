// SkyPortal360 open toolchain: the DLL entry point behind entry.S's _xrt_dll_entry.
//
// The loader calls the entry point like DllMain(handle, reason, 0): DLL_PROCESS_ATTACH once, and
// DLL_THREAD_ATTACH / DLL_THREAD_DETACH on every thread start and exit in the game (Xenia's
// kernel_state.cc does the same). Static constructors (.init_array) run once, on the first
// DLL_PROCESS_ATTACH, as the XDK's _DllMainCRTStartup does.
#include <xtl.h>

typedef void (*InitFn)(void);
extern "C" InitFn __init_array_start[];
extern "C" InitFn __init_array_end[];

BOOL APIENTRY DllMain(HANDLE module, DWORD reason, LPVOID reserved);  // src/xbox/main.cpp

namespace {
bool g_constructed = false;
}

extern "C" BOOL xrt_dll_entry(HANDLE module, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH && !g_constructed) {
    g_constructed = true;
    for (InitFn* fn = __init_array_start; fn < __init_array_end; ++fn) (*fn)();
  }
  if (!g_constructed) return TRUE;  // a thread event before attach: nothing set up yet
  return DllMain(module, reason, reserved);
}
