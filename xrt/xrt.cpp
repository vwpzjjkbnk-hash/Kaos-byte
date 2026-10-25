// SkyPortal360 open toolchain: the Win32/XAPI subset from xtl.h, implemented on kernel and XAM
// exports. Patterns follow openpak/x360-sdk (xsdk), whose versions of these calls were checked
// on retail consoles: NtCreateFile with SYNCHRONIZE + FILE_SYNCHRONOUS_IO_NONALERT, "drive:\"
// paths relative to OB_DOS_DEVICES, NtQueryDirectoryFile with the "*" pattern on the first call
// and a cleared buffer every call (FAT32 USB sticks leave the entry links unwritten), and
// ExCreateThread with a startup routine the kernel calls as startup(start, context).
#include <xtl.h>
#include <xkelib.h>

namespace {

// ---- Kernel types (layouts as Xenia and xsdk define them) ----

struct OBJECT_ATTRIBUTES {
  HANDLE root_directory;
  const ANSI_STRING* name;
  DWORD attributes;
};

struct IO_STATUS_BLOCK {
  NTSTATUS status;
  DWORD information;
};

struct FILE_NETWORK_OPEN_INFORMATION {
  ULONGLONG creation_time;
  ULONGLONG last_access_time;
  ULONGLONG last_write_time;
  ULONGLONG change_time;
  ULONGLONG allocation_size;
  ULONGLONG end_of_file;
  DWORD attributes;
  DWORD pad;
};

struct FILE_DIRECTORY_INFORMATION {
  DWORD next_entry_offset;
  DWORD file_index;
  ULONGLONG creation_time;
  ULONGLONG last_access_time;
  ULONGLONG last_write_time;
  ULONGLONG change_time;
  ULONGLONG end_of_file;
  ULONGLONG allocation_size;
  DWORD attributes;
  DWORD file_name_length;
  char file_name[1];
};

struct FILE_POSITION_INFORMATION {
  ULONGLONG current_byte_offset;
};

const HANDLE kObDosDevices = (HANDLE)0xFFFFFFFD;  // root for "drive:\..." names
const DWORD kSynchronize = 0x00100000;
const DWORD kFileOpen = 1, kFileCreate = 2, kFileOpenIf = 3, kFileOverwriteIf = 5;
const DWORD kFileDirectoryFile = 0x00000001;
const DWORD kFileSynchronousIoNonalert = 0x00000020;
const DWORD kFileNonDirectoryFile = 0x00000040;
const DWORD kClassPosition = 14;
const DWORD kClassNetworkOpen = 34;
const NTSTATUS kStatusNoMoreFiles = (NTSTATUS)0x80000006;

bool NtOk(NTSTATUS s) { return s >= 0; }

}  // namespace

// ---- The kernel imports this file uses (generated wrappers in imports.S) ----
extern "C" {
NTSTATUS NtCreateFile(HANDLE* handle, DWORD access, const OBJECT_ATTRIBUTES* attributes,
                      IO_STATUS_BLOCK* io, const ULONGLONG* allocation, DWORD file_attributes,
                      DWORD share, DWORD disposition, DWORD options);
NTSTATUS NtReadFile(HANDLE file, HANDLE event, void* apc, void* apc_context, IO_STATUS_BLOCK* io,
                    void* buffer, DWORD length, const ULONGLONG* offset);
NTSTATUS NtWriteFile(HANDLE file, HANDLE event, void* apc, void* apc_context, IO_STATUS_BLOCK* io,
                     const void* buffer, DWORD length, const ULONGLONG* offset);
NTSTATUS NtClose(HANDLE handle);
NTSTATUS NtFlushBuffersFile(HANDLE file, IO_STATUS_BLOCK* io);
NTSTATUS NtQueryInformationFile(HANDLE file, IO_STATUS_BLOCK* io, void* info, DWORD length,
                                DWORD info_class);
NTSTATUS NtSetInformationFile(HANDLE file, IO_STATUS_BLOCK* io, const void* info, DWORD length,
                              DWORD info_class);
NTSTATUS NtQueryFullAttributesFile(const OBJECT_ATTRIBUTES* attributes,
                                   FILE_NETWORK_OPEN_INFORMATION* info);
NTSTATUS NtQueryDirectoryFile(HANDLE file, HANDLE event, void* apc, void* apc_context,
                              IO_STATUS_BLOCK* io, void* info, DWORD length,
                              const ANSI_STRING* pattern, DWORD restart);
NTSTATUS KeDelayExecutionThread(DWORD mode, DWORD alertable, const LONGLONG* interval);
void KeQuerySystemTime(ULONGLONG* time);
void ExTerminateThread(DWORD exit_code);
void RtlInitializeCriticalSection(CRITICAL_SECTION* cs);
void RtlEnterCriticalSection(CRITICAL_SECTION* cs);
void RtlLeaveCriticalSection(CRITICAL_SECTION* cs);
DWORD XamInputGetState(DWORD user_index, DWORD flags, XINPUT_STATE* state);
NTSTATUS DbgPrint(const char* format, const char* text);  // called with "%s" only
}

namespace {

// "SKYHDD:\SkyPortal\x" -> name + root; plain kernel paths ("\Device\...") use no root.
void MakeName(const char* path, ANSI_STRING* name, OBJECT_ATTRIBUTES* oa) {
  const size_t n = strlen(path);
  name->Length = (WORD)n;
  name->MaximumLength = (WORD)(n + 1);
  name->Buffer = (char*)path;
  oa->root_directory = path[0] == '\\' ? 0 : kObDosDevices;
  oa->name = name;
  oa->attributes = 0;
}

NTSTATUS Open(const char* path, DWORD access, DWORD share, DWORD disposition, DWORD options,
              HANDLE* out) {
  ANSI_STRING name;
  OBJECT_ATTRIBUTES oa;
  MakeName(path, &name, &oa);
  IO_STATUS_BLOCK io = {0, 0};
  *out = 0;
  return NtCreateFile(out, access | kSynchronize, &oa, &io, 0, FILE_ATTRIBUTE_NORMAL, share,
                      disposition, options | kFileSynchronousIoNonalert);
}

bool QuerySize(HANDLE file, ULONGLONG* size) {
  FILE_NETWORK_OPEN_INFORMATION info;
  memset(&info, 0, sizeof(info));
  IO_STATUS_BLOCK io = {0, 0};
  if (!NtOk(NtQueryInformationFile(file, &io, &info, sizeof(info), kClassNetworkOpen))) {
    return false;
  }
  *size = info.end_of_file;
  return true;
}

// ---- FindFirstFile / FindNextFile state ----

struct FindState {
  bool used;
  HANDLE dir;
  bool restart;
  DWORD next_offset;  // kDone when the buffer has no entries left
  ULONGLONG buffer[(sizeof(FILE_DIRECTORY_INFORMATION) + MAX_PATH + 7) / 8];
};
const DWORD kDone = 0xFFFFFFFF;
// Open searches at once. The figure scan keeps one open per folder level (Figures, game, category
// and up to three more), so this must be more than the deepest folder: with 4, every figure four
// folders deep ("4. Trap Team\7) Traps\1) Crystal Traps\Air\...") was silently skipped.
enum { kMaxFinds = 12 };
FindState g_finds[kMaxFinds];
CRITICAL_SECTION g_find_lock;
bool g_find_lock_ready = false;

bool QueryEntries(FindState* f) {
  static const char kAll[] = "*";
  ANSI_STRING pattern;
  pattern.Length = 1;
  pattern.MaximumLength = 2;
  pattern.Buffer = (char*)kAll;
  memset(f->buffer, 0, sizeof(f->buffer));
  IO_STATUS_BLOCK io = {0, 0};
  const NTSTATUS s = NtQueryDirectoryFile(f->dir, 0, 0, 0, &io, f->buffer, sizeof(f->buffer),
                                          f->restart ? &pattern : 0, f->restart ? 1 : 0);
  f->restart = false;
  if (!NtOk(s) || s == kStatusNoMoreFiles) return false;
  f->next_offset = 0;
  return true;
}

bool NextEntry(FindState* f, WIN32_FIND_DATA* data) {
  if (f->next_offset == kDone && !QueryEntries(f)) return false;
  const FILE_DIRECTORY_INFORMATION* e =
      (const FILE_DIRECTORY_INFORMATION*)((const unsigned char*)f->buffer + f->next_offset);
  f->next_offset = e->next_entry_offset ? f->next_offset + e->next_entry_offset : kDone;
  DWORD len = e->file_name_length;
  if (len >= MAX_PATH) len = MAX_PATH - 1;
  memcpy(data->cFileName, e->file_name, len);
  data->cFileName[len] = 0;
  data->dwFileAttributes = e->attributes;
  data->nFileSizeLow = (DWORD)e->end_of_file;
  data->nFileSizeHigh = (DWORD)(e->end_of_file >> 32);
  return true;
}

// ms from 100 ns units without a 64-bit division helper: (t / 10000) = (t >> 4) / 625.
DWORD HundredNsToMs(ULONGLONG t) {
  ULONGLONG x = t >> 4;  // now divide by 625
  unsigned int hi = (unsigned int)(x >> 32), lo = (unsigned int)x;
  const unsigned int d = 625;
  unsigned int r = hi % d;
  const unsigned int q_hi = hi / d;
  (void)q_hi;  // only the low 32 bits of the result are returned (GetTickCount wraps)
  unsigned int mid = (r << 16) | (lo >> 16);
  const unsigned int q1 = mid / d;
  r = mid % d;
  unsigned int low = (r << 16) | (lo & 0xFFFF);
  const unsigned int q0 = low / d;
  return (q1 << 16) + q0;
}

// ---- Threads ----

// The kernel calls the startup routine (via xrt_thread_startup_guest in entry.S, which keeps the
// console's registers whole) as startup(start, context).
extern "C" void xrt_thread_startup(LPTHREAD_START_ROUTINE start, LPVOID context) {
  const DWORD code = start(context);
  ExTerminateThread(code);
}

// ---- Heap: a fixed arena for the few allocations the plugin makes at startup ----

enum { kArenaBytes = 128 * 1024 };  // the figure library needs ~117 KB
unsigned long long g_arena[kArenaBytes / 8];
size_t g_arena_used = 0;

}  // namespace (reopened below)

extern "C" DWORD MmAllocatePhysicalMemoryEx(DWORD flags, DWORD size, DWORD protect, DWORD min_address,
                                           DWORD max_address, DWORD alignment);
extern "C" void MmFreePhysicalMemory(DWORD type, DWORD address);

// Large buffers (the figure list, the drawn menu's frames and images) come from the kernel's
// physical-memory allocator, page-aligned: cached for work memory, write-combined for memory the
// display controller reads.
extern "C" void* xrt_big_alloc(size_t bytes, int write_combine) {
  const DWORD protect = 0x04 /* PAGE_READWRITE */ | (write_combine ? 0x400 /* WRITECOMBINE */ : 0);
  return (void*)MmAllocatePhysicalMemoryEx(0, (DWORD)((bytes + 4095) & ~(size_t)4095), protect, 0,
                                           0xFFFFFFFF, 4096);
}

extern "C" void xrt_big_free(void* p) {
  if (p) MmFreePhysicalMemory(0, (DWORD)p);
}

extern "C" NTSTATUS NtAllocateVirtualMemory(DWORD* base, DWORD* size, DWORD type, DWORD protect,
                                            DWORD debug_memory);
extern "C" NTSTATUS NtFreeVirtualMemory(DWORD* base, DWORD* size, DWORD type, DWORD debug_memory);

// Ordinary (virtual) memory in 4 KB pages: unlike xrt_big_alloc it doesn't need one unbroken
// stretch of physical memory, so it still works when a running game has used or split most of it.
// The CPU only (not for anything the GPU or the display reads). Addresses are below 0x80000000.
extern "C" void* xrt_virt_alloc(size_t bytes) {
  DWORD base = 0, size = (DWORD)((bytes + 4095) & ~(size_t)4095);
  if (!size) return 0;
  if (!NtOk(NtAllocateVirtualMemory(&base, &size, 0x1000 | 0x2000 /* MEM_COMMIT | MEM_RESERVE */,
                                    0x04 /* PAGE_READWRITE */, 0))) {
    return 0;
  }
  return (void*)base;
}

extern "C" void xrt_virt_free(void* p) {
  if (!p) return;
  DWORD base = (DWORD)p, size = 0;
  NtFreeVirtualMemory(&base, &size, 0x8000 /* MEM_RELEASE */, 0);
}

namespace {

void* ArenaAlloc(size_t bytes) {
  bytes = (bytes + 15) & ~(size_t)15;
  if (bytes > 16 * 1024) return xrt_big_alloc(bytes, 0);  // never freed: startup tables
  if (g_arena_used + bytes > sizeof(g_arena)) return xrt_big_alloc(bytes, 0);
  void* p = (unsigned char*)g_arena + g_arena_used;
  g_arena_used += bytes;
  return p;
}

}  // namespace

extern "C" {

// ---- Critical sections ----

void InitializeCriticalSection(CRITICAL_SECTION* cs) { RtlInitializeCriticalSection(cs); }
void DeleteCriticalSection(CRITICAL_SECTION* cs) { (void)cs; }
void EnterCriticalSection(CRITICAL_SECTION* cs) { RtlEnterCriticalSection(cs); }
void LeaveCriticalSection(CRITICAL_SECTION* cs) { RtlLeaveCriticalSection(cs); }

// ---- Files ----

HANDLE CreateFile(const char* path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                  DWORD disposition, DWORD attributes, HANDLE template_file) {
  (void)sa;
  (void)attributes;
  (void)template_file;
  DWORD nt_disposition;
  switch (disposition) {
    case CREATE_NEW: nt_disposition = kFileCreate; break;
    case CREATE_ALWAYS: nt_disposition = kFileOverwriteIf; break;
    case OPEN_ALWAYS: nt_disposition = kFileOpenIf; break;
    case OPEN_EXISTING:
    default: nt_disposition = kFileOpen; break;
  }
  HANDLE h = 0;
  if (!NtOk(Open(path, access, share, nt_disposition, kFileNonDirectoryFile, &h)) || !h) {
    return INVALID_HANDLE_VALUE;
  }
  return h;
}

BOOL ReadFile(HANDLE file, void* buffer, DWORD bytes, DWORD* done, void* overlapped) {
  (void)overlapped;
  IO_STATUS_BLOCK io = {0, 0};
  const NTSTATUS s = NtReadFile(file, 0, 0, 0, &io, buffer, bytes, 0);
  if (done) *done = NtOk(s) ? io.information : 0;
  return NtOk(s) ? TRUE : FALSE;
}

BOOL WriteFile(HANDLE file, const void* buffer, DWORD bytes, DWORD* done, void* overlapped) {
  (void)overlapped;
  IO_STATUS_BLOCK io = {0, 0};
  const NTSTATUS s = NtWriteFile(file, 0, 0, 0, &io, buffer, bytes, 0);
  if (done) *done = NtOk(s) ? io.information : 0;
  return NtOk(s) ? TRUE : FALSE;
}

BOOL FlushFileBuffers(HANDLE file) {
  IO_STATUS_BLOCK io = {0, 0};
  return NtOk(NtFlushBuffersFile(file, &io)) ? TRUE : FALSE;
}

BOOL CloseHandle(HANDLE handle) {
  if (!handle || handle == INVALID_HANDLE_VALUE) return FALSE;
  return NtOk(NtClose(handle)) ? TRUE : FALSE;
}

DWORD GetFileSize(HANDLE file, DWORD* high) {
  ULONGLONG size = 0;
  if (!QuerySize(file, &size)) {
    if (high) *high = 0;
    return 0xFFFFFFFF;
  }
  if (high) *high = (DWORD)(size >> 32);
  return (DWORD)size;
}

DWORD SetFilePointer(HANDLE file, LONG distance, LONG* distance_high, DWORD method) {
  LONGLONG offset = distance;
  if (distance_high) offset = ((LONGLONG)*distance_high << 32) | (DWORD)distance;
  LONGLONG base = 0;
  IO_STATUS_BLOCK io = {0, 0};
  if (method == FILE_END) {
    ULONGLONG size = 0;
    if (!QuerySize(file, &size)) return 0xFFFFFFFF;
    base = (LONGLONG)size;
  } else if (method == FILE_CURRENT) {
    FILE_POSITION_INFORMATION pos = {0};
    if (!NtOk(NtQueryInformationFile(file, &io, &pos, sizeof(pos), kClassPosition))) {
      return 0xFFFFFFFF;
    }
    base = (LONGLONG)pos.current_byte_offset;
  }
  FILE_POSITION_INFORMATION pos;
  pos.current_byte_offset = (ULONGLONG)(base + offset);
  if (!NtOk(NtSetInformationFile(file, &io, &pos, sizeof(pos), kClassPosition))) {
    return 0xFFFFFFFF;
  }
  if (distance_high) *distance_high = (LONG)(pos.current_byte_offset >> 32);
  return (DWORD)pos.current_byte_offset;
}

DWORD GetFileAttributes(const char* path) {
  ANSI_STRING name;
  OBJECT_ATTRIBUTES oa;
  MakeName(path, &name, &oa);
  FILE_NETWORK_OPEN_INFORMATION info;
  memset(&info, 0, sizeof(info));
  if (!NtOk(NtQueryFullAttributesFile(&oa, &info))) return INVALID_FILE_ATTRIBUTES;
  return info.attributes;
}

BOOL CreateDirectory(const char* path, LPSECURITY_ATTRIBUTES sa) {
  (void)sa;
  HANDLE h = 0;
  if (!NtOk(Open(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, kFileCreate,
                 kFileDirectoryFile, &h))) {
    return FALSE;
  }
  NtClose(h);
  return TRUE;
}

// Supports what the plugin passes: "<dir>\*".
HANDLE FindFirstFile(const char* pattern, WIN32_FIND_DATA* data) {
  char dir[MAX_PATH];
  strncpy_s(dir, sizeof(dir), pattern, _TRUNCATE);
  char* slash = strrchr(dir, '\\');
  if (!slash) return INVALID_HANDLE_VALUE;
  *slash = 0;
  HANDLE h = 0;
  if (!NtOk(Open(dir, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, kFileOpen,
                 kFileDirectoryFile, &h))) {
    return INVALID_HANDLE_VALUE;
  }
  if (!g_find_lock_ready) {  // first use is from the plugin's one worker thread
    InitializeCriticalSection(&g_find_lock);
    g_find_lock_ready = true;
  }
  FindState* f = 0;
  EnterCriticalSection(&g_find_lock);
  for (int i = 0; i < kMaxFinds; ++i) {
    if (!g_finds[i].used) {
      f = &g_finds[i];
      f->used = true;
      break;
    }
  }
  LeaveCriticalSection(&g_find_lock);
  if (!f) {
    NtClose(h);
    return INVALID_HANDLE_VALUE;
  }
  f->dir = h;
  f->restart = true;
  f->next_offset = kDone;
  if (!NextEntry(f, data)) {
    FindClose((HANDLE)f);
    return INVALID_HANDLE_VALUE;
  }
  return (HANDLE)f;
}

BOOL FindNextFile(HANDLE find, WIN32_FIND_DATA* data) {
  FindState* f = (FindState*)find;
  if (!f || find == INVALID_HANDLE_VALUE || !f->used) return FALSE;
  return NextEntry(f, data) ? TRUE : FALSE;
}

BOOL FindClose(HANDLE find) {
  FindState* f = (FindState*)find;
  if (!f || find == INVALID_HANDLE_VALUE || !f->used) return FALSE;
  NtClose(f->dir);
  f->dir = 0;
  f->used = false;
  return TRUE;
}

// ---- Time, debug output, threads, atomics, input ----

void Sleep(DWORD ms) {
  const LONGLONG interval = -(LONGLONG)ms * 10000;  // relative, 100 ns units
  KeDelayExecutionThread(1 /* UserMode */, 0, &interval);
}

DWORD GetTickCount(void) {
  ULONGLONG t = 0;
  KeQuerySystemTime(&t);
  return HundredNsToMs(t);
}

void OutputDebugStringA(const char* text) { DbgPrint("%s", text); }

HANDLE CreateThread(LPSECURITY_ATTRIBUTES sa, size_t stack, LPTHREAD_START_ROUTINE start,
                    LPVOID param, DWORD flags, LPDWORD thread_id) {
  (void)sa;
  HANDLE h = 0;
  DWORD id = 0;
  const DWORD ex_flags = (flags & CREATE_SUSPENDED) ? EX_CREATE_FLAG_SUSPENDED : 0;
  if (!NtOk((NTSTATUS)ExCreateThread(&h, (DWORD)stack, &id, (PVOID)xrt_thread_startup_guest,
                                     start, param, ex_flags))) {
    return 0;
  }
  if (thread_id) *thread_id = id;
  return h;
}

LONG InterlockedIncrement(LONG volatile* value) {
  return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST);
}

DWORD XInputGetState(DWORD user_index, XINPUT_STATE* state) {
  return XamInputGetState(user_index, 0, state);
}

// ---- C++ runtime support ----

void* __xrt_alloc(size_t bytes) { return ArenaAlloc(bytes); }

int __cxa_atexit(void (*fn)(void*), void* arg, void* dso) {
  // A title DLL's static objects live until the process ends; nothing is registered.
  (void)fn;
  (void)arg;
  (void)dso;
  return 0;
}
void* __dso_handle = 0;
void __cxa_pure_virtual(void) {
  for (;;) {
  }
}

}  // extern "C"

void* operator new(size_t bytes) { return ArenaAlloc(bytes); }
void* operator new[](size_t bytes) { return ArenaAlloc(bytes); }
void operator delete(void*) {}
void operator delete[](void*) {}
void operator delete(void*, size_t) {}
void operator delete[](void*, size_t) {}
