// SkyPortal360 - skydrawn full-screen menu (console side). See overlay.h.
#include "overlay.h"

#include <string.h>

#include "elements.h"
#include "overlay_render.h"
#include "qoi.h"
#include "util.h"

extern "C" {
// xrt/entry.S: the stub the game's VdSwap import is pointed at, and the C recorder it calls.
void sp_vdswap_hook();
// Read by the stub: where it jumps after recording.
DWORD sp_real_vdswap = 0;
// Read by the stub on every VdSwap: 0 while the game's own frames are shown, else the address
// of a MenuFrame whose fetch constant (offset 0) and front buffer address (offset 24) replace
// the game's (r4 and r8).
volatile DWORD sp_menu_current = 0;
// xrt/entry.S: the stub the game's VdSetGraphicsInterruptCallback import is pointed at, and the
// handler it registers in place of Direct3D's (which it calls, then re-points scan-out).
void sp_set_gfx_cb_hook();
void sp_gfx_irq_hook();
DWORD sp_real_set_gfx_cb = 0;           // the real export
volatile DWORD sp_game_gfx_cb = 0;      // Direct3D's interrupt handler
volatile DWORD sp_pin_phys = 0;         // scan-out address to hold while the menu is up, else 0
volatile DWORD sp_pin_game = 0;         // the game's latest scan-out address seen
volatile DWORD sp_surf_phys[3] = {0, 0, 0};  // the menu surfaces (never recorded as the game's)
volatile DWORD sp_irq_count = 0, sp_irq_fixes = 0;
volatile DWORD sp_irq_game_fixes = 0;   // ... of them taking the display off the game's frame
// D1GRPH_FLIP_CONTROL bit 0 makes the display take a new scan-out address at the next horizontal
// retrace (mid-picture) instead of the next vertical blank. Direct3D uses it when a frame is late;
// with the menu up that put a band of the game's frame on screen until the address was re-pointed.
// While the menu is up it's kept at 0 (the interrupt and the pin thread clear it), so any address
// change waits for the vertical blank and is re-pointed at the menu before it's ever shown.
volatile DWORD sp_flipctl_saved = 0;    // 1 once cleared (the game's 1 is put back on closing)
volatile DWORD sp_flipctl_clears = 0;
// The interrupt locks the display's surface registers around Direct3D's handler while the menu
// is up (xrt/entry.S): the register's value before locking, whether it's locked, how often.
volatile DWORD sp_lock_saved = 0, sp_lock_held = 0, sp_lock_uses = 0;
#if defined(SKYPORTAL_XRT)
// The plugin's own import slots (xrt/gen_imports.py): the loader writes the real addresses.
extern const DWORD xrt_imp_VdSwap;
extern const DWORD xrt_imp_VdSetGraphicsInterruptCallback;
DWORD MmAllocatePhysicalMemoryEx(DWORD flags, DWORD size, DWORD protect, DWORD min_address,
                                 DWORD max_address, DWORD alignment);
void MmFreePhysicalMemory(DWORD type, DWORD address);
LONG KeSetBasePriorityThread(DWORD thread, LONG increment);
void* xrt_big_alloc(size_t bytes, int write_combine);
void xrt_big_free(void* p);
void* xrt_virt_alloc(size_t bytes);
void xrt_virt_free(void* p);
LONG MmQueryStatistics(void* stats);
#endif
DWORD MmGetPhysicalAddress(DWORD address);
}

namespace overlay {

namespace {

const int kStripRows = 32;      // one row of the front buffer's 32x32 tiles (fewer when memory is short)
const int kReadChunk = 16 * 1024;  // pictures are read and decoded 16 KB at a time
const int kBgWindowRows = 48;      // background rows decoded at once (it's decoded as the frame is drawn)
const DWORD kMarginKB = 1536;      // memory always left to the game when the menu takes its own frames
const DWORD kFormat2101010 = 7, kFormat2101010As16 = 54;

// ---- the game's frame, written by the VdSwap recorder (the game's render thread) ----

struct GameFrame {
  DWORD fetch[6];     // the texture fetch constant VdSwap was given
  DWORD fb_word;      // *frontbuffer_ptr
  DWORD phys;         // physical address of the front buffer
  int w, h;
  int pitch;          // pixels per row (32-aligned)
  bool tiled;
  DWORD format;
  DWORD endian;
};

GameFrame g_game;                 // latest frame description
GameFrame g_game_prev;            // the one before it (Direct3D alternates two front buffers)
volatile LONG g_game_prev_valid = 0;
// Pausing the game (the menu borrows its two front buffers when there's no memory for its own):
// the game's render thread waits inside its next VdSwap call until the menu closes.
volatile LONG g_pause_req = 0, g_paused = 0;
bool g_allow_pause = true;
volatile DWORD g_game_seq = 0;    // odd while the recorder is writing g_game
volatile LONG g_game_valid = 0;
volatile DWORD g_swaps = 0;
GameFrame g_used;                 // the description the menu surfaces were built for

// ---- menu frames ----

struct MenuFrame {   // layout read by xrt/entry.S
  DWORD fetch[6];
  DWORD fb_word;
  DWORD pad;
};

struct Surface {
  BYTE* mem;         // write-combined physical memory, `bytes` long (or the game's front buffer)
  bool borrowed;     // the game's own front buffer (never freed; the game is paused)
  DWORD bytes;
  DWORD phys;
  MenuFrame frame;   // what VdSwap is given while this surface is shown
};

struct OwnedImage {
  Image img;
  unsigned int* buf;
  DWORD buf_bytes;
  char key[MAX_PATH];
};

// Two menu frames (the one shown and the one drawn), or one when memory is short. Each is a
// full 1280x720 front buffer (3.7 MB of unbroken display memory), which is what runs out: 2.0's
// third frame made Giants, Swap Force and Imaginators fail to open the menu at all.
const int kSurfaces = 2;
Surface g_surf[kSurfaces];
int g_surf_count = 0;
int g_shown = -1;              // surface currently published
DWORD g_published_swap = 0;    // g_swaps when it was published
bool g_ever_shown[kSurfaces];  // published at least once since it was allocated
DWORD g_off_tick[kSurfaces];   // when it stopped being the published one
DWORD g_off_swap[kSurfaces];

unsigned int* g_strip = 0;     // g_strip_rows rows of 0x00RRGGBB
int g_strip_rows = kStripRows;
bool g_borrowed = false;       // drawing into the game's front buffers (game paused)
unsigned int* g_bgwin = 0;     // kBgWindowRows decoded background rows
bool g_bg_drawn = false;       // the last frame had its background
DWORD g_borrow_fail = 0;
BYTE* g_block = 0;             // one tiled block, built in cached memory then copied out
unsigned char* g_read = 0;     // file read buffer (kReadChunk)
BYTE* g_stage = 0;             // one frame: drawn here, then copied onto the shown frame at once
OwnedImage g_bg, g_fig, g_icon, g_logo;

// ---- requests from the worker thread ----

sp::Lock g_lock;
menu::View g_view;
volatile DWORD g_view_version = 0;
volatile bool g_want_open = false;
volatile bool g_stop = false;
volatile int g_state = 0;  // 0 closed, 1 open, 2 closing (waiting to free)
char g_root[MAX_PATH] = "";
bool g_swap_rb = false;

// Diagnostics.
DWORD g_opens = 0, g_renders = 0, g_last_render_ms = 0, g_alloc_fail = 0;

// Dashlaunch build: each step of opening the menu goes in the log (written to disk line by line),
// so if the console crashes there the last line says where. The first few opens per game only.
#if !defined(SKYPORTAL_SIDELOAD)
volatile LONG g_steps_left = 80;
#define STEP(...)                                                  \
  do {                                                             \
    if (g_steps_left > 0) {                                        \
      --g_steps_left;                                              \
      util::Log("Overlay step: " __VA_ARGS__);                     \
    }                                                              \
  } while (0)
#else
#define STEP(...) \
  do {            \
  } while (0)
#endif

// ---- scan-out pinning ----
// The display controller scans out whatever D1GRPH_PRIMARY_SURFACE_ADDRESS holds (a physical
// address; the kernel writes the game's front buffer there on every swap). On hardware, VdSwap
// doesn't take the scan-out address from its arguments, so while the menu is up a thread keeps
// this register pointed at the menu surface, rewriting it within ~1 ms whenever a swap moves it
// back to the game (the register is latched at vertical blank, so the game's frame never shows).
volatile DWORD* const kScanoutAddress = (volatile DWORD*)(0x7FC80000 + 0x1844 * 4);
volatile DWORD* const kFlipControl = (volatile DWORD*)(0x7FC80000 + 0x1852 * 4);  // D1GRPH_FLIP_CONTROL
// (sp_pin_phys: physical address to keep on screen, 0 = leave it alone.)
volatile bool g_pin_active = false;   // the pin thread is holding the register
// The display registers as the scan-out thread last read them (it reads them every few ms), for
// code on other threads: in the Dashlaunch build only the game's own threads touch them, as in the
// Peer Pressure build, where everything ran on game threads.
volatile DWORD g_reg_scanout = 0, g_reg_flip = 0, g_reg_update = 0;
// Dashlaunch build: the drawing and scan-out threads are started from the game's render thread
// (the first frame it presents through VdSwap), so they are the game's own threads, as in the Peer
// Pressure build, and end with the game. The display memory, the display registers and the game's
// frames are only usable from the game's threads: a system thread asking for display memory
// froze the console. g_gen counts games, so a thread of an earlier game never touches the next's.
volatile LONG g_need_game_threads = 0;
volatile LONG g_gen = 0;
volatile LONG g_draw_running = 0;
DWORD g_pin_fixes = 0, g_pin_fixes_total = 0, g_pin_start = 0, g_pin_ms = 0;
DWORD g_pin_game_fixes = 0, g_pin_game_fixes_total = 0;  // ... from the game's frame (not a menu one)
DWORD g_flipctl_at_open = 0xFFFFFFFF;
DWORD g_scanout_at_open = 0;
DWORD g_published_tick = 0;

DWORD Swap32(DWORD v) {
  return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

// Byte offset of pixel (x, y) in a tiled 32 bpp surface (Xenia texture_address.h Tiled2D).
DWORD TiledOffset(int x, int y, int pitch) {
  const int outer = ((y >> 5) * (pitch >> 5) + (x >> 5)) << 6;
  const int inner = (((y >> 1) & 7) << 3) | (x & 7);
  const DWORD oi = (DWORD)(outer | inner) << 2;
  const DWORD bank = (y >> 4) & 1;
  const DWORD pipe = ((x >> 3) & 3) ^ (((y >> 3) & 1) << 1);
  return ((DWORD)(y & 1) << 4) | (pipe << 6) | (bank << 11) | (oi & 15) | (((oi >> 4) & 1) << 5) |
         (((oi >> 5) & 7) << 8) | ((oi >> 8) << 12);
}

// The CPU word to store so the GPU (reading with the buffer's endian swap) sees `rgb`.
DWORD Encode(const GameFrame& fb, DWORD rgb) {
  DWORD r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
  if (g_swap_rb) {
    const DWORD t = r;
    r = b;
    b = t;
  }
  DWORD v;
  if (fb.format == kFormat2101010 || fb.format == kFormat2101010As16) {
    v = 3u << 30 | (r * 1023 / 255) << 20 | (g * 1023 / 255) << 10 | (b * 1023 / 255);
  } else {
    v = 0xFF000000u | r << 16 | g << 8 | b;
  }
  switch (fb.endian) {
    case 0: return Swap32(v);                                              // none
    case 1: return ((v >> 16) | (v << 16));                                // 8in16 -> swap halves
    case 3: return ((v & 0x00FF00FF) << 8) | ((v >> 8) & 0x00FF00FF);      // 16in32 -> 8in16
    default: return v;                                                     // 8in32
  }
}

// ---- memory ----

// Memory the running game has left free, in KB (0 if unknown): the kernel's count of free pages.
DWORD FreeKB() {
#if defined(SKYPORTAL_XRT)
  DWORD stats[26];  // MM_STATISTICS: size, total pages, kernel pages, title: available pages, ...
  memset(stats, 0, sizeof(stats));
  stats[0] = sizeof(stats);
  if (MmQueryStatistics(stats) < 0) return 0;
  return stats[3] * 4;
#else
  return 0;
#endif
}

// Working memory (pictures, the file buffer, the strip being drawn): only the CPU touches it, so
// it comes from ordinary virtual memory first, which doesn't need an unbroken stretch of physical
// memory (in-game, that is what runs out first: pictures turned into element icons in
// SuperChargers once a level was loaded).
void* WorkAlloc(DWORD bytes) {
#if defined(SKYPORTAL_XRT)
  void* p = xrt_virt_alloc(bytes);
  if (!p) p = xrt_big_alloc(bytes, 0);
  return p;
#else
  return XPhysicalAlloc(bytes, MAXULONG_PTR, 4096, PAGE_READWRITE);
#endif
}

void WorkFree(void* p) {
  if (!p) return;
#if defined(SKYPORTAL_XRT)
  if ((DWORD)p < 0x80000000u) {
    xrt_virt_free(p);  // virtual memory lives below 0x80000000, physical views above
  } else {
    xrt_big_free(p);
  }
#else
  XPhysicalFree(p);
#endif
}

// Display memory: write-combined, 64 KB pages like Direct3D's own front buffers.
BYTE* DisplayAlloc(DWORD bytes) {
  bytes = (bytes + 0xFFFF) & ~0xFFFFu;
#if defined(SKYPORTAL_XRT)
  BYTE* p = (BYTE*)MmAllocatePhysicalMemoryEx(0, bytes, 0x04 | 0x400 | 0x20000000, 0, 0xFFFFFFFF,
                                              0x10000);
  // No run of free 64 KB pages that long: the same in 4 KB pages (still one unbroken stretch).
  if (!p) p = (BYTE*)MmAllocatePhysicalMemoryEx(0, bytes, 0x04 | 0x400, 0, 0xFFFFFFFF, 0x10000);
  return p;
#else
  return (BYTE*)XPhysicalAlloc(bytes, MAXULONG_PTR, 0x10000,
                               PAGE_READWRITE | PAGE_WRITECOMBINE | MEM_LARGE_PAGES);
#endif
}

void DisplayFree(BYTE* p) {
  if (!p) return;
#if defined(SKYPORTAL_XRT)
  MmFreePhysicalMemory(0, (DWORD)p);
#else
  XPhysicalFree(p);
#endif
}

void FreeImage(OwnedImage* o) {
  WorkFree(o->buf);
  memset(o, 0, sizeof(*o));
}

// Dashlaunch build: the game has exited. The menu's memory belonged to the game (its threads asked
// for it) and went back with it: it's let go of without being freed, so nothing is freed twice.
volatile bool g_abandon = false;

void FreeAll() {
  const bool keep = g_abandon;
  for (int i = 0; i < kSurfaces; ++i) {
    sp_surf_phys[i] = 0;
    if (!g_surf[i].borrowed && !keep) DisplayFree(g_surf[i].mem);
    memset(&g_surf[i], 0, sizeof(g_surf[i]));
  }
  g_surf_count = 0;
  g_shown = -1;
  g_borrowed = false;
  if (!keep) {
    WorkFree(g_strip);
    WorkFree(g_block);
    WorkFree(g_read);
    WorkFree(g_stage);
    WorkFree(g_bgwin);
    FreeImage(&g_bg);
    FreeImage(&g_fig);
    FreeImage(&g_icon);
    FreeImage(&g_logo);
  } else {
    memset(&g_bg, 0, sizeof(g_bg));
    memset(&g_fig, 0, sizeof(g_fig));
    memset(&g_icon, 0, sizeof(g_icon));
    memset(&g_logo, 0, sizeof(g_logo));
  }
  g_stage = 0;
  g_strip = 0;
  g_block = 0;
  g_read = 0;
  g_bgwin = 0;
}

// ---- images ----

// Loads a QOI file into `o` (keyed by path, so the same picture isn't read twice). An empty or
// missing path leaves the image empty.
// Why the last picture that failed to load failed (for the log).
char g_img_fail_path[MAX_PATH] = "";
const char* g_img_fail_why = "";
DWORD g_img_fails = 0;

void ImageFailed(const char* path, const char* why) {
  strncpy_s(g_img_fail_path, sizeof(g_img_fail_path), path, _TRUNCATE);
  g_img_fail_why = why;
  ++g_img_fails;
}

int ReadChunk(void* ctx, unsigned char* buf, int cap) {
  DWORD got = 0;
  if (!ReadFile((HANDLE)ctx, buf, (DWORD)cap, &got, NULL)) return -1;
  return (int)got;
}

void LoadImage(OwnedImage* o, const char* path) {
  if (!path || !path[0]) {
    o->img.px = 0;
    o->key[0] = 0;
    return;
  }
  if (!strcmp(o->key, path) && o->img.px) return;
  o->img.px = 0;
  strncpy_s(o->key, sizeof(o->key), path, _TRUNCATE);
  if (!g_read) return;
  HANDLE f = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return ImageFailed(path, "missing or unreadable");
  qoi::Reader reader(ReadChunk, f, g_read, kReadChunk);
  int w = 0, h = 0;
  if (!reader.Header(&w, &h) || w <= 0 || h <= 0 || w > 2048 || h > 2048) {
    CloseHandle(f);
    return ImageFailed(path, "not a QOI picture");
  }
  const DWORD need = (DWORD)(w * h * 4);
  if (o->buf_bytes < need) {
    WorkFree(o->buf);
    o->buf = (unsigned int*)WorkAlloc(need);
    o->buf_bytes = o->buf ? need : 0;
  }
  if (!o->buf) {
    CloseHandle(f);
    return ImageFailed(path, "not enough memory");
  }
  const bool ok = reader.Decode(o->buf, w * h);
  CloseHandle(f);
  if (!ok) return ImageFailed(path, "damaged QOI picture");
  o->img.w = w;
  o->img.h = h;
  o->img.px = o->buf;
}

void UiPath(char* out, const char* rel) {
  strcpy(out, g_root);
  strcat(out, "\\UI\\");
  strcat(out, rel);
}

void LoadSceneImages(const menu::View& v) {
  char path[MAX_PATH];
  LoadImage(&g_fig, v.id >= 0 ? v.image : "");
  path[0] = 0;
  const elements::Element el = (elements::Element)v.element;
  if (el != elements::kElementUnknown) {
    char name[24];
    const char* n = elements::Name(el);
    int i = 0;
    for (; n[i] && i < 23; ++i) name[i] = (char)(n[i] >= 'A' && n[i] <= 'Z' ? n[i] - 'A' + 'a' : n[i]);
    name[i] = 0;
    char rel[48] = "elements\\";
    strcat(rel, name);
    strcat(rel, ".qoi");
    UiPath(path, rel);
  }
  LoadImage(&g_icon, path);
  path[0] = 0;
  if (v.game >= 1 && v.game <= 6) {
    char rel[16] = "games\\0.qoi";
    rel[6] = (char)('0' + v.game);
    UiPath(path, rel);
  }
  LoadImage(&g_logo, path);
}

// ---- frames ----

// Picture buffers big enough for every picture the menu shows, taken when it opens so a picture
// never needs new memory while you browse (card art is at most 320x320, game logos 440x200).
void Reserve(OwnedImage* o, DWORD bytes) {
  if (o->buf_bytes >= bytes) return;
  WorkFree(o->buf);
  o->buf = (unsigned int*)WorkAlloc(bytes);
  o->buf_bytes = o->buf ? bytes : 0;
}

const char* g_alloc_what = "";  // what the last failed menu allocation was for

// The menu's frames, in order of preference:
//   1. two of its own, if that still leaves the game kMarginKB;
//   2. the game's own two front buffers, the game paused while the menu is up (no memory needed:
//      Imaginators leaves less than one frame's worth);
//   3. one of its own, if that leaves the margin; 4. one of its own anyway.
void SetupSurface(Surface& s, const GameFrame& gf, DWORD phys, DWORD bytes) {
  s.bytes = bytes;
  s.phys = phys;
  // Same fetch constant and front buffer word as the game's, moved to this surface: the game's
  // values are taken relative to its physical address, so whichever address form it uses
  // (physical, or a mapped view) this surface's is given in the same form.
  memcpy(s.frame.fetch, gf.fetch, sizeof(s.frame.fetch));
  const DWORD game_base = gf.fetch[1] & 0xFFFFF000;
  s.frame.fetch[1] = ((s.phys + (game_base - gf.phys)) & 0xFFFFF000) | (gf.fetch[1] & 0xFFF);
  s.frame.fb_word = s.phys + (gf.fb_word - gf.phys);
}

int OwnFrames(const GameFrame& gf, DWORD bytes, int want, DWORD margin_kb) {
  for (int i = g_surf_count; i < want; ++i) {
    Surface& s = g_surf[i];
    STEP("frame %d: asking for %u bytes of display memory (%u KB free)", i, bytes, FreeKB());
    s.mem = DisplayAlloc(bytes);
    STEP("frame %d: display memory at %08X", i, (DWORD)s.mem);
    if (!s.mem) break;
    if (margin_kb && FreeKB() < margin_kb) {  // would leave the game too little
      DisplayFree(s.mem);
      s.mem = 0;
      break;
    }
    s.borrowed = false;
    SetupSurface(s, gf, MmGetPhysicalAddress((DWORD)s.mem), bytes);
    STEP("frame %d: physical %08X, front buffer word %08X, fetch %08X", i, s.phys, s.frame.fb_word,
         s.frame.fetch[1]);
    sp_surf_phys[i] = s.phys;
    g_ever_shown[i] = false;
    g_surf_count = i + 1;
  }
  return g_surf_count;
}

void FreeOwnFrames() {
  for (int i = 0; i < g_surf_count; ++i) {
    if (!g_surf[i].borrowed) DisplayFree(g_surf[i].mem);
    sp_surf_phys[i] = 0;
    memset(&g_surf[i], 0, sizeof(g_surf[i]));
  }
  g_surf_count = 0;
}

void ReadGame(GameFrame* out);

void ResumeGame() {
  sp_menu_current = 0;
  __sync();
  g_pause_req = 0;
}

// Pauses the game at its next frame and takes its two front buffers. The one it was about to
// present (whose picture the GPU is finishing) is drawn first; the one on screen after that.
bool BorrowFrames(GameFrame* used) {
  if (!g_allow_pause) return false;
  STEP("pausing the game to use its own frames");
  g_pause_req = 1;
  __sync();
  const DWORD t0 = GetTickCount();
  while (!g_paused && g_want_open && !g_stop && GetTickCount() - t0 < 2000) Sleep(2);
  if (!g_paused) {
    g_pause_req = 0;
    STEP("the game didn't pause within 2 s");
    return false;
  }
  STEP("game paused");
  GameFrame cur, prev;
  ReadGame(&cur);
  prev = g_game_prev;
  const int rows = (cur.h + 31) & ~31;
  const DWORD bytes = (DWORD)(cur.pitch * rows * 4);
  if (cur.fb_word < 0x80000000u) {  // no CPU view of it to draw through
    ResumeGame();
    return false;
  }
  Sleep(120);  // the GPU finishes writing the frames it was handed (it can be a few behind)
  g_surf[0].mem = (BYTE*)cur.fb_word;
  g_surf[0].borrowed = true;
  SetupSurface(g_surf[0], cur, cur.phys, bytes);
  g_surf_count = 1;
  if (g_game_prev_valid && prev.phys != cur.phys && prev.w == cur.w && prev.h == cur.h &&
      prev.pitch == cur.pitch && prev.fb_word >= 0x80000000u) {
    g_surf[1].mem = (BYTE*)prev.fb_word;
    g_surf[1].borrowed = true;
    SetupSurface(g_surf[1], prev, prev.phys, bytes);
    g_surf_count = 2;
    // It's on screen now: draw into it only once the display has moved to the first one.
    g_ever_shown[1] = true;
    g_off_tick[1] = GetTickCount();
    g_off_swap[1] = g_swaps;
  }
  g_ever_shown[0] = false;
  for (int i = 0; i < g_surf_count; ++i) sp_surf_phys[i] = g_surf[i].phys;
  g_borrowed = true;
  sp_pin_game = 0;  // nothing to put back: the game presents its next frame when it resumes
  *used = cur;
  return true;
}

bool AllocateFrames(GameFrame& gf) {
  if (g_surf_count > 0) return true;
  const int rows = (gf.h + 31) & ~31;
  const DWORD bytes = (DWORD)(gf.pitch * rows * 4);
  sp_pin_game = gf.phys;
  if (OwnFrames(gf, bytes, 2, kMarginKB) < 2) {
    FreeOwnFrames();
    if (!BorrowFrames(&gf)) {
      ++g_borrow_fail;
      if (OwnFrames(gf, bytes, 1, kMarginKB) < 1) OwnFrames(gf, bytes, 1, 0);
    }
  }
  if (g_surf_count == 0) {
    g_alloc_what = "a menu frame (display memory)";
    ++g_alloc_fail;
    FreeAll();
    return false;
  }
  // What the menu can't do without: a strip of 32 rows and its tiled copy, or 8 rows written
  // straight to the frame when memory is short; and the file buffer.
  STEP("%d frame(s)%s; working memory next", g_surf_count, g_borrowed ? " (the game's own)" : "");
  g_read = (unsigned char*)WorkAlloc(kReadChunk);
  STEP("file buffer at %08X", (DWORD)g_read);
  g_strip_rows = kStripRows;
  g_strip = (unsigned int*)WorkAlloc((DWORD)(gf.w * kStripRows * 4));
  if (g_strip) g_block = (BYTE*)WorkAlloc((DWORD)(gf.pitch * kStripRows * 4));
  if (!g_block) {
    WorkFree(g_strip);
    g_strip_rows = 8;
    g_strip = (unsigned int*)WorkAlloc((DWORD)(gf.w * g_strip_rows * 4));
  }
  if (!g_strip || !g_read) {
    g_alloc_what = "working memory (about 100 KB)";
    ++g_alloc_fail;
    if (g_borrowed) ResumeGame();
    FreeAll();
    return false;
  }
  // Pictures: the menu still works without any of them. Most visible first.
  g_bgwin = (unsigned int*)WorkAlloc(480 * kBgWindowRows * 4);
  Reserve(&g_icon, 128 * 128 * 4);
  Reserve(&g_logo, 220 * 100 * 4);
  Reserve(&g_fig, 320 * 320 * 4);
  // One frame of its own only: draw into a copy and put it on screen in one go, so a new figure
  // never appears a strip at a time (skipped if there isn't room for the copy either).
  if (g_surf_count == 1 && !g_borrowed && FreeKB() > bytes / 1024 + kMarginKB) {
    g_stage = (BYTE*)WorkAlloc(bytes);
  }
  STEP("working memory: strip %08X (%d rows), block %08X, background rows %08X", (DWORD)g_strip,
       g_strip_rows, (DWORD)g_block, (DWORD)g_bgwin);
  return true;
}

// Renders the scene into a surface, 32 rows at a time.
// The background is decoded a few rows at a time as the frame is drawn top to bottom, so it never
// needs memory for the whole picture.
struct BgStream {
  HANDLE file;
  qoi::Reader* reader;
  int w, h;
  int first, count;  // rows [first, first + count) are in g_bgwin
  int cap;           // rows g_bgwin holds
  int decoded;       // rows decoded from the file so far
  bool ok;
};

bool BgRows(BgStream* b, int first, int last) {
  if (!b->ok) return false;
  if (last - first + 1 > b->cap) return (b->ok = false);
  if (b->count && first > b->first) {  // drop rows no longer needed
    const int drop = first - b->first < b->count ? first - b->first : b->count;
    memmove(g_bgwin, g_bgwin + drop * b->w, (DWORD)((b->count - drop) * b->w * 4));
    b->first += drop;
    b->count -= drop;
  }
  if (b->count == 0) b->first = b->decoded;
  while (b->decoded < first) {  // rows above the ones needed (count is 0 here)
    if (!b->reader->DecodeMore(g_bgwin, b->w)) return (b->ok = false);
    ++b->decoded;
    b->first = b->decoded;
  }
  while (b->decoded <= last) {
    if (b->count >= b->cap) return (b->ok = false);
    if (!b->reader->DecodeMore(g_bgwin + b->count * b->w, b->w)) return (b->ok = false);
    ++b->count;
    ++b->decoded;
  }
  return first >= b->first && last < b->first + b->count;
}

void RenderInto(BYTE* dst, const GameFrame& gf, const Scene& base) {
  const DWORD t0 = GetTickCount();
  Scene scene = base;
  // The background, streamed.
  BgStream bg;
  memset(&bg, 0, sizeof(bg));
  char path[MAX_PATH];
  UiPath(path, "background.qoi");
  bg.file = g_bgwin && g_read ? CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                           FILE_ATTRIBUTE_NORMAL, NULL)
                              : INVALID_HANDLE_VALUE;
  qoi::Reader reader(ReadChunk, bg.file, g_read, kReadChunk);
  if (bg.file != INVALID_HANDLE_VALUE) {
    bg.reader = &reader;
    if (reader.Header(&bg.w, &bg.h) && bg.w > 0 && bg.w <= 1920 && bg.h > 0) {
      bg.cap = (480 * kBgWindowRows) / bg.w;
      bg.ok = bg.cap >= 2;
    } else {
      ImageFailed(path, "not a QOI picture");
    }
  } else if (g_bgwin) {
    ImageFailed(path, "missing or unreadable");
  }
  scene.background.px = 0;
  const DWORD block_bytes = (DWORD)(gf.pitch * kStripRows * 4);
  const int step = g_strip_rows;
  bool bg_all = bg.ok;
  for (int y0 = 0; y0 < gf.h; y0 += step) {
    Strip strip;
    strip.w = gf.w;
    strip.h = gf.h;
    strip.y0 = y0;
    strip.rows = gf.h - y0 < step ? gf.h - y0 : step;
    strip.px = g_strip;
    if (bg.ok) {
      int first, last;
      BackgroundRows(bg.w, bg.h, gf.w, gf.h, y0, y0 + strip.rows, &first, &last);
      if (BgRows(&bg, first, last)) {
        scene.background.w = bg.w;
        scene.background.h = bg.h;
        scene.background.px = g_bgwin - bg.first * bg.w;  // row r is at px[r * w] for r in the window
      } else {
        scene.background.px = 0;
        bg_all = false;
      }
    }
    RenderStrip(scene, &strip);
    if (gf.tiled && g_block && step == kStripRows) {
      // All 32 rows of a tile row are one contiguous block of the surface.
      const DWORD block_base = TiledOffset(0, y0, gf.pitch);
      for (int y = 0; y < strip.rows; ++y) {
        const unsigned int* row = &g_strip[y * gf.w];
        for (int x = 0; x < gf.w; ++x) {
          const DWORD off = TiledOffset(x, y0 + y, gf.pitch) - block_base;
          *(DWORD*)(g_block + off) = Encode(gf, row[x]);
        }
      }
      memcpy(dst + block_base, g_block, block_bytes);
    } else if (gf.tiled) {
      for (int y = 0; y < strip.rows; ++y) {
        const unsigned int* row = &g_strip[y * gf.w];
        for (int x = 0; x < gf.w; ++x) {
          *(DWORD*)(dst + TiledOffset(x, y0 + y, gf.pitch)) = Encode(gf, row[x]);
        }
      }
    } else {
      for (int y = 0; y < strip.rows; ++y) {
        const unsigned int* row = &g_strip[y * gf.w];
        DWORD* out = (DWORD*)(dst + (DWORD)((y0 + y) * gf.pitch) * 4);
        for (int x = 0; x < gf.w; ++x) out[x] = Encode(gf, row[x]);
      }
    }
  }
  if (bg.file != INVALID_HANDLE_VALUE) CloseHandle(bg.file);
  g_bg_drawn = bg_all;
  __sync();
  ++g_renders;
  g_last_render_ms = GetTickCount() - t0;
}

// Pushes what the CPU wrote out of its cache (the game's front buffers may be cached memory).
void FlushToMemory(BYTE* p, DWORD bytes) {
#if defined(SKYPORTAL_XRT)
  for (DWORD off = 0; off < bytes; off += 128) __asm__ volatile("dcbst 0, %0" : : "r"(p + off) : "memory");
#else
  for (DWORD off = 0; off < bytes; off += 128) __dcbst(0, p + off);
#endif
  __sync();
}

// A consistent copy of the latest frame description (the recorder may be mid-update).
void ReadGame(GameFrame* out) {
  for (;;) {
    const DWORD seq = g_game_seq;
    __lwsync();
    if (seq & 1) {
      Sleep(0);
      continue;
    }
    *out = g_game;
    __lwsync();
    if (g_game_seq == seq) return;
  }
}

void Publish(int idx) {
  if (g_shown >= 0 && g_shown != idx) {
    g_off_tick[g_shown] = GetTickCount();
    g_off_swap[g_shown] = g_swaps;
  }
  g_ever_shown[idx] = true;
  g_shown = idx;
  g_published_swap = g_swaps;
  g_published_tick = GetTickCount();
  __sync();
  sp_menu_current = (DWORD)&g_surf[idx].frame;
  sp_pin_phys = g_surf[idx].phys;
}

bool IsMenuSurface(DWORD phys) {
  for (int i = 0; i < g_surf_count; ++i) {
    if (g_surf[i].phys == phys) return true;
  }
  return false;
}

DWORD WINAPI PinThread(LPVOID) {
  // Time-critical, so it wakes on time (it sleeps between its quick register checks).
#if defined(SKYPORTAL_XRT)
  DWORD kthread = 0;
  __asm__ volatile("lwz %0, 0x100(13)" : "=r"(kthread));  // KeGetCurrentThread(): PCR->CurrentThread
  if (kthread) KeSetBasePriorityThread(kthread, 15);
#else
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
  STEP("scan-out thread running (a thread of the game)");
  const LONG gen = g_gen;
  while (!g_stop && gen == g_gen) {
    g_reg_scanout = *kScanoutAddress;
    g_reg_flip = *kFlipControl;
    g_reg_update = *(volatile DWORD*)(0x7FC80000 + 0x1851 * 4);
    const DWORD want = sp_pin_phys;
    if (want) {
      if (!g_pin_active) {
        STEP("scan-out thread: holding the display on the menu (first register read next)");
        g_pin_active = true;
        g_pin_fixes = 0;
        g_pin_game_fixes = 0;
        g_pin_start = GetTickCount();
      }
      if (*kFlipControl == 1) {  // see sp_flipctl_saved
        if (g_flipctl_at_open == 0xFFFFFFFF) g_flipctl_at_open = 1;
        *kFlipControl = 0;
        __sync();
        sp_flipctl_saved = 1;
        ++sp_flipctl_clears;
      } else if (g_flipctl_at_open == 0xFFFFFFFF) {
        g_flipctl_at_open = *kFlipControl;
      }
      const DWORD cur = *kScanoutAddress;
      if (g_pin_fixes == 0 && g_pin_game_fixes == 0) STEP("scan-out register read: %08X", cur);
      if (cur != want) {
        if (!IsMenuSurface(cur)) {
          sp_pin_game = cur;
          ++g_pin_game_fixes;
        }
        *kScanoutAddress = want;
        __sync();
        ++g_pin_fixes;
      }
      Sleep(1);
      continue;
    }
    if (g_pin_active) {
      // Released: put the game's latest frame back now rather than at its next swap (the kernel
      // may have no reason to write the register again if the game is paused).
      const DWORD cur = *kScanoutAddress;
      if (IsMenuSurface(cur) && sp_pin_game) {
        *kScanoutAddress = sp_pin_game;
        __sync();
      }
      if (sp_flipctl_saved) {  // the game's own setting back
        *kFlipControl = 1;
        __sync();
        sp_flipctl_saved = 0;
      }
      g_pin_ms = GetTickCount() - g_pin_start;
      g_pin_fixes_total += g_pin_fixes;
      g_pin_game_fixes_total += g_pin_game_fixes;
      __sync();
      g_pin_active = false;
    }
    Sleep(5);
  }
  if (g_pin_active && gen == g_gen) {
    if (IsMenuSurface(*kScanoutAddress) && sp_pin_game) *kScanoutAddress = sp_pin_game;
    if (sp_flipctl_saved) *kFlipControl = 1;
    sp_flipctl_saved = 0;
    __sync();
    g_pin_active = false;
  }
  return 0;
}

DWORD WINAPI DrawThread(LPVOID) {
  // Normal priority: below normal, a busy game (Giants at 1080p) left it 20 s to draw a frame.
  const LONG gen = g_gen;
#if !defined(SKYPORTAL_SIDELOAD)
  g_draw_running = 1;
  // The data folder's drive names, as this game sees them (the worker's are system-wide ones).
  util::MountDrive("SKYHDD:", "\\Device\\Harddisk0\\Partition1", false);
  util::MountDrive("SKYUSB0:", "\\Device\\Mass0", false);
  util::MountDrive("SKYUSB1:", "\\Device\\Mass1", false);
  util::MountDrive("SKYUSB2:", "\\Device\\Mass2", false);
  STEP("drawing thread running (a thread of the game)");
#endif
  DWORD drawn_version = 0xFFFFFFFF;
  DWORD close_swap = 0, close_tick = 0;
  menu::View view;
  while (!g_stop && gen == g_gen) {
    if (g_state == 0) {  // closed
      if (!g_want_open || !g_game_valid) {
        Sleep(10);
        continue;
      }
    } else if (g_state == 2) {  // closing: free once the display has moved back to the game
      if (g_want_open && !g_borrowed) {
        g_state = 1;
        drawn_version = 0xFFFFFFFF;  // reopen with the surfaces still allocated
      } else if (g_want_open) {
        // Reopened while closing from the game's frames: the game has resumed, so start over.
        FreeAll();
        g_state = 0;
        continue;
      } else {
        if (!g_pin_active && (g_swaps - close_swap >= 3 || GetTickCount() - close_tick > 3000)) {
          FreeAll();
          g_state = 0;
          util::Log("Overlay: menu was on screen %u ms; scan-out re-pointed at the menu %u time(s) "
                    "outside the interrupt (%u of them off the game's frame); flip control %08X "
                    "when opened, cleared %u time(s) so far",
                    g_pin_ms, g_pin_fixes, g_pin_game_fixes, g_flipctl_at_open, sp_flipctl_clears);
        }
        Sleep(10);
        continue;
      }
    } else if (!g_want_open) {  // open -> closing
      sp_menu_current = 0;
      sp_pin_phys = 0;
      __sync();
      if (g_borrowed) ResumeGame();  // the game carries on and presents its next frame
      close_swap = g_swaps;
      close_tick = GetTickCount();
      g_state = 2;
      continue;
    }

    if (g_state == 0) {
      ReadGame(&g_used);
      STEP("opening: the game's frame is %dx%d at %08X (physical %08X), %u KB free", g_used.w,
           g_used.h, g_used.fb_word, g_used.phys, FreeKB());
      if (!AllocateFrames(g_used)) {
        util::Log("Overlay: not enough memory for the menu (%dx%d): %s; %u KB free", g_used.w,
                  g_used.h, g_alloc_what, FreeKB());
        g_want_open = false;
        continue;
      }
      ++g_opens;
      g_state = 1;
      drawn_version = 0xFFFFFFFF;
      g_scanout_at_open = g_reg_scanout;
      if (g_opens == 1 || g_surf_count < kSurfaces || g_borrowed) {
        util::Log("Overlay: %s %d x %u bytes, %u KB free, strips of %d rows;", g_borrowed
                  ? "game paused, drawing into its own front buffers:" : "menu frames", g_surf_count,
                  g_surf[0].bytes, FreeKB(), g_strip_rows);
        util::Log("Overlay: menu frames %d x %u bytes; game front buffer %08X (phys %08X, fetch "
                  "%08X) -> menu %08X (phys %08X, fetch %08X)",
                  g_surf_count, g_surf[0].bytes, g_used.fb_word, g_used.phys, g_used.fetch[1],
                  g_surf[0].frame.fb_word, g_surf[0].phys, g_surf[0].frame.fetch[1]);
        util::Log("Overlay: scan-out address register %08X (game front buffer phys %08X)",
                  g_scanout_at_open, g_used.phys);
      }
    }

    if (drawn_version == g_view_version) {
      Sleep(5);
      continue;
    }
    // A surface we may draw into: one not shown - never shown yet, else the one off screen the
    // longest - once the display certainly can't flip to it any more (frames the game queued
    // before it was replaced can still point at it for a few swaps); or the only one.
    int target = 0;
    if (g_surf_count >= 2 && g_shown >= 0) {
      target = -1;
      for (int i = 0; i < g_surf_count; ++i) {
        if (i == g_shown) continue;
        if (!g_ever_shown[i]) {
          target = i;
          break;
        }
        if (target < 0 || (int)(g_off_tick[i] - g_off_tick[target]) < 0) target = i;
      }
      for (;;) {
        if (g_stop || !g_want_open || !g_ever_shown[target]) break;
        const DWORD since = GetTickCount() - g_off_tick[target];
        if (since >= 40 && (g_swaps - g_off_swap[target] >= 3 || since >= 150)) break;
        Sleep(2);
      }
    } else if (g_shown >= 0) {
      target = g_shown;
    }
    {
      sp::Guard guard(g_lock);
      view = g_view;
      drawn_version = g_view_version;
    }
    STEP("loading pictures");
    LoadSceneImages(view);
    STEP("drawing into frame %d (%08X)", target, (DWORD)g_surf[target].mem);
    Scene scene;
    scene.view = &view;
    scene.background.px = 0;  // streamed by RenderInto
    scene.figure = g_fig.img;
    scene.element_icon = g_icon.img;
    scene.game_logo = g_logo.img;
    if (g_stage && g_surf_count == 1) {
      RenderInto(g_stage, g_used, scene);
      memcpy(g_surf[target].mem, g_stage, g_surf[target].bytes);
      __sync();
    } else {
      RenderInto(g_surf[target].mem, g_used, scene);
      if (g_surf[target].borrowed) FlushToMemory(g_surf[target].mem, g_surf[target].bytes);
    }
    if (g_renders == 1) util::Log("Overlay: first menu frame drawn in %u ms", g_last_render_ms);
    STEP("drawn; putting frame %d on screen", target);
    if (g_want_open) Publish(target);
  }
  if (gen == g_gen) {
    sp_menu_current = 0;
    sp_pin_phys = 0;
    g_pause_req = 0;
    g_draw_running = 0;
  }
  return 0;
}

}  // namespace

}  // namespace overlay

// Called by sp_vdswap_hook (through its GUEST_ENTRY trampoline) for every frame the game presents,
// before any substitution: these are always the game's own values.
extern "C" void sp_vdswap_record(const DWORD* fetch, const DWORD* frontbuffer_ptr,
                                 const DWORD* format_ptr) {
  using namespace overlay;
  if (!fetch || !frontbuffer_ptr) return;
  const DWORD d0 = fetch[0], d1 = fetch[1], d2 = fetch[2];
  const DWORD word = *frontbuffer_ptr ? *frontbuffer_ptr : (d1 & 0xFFFFF000);
  if (!g_game_valid || g_game.fb_word != word || g_game.fetch[2] != d2) {
    // A front buffer we haven't described yet (Direct3D rotates two): the size and format stay the
    // same, so only the address-dependent fields matter, but refresh everything.
    GameFrame gf;
    for (int i = 0; i < 6; ++i) gf.fetch[i] = fetch[i];
    gf.fb_word = word;
    gf.phys = word < 0x20000000 ? word : MmGetPhysicalAddress(word);
    gf.w = (int)(d2 & 0x1FFF) + 1;
    gf.h = (int)((d2 >> 13) & 0x1FFF) + 1;
    gf.pitch = (int)(((d0 >> 22) & 0x1FF) << 5);
    if (gf.pitch < gf.w) gf.pitch = (gf.w + 31) & ~31;
    gf.tiled = (d0 >> 31) != 0;
    gf.format = format_ptr ? *format_ptr : (d1 & 0x3F);
    gf.endian = (d1 >> 6) & 3;
    g_game_seq = g_game_seq + 1;
    __lwsync();
    if (g_game_valid) {
      g_game_prev = g_game;
      g_game_prev_valid = 1;
    }
    g_game = gf;
    __lwsync();
    g_game_seq = g_game_seq + 1;
    g_game_valid = 1;
  }
  g_swaps = g_swaps + 1;
  if (g_need_game_threads) {  // Dashlaunch build: this is the game's render thread
    g_need_game_threads = 0;
    util::StartGameThread(DrawThread, 0x10000);
    util::StartGameThread(PinThread, 0x4000);
  }
  // The menu wants the game's front buffers: wait here (the game's render thread, before this
  // frame is presented) until it closes. The game's other threads carry on.
  if (g_pause_req) {
    g_paused = 1;
    __sync();
    while (g_pause_req && !g_stop) Sleep(2);
    g_paused = 0;
  }
}

namespace overlay {

void Init() {
#if defined(SKYPORTAL_XRT)
  sp_real_vdswap = xrt_imp_VdSwap;
  sp_real_set_gfx_cb = xrt_imp_VdSetGraphicsInterruptCallback;
#else
  sp_real_vdswap = util::ResolveExport("xboxkrnl.exe", 0x25B);
  sp_real_set_gfx_cb = util::ResolveExport("xboxkrnl.exe", 0x1D5);
#endif
}

int PatchTitle(PLDR_DATA_TABLE_ENTRY title) {
  int n = 0;
  if (sp_real_vdswap) n += util::PatchModuleImport(title, sp_real_vdswap, (DWORD)&sp_vdswap_hook);
  if (sp_real_set_gfx_cb) {
    n += util::PatchModuleImport(title, sp_real_set_gfx_cb, (DWORD)&sp_set_gfx_cb_hook);
  }
  return n;
}

void Start(const char* root, bool swap_rb) {
  strncpy_s(g_root, sizeof(g_root), root, _TRUNCATE);
  g_swap_rb = swap_rb;
#if defined(SKYPORTAL_SIDELOAD)
  util::StartThread(DrawThread, 0x10000);
  util::StartThread(PinThread, 0x4000);
#else
  g_need_game_threads = 1;  // started by each game's first frame (sp_vdswap_record)
#endif
}

void Open(const menu::View& v) {
  {
    sp::Guard guard(g_lock);
    g_view = v;
    ++g_view_version;
  }
  g_want_open = true;
}

void Update(const menu::View& v) {
  sp::Guard guard(g_lock);
  g_view = v;
  ++g_view_version;
}

void Close() { g_want_open = false; }

bool IsOpen() { return g_want_open; }

bool Ready() { return g_game_valid != 0; }

void SetAllowPause(bool allow) { g_allow_pause = allow; }

bool TitleEnded() {
  const bool was_open = g_want_open || g_state != 0;
  ++g_gen;  // the old game's drawing and scan-out threads stop, if anything of them is left
  sp_pin_game = 0;  // nothing of the old game's to put back on screen
  sp_pin_phys = 0;
  sp_menu_current = 0;
  sp_flipctl_saved = 0;
  __sync();
  g_want_open = false;
  g_pause_req = 0;
  // They ended with the game; if one is somehow still finishing a frame, give it a moment.
  for (int i = 0; i < 50 && g_draw_running; ++i) Sleep(10);
  g_draw_running = 0;
  g_abandon = true;  // their memory went back with the game: forget it, don't free it
  FreeAll();
  g_abandon = false;
  g_state = 0;
  g_pin_active = false;
  g_game_valid = 0;
  g_game_prev_valid = 0;
  g_lock.Reinit();  // in case the game ended while one of them held it
#if !defined(SKYPORTAL_SIDELOAD)
  g_steps_left = 80;
  g_need_game_threads = 1;  // the next game's first frame starts its own
#endif
  return was_open;
}

void Stop() {
  g_want_open = false;
  g_stop = true;
  g_pause_req = 0;
}

void LogStatus() {
  if (!g_game_valid) {
    util::Log("Overlay: no frames presented through VdSwap yet (%u swaps)", g_swaps);
    return;
  }
  const GameFrame& fb = g_game;
  util::Log("Overlay: %u swaps, %dx%d pitch %d %s format %u endian %u, front buffer %08X "
            "(phys %08X), fetch %08X %08X %08X",
            g_swaps, fb.w, fb.h, fb.pitch, fb.tiled ? "tiled" : "linear", fb.format, fb.endian,
            fb.fb_word, fb.phys, fb.fetch[0], fb.fetch[1], fb.fetch[2]);
  util::Log("Overlay: %u opens, %u renders (last %u ms), %d menu frame(s) (%08X %08X), "
            "state %d, %u allocation failures",
            g_opens, g_renders, g_last_render_ms, g_surf_count, g_surf[0].frame.fb_word,
            g_surf[1].frame.fb_word, (int)g_state, g_alloc_fail);
  util::Log("Overlay: %u KB free; one-frame copy %s; background %s; %u picture(s) failed to load%s%s%s%s%s",
            FreeKB(), g_stage ? "yes" : "no", g_bg_drawn ? "drawn" : "not drawn", g_img_fails, g_img_fails ? " (last: " : "",
            g_img_fails ? g_img_fail_path : "", g_img_fails ? ", " : "",
            g_img_fails ? g_img_fail_why : "", g_img_fails ? ")" : "");
  util::Log("Overlay: Direct3D interrupt handler %08X, %u interrupts, scan-out re-pointed %u time(s) "
            "in the interrupt (%u off the game's frame), surface update locked %u time(s), "
            "update register %08X",
            sp_game_gfx_cb, sp_irq_count, sp_irq_fixes, sp_irq_game_fixes, sp_lock_uses,
            g_reg_update);
  util::Log("Overlay: scan-out register %08X, flip control %08X (cleared %u time(s)), pinned %s, "
            "%u re-point(s) outside the interrupt so far (%u this time, %u off the game's frame), "
            "game's last frame %08X",
            g_reg_scanout, g_reg_flip, sp_flipctl_clears, g_pin_active ? "yes" : "no",
            g_pin_fixes_total + (g_pin_active ? g_pin_fixes : 0), g_pin_fixes,
            g_pin_game_fixes_total + (g_pin_active ? g_pin_game_fixes : 0), sp_pin_game);
}

}  // namespace overlay
