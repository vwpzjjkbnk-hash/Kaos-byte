// SkyPortal360 - low-level helpers. See util.h.
#include "util.h"

#include <stdarg.h>

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

namespace util {

// --------------------------------------------------------------------------------------------
// Logging
// --------------------------------------------------------------------------------------------

namespace {
char g_log_path[MAX_PATH] = {0};
sp::Lock g_log_lock;
const DWORD kMaxLogBytes = 256 * 1024;
DWORD g_log_bytes = 0;
}  // namespace

void LogOpen(const char* path) {
  sp::Guard g(g_log_lock);
  strncpy_s(g_log_path, sizeof(g_log_path), path, _TRUNCATE);
  HANDLE h = CreateFile(g_log_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
  if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
  g_log_bytes = 0;
}

void Log(const char* fmt, ...) {
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  int n = _vsnprintf_s(line, sizeof(line) - 2, _TRUNCATE, fmt, ap);
  va_end(ap);
  if (n < 0) n = (int)strlen(line);
  line[n++] = '\r';
  line[n++] = '\n';
  line[n] = 0;
  OutputDebugStringA(line);  // visible with a debugger / xbWatson

  sp::Guard g(g_log_lock);
  if (!g_log_path[0] || g_log_bytes > kMaxLogBytes) return;
  HANDLE h = CreateFile(g_log_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  SetFilePointer(h, 0, NULL, FILE_END);
  DWORD written = 0;
  WriteFile(h, line, (DWORD)n, &written, NULL);
  CloseHandle(h);
  g_log_bytes += written;
}

// --------------------------------------------------------------------------------------------
// Load trace
// --------------------------------------------------------------------------------------------

namespace {
char g_trace[4096];
int g_trace_len = 0;
}  // namespace

void Trace(const char* fmt, ...) {
#if defined(SKYPORTAL_SIDELOAD)
  va_list ap;
  va_start(ap, fmt);
  char line[256];
  int n = _vsnprintf_s(line, sizeof(line) - 2, _TRUNCATE, fmt, ap);
  va_end(ap);
  if (n < 0) n = (int)strlen(line);
  line[n++] = '\r';
  line[n++] = '\n';
  if (g_trace_len + n >= (int)sizeof(g_trace)) return;
  memcpy(g_trace + g_trace_len, line, n);
  g_trace_len += n;
  // Rewrite the whole (small) file each time: no seeking, and it survives a crash mid-startup.
  HANDLE h = CreateFile("\\Device\\Harddisk0\\Partition1\\StaticRift_trace.txt", GENERIC_WRITE, 0,
                        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(h, g_trace, (DWORD)g_trace_len, &written, NULL);
  CloseHandle(h);
#else
  (void)fmt;
#endif
}

// --------------------------------------------------------------------------------------------
// Drives and files
// --------------------------------------------------------------------------------------------

bool MountDrive(const char* drive, const char* device, bool system_link) {
  char link[MAX_PATH];
  // "\\System??\\%s" (all processes) or "\\??\\%s" (this title only)
  sprintf_s(link, sizeof(link), system_link ? "\\System??\\%s" : "\\??\\%s", drive);
  STRING link_name, device_name;
  RtlInitAnsiString(&link_name, link);
  RtlInitAnsiString(&device_name, device);
  ObDeleteSymbolicLink(&link_name);
  return ObCreateSymbolicLink(&link_name, &device_name) >= 0;
}

bool FileExists(const char* path) {
  const DWORD a = GetFileAttributes(path);
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const char* path) {
  const DWORD a = GetFileAttributes(path);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadExact(const char* path, void* out, DWORD size) {
  HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return false;
  bool ok = false;
  if (GetFileSize(h, NULL) == size) {
    DWORD got = 0;
    ok = ReadFile(h, out, size, &got, NULL) && got == size;
  }
  CloseHandle(h);
  return ok;
}

int ReadAll(const char* path, char* out, int cap) {
  HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return -1;
  DWORD got = 0;
  const BOOL ok = ReadFile(h, out, (DWORD)cap, &got, NULL);
  CloseHandle(h);
  return ok ? (int)got : -1;
}

bool WriteAll(const char* path, const void* data, DWORD size, bool fail_if_exists) {
  HANDLE h = CreateFile(path, GENERIC_WRITE, 0, NULL, fail_if_exists ? CREATE_NEW : CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL ok = WriteFile(h, data, size, &written, NULL);
  FlushFileBuffers(h);
  CloseHandle(h);
  return ok && written == size;
}

// --------------------------------------------------------------------------------------------
// Toasts
// --------------------------------------------------------------------------------------------

void Notify(const wchar_t* fmt, ...) {
  wchar_t text[256];
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf_s(text, 256, _TRUNCATE, fmt, ap);
  va_end(ap);
  XNotifyQueueUI(XNOTIFYUI_TYPE_PREFERRED_REVIEW, 0, XNOTIFYUI_PRIORITY_HIGH, text, NULL);
}

void NotifyA(const char* fmt, ...) {
  char text[256];
  va_list ap;
  va_start(ap, fmt);
  _vsnprintf_s(text, sizeof(text), _TRUNCATE, fmt, ap);
  va_end(ap);
  wchar_t wide[256];
  int i = 0;
  for (; text[i] && i < 255; ++i) wide[i] = (wchar_t)(unsigned char)text[i];
  wide[i] = 0;
  XNotifyQueueUI(XNOTIFYUI_TYPE_PREFERRED_REVIEW, 0, XNOTIFYUI_PRIORITY_HIGH, wide, NULL);
}

// --------------------------------------------------------------------------------------------
// Import patching
// --------------------------------------------------------------------------------------------

namespace {

// Makes freshly written instructions/data visible to instruction fetch on every core.
void FlushRange(void* p, DWORD bytes) {
  BYTE* start = (BYTE*)((DWORD)p & ~(DWORD)127);
  BYTE* end = (BYTE*)p + bytes;
  for (BYTE* line = start; line < end; line += 128) {
    __dcbst(0, line);
  }
  __sync();
  for (BYTE* line = start; line < end; line += 128) {
    __icbi(0, line);
  }
  __isync();
}

// lis r11,hi ; addi r11,r11,lo ; mtctr r11 ; bctr
void WriteJump(DWORD* at, DWORD dest) {
  DWORD hi = (dest >> 16) & 0xFFFF;
  if (dest & 0x8000) hi += 1;  // addi sign-extends the low half
  at[0] = 0x3D600000 | hi;
  at[1] = 0x396B0000 | (dest & 0xFFFF);
  at[2] = 0x7D6903A6;
  at[3] = 0x4E800420;
  FlushRange(at, 16);
}

}  // namespace

DWORD ResolveExport(const char* module, DWORD ordinal) {
  HANDLE h = NULL;
  if (XexGetModuleHandle((PSZ)module, &h) != 0 || h == NULL) return 0;
  DWORD addr = 0;
  if (XexGetProcedureAddress(h, ordinal, &addr) != 0) return 0;
  return addr;
}

PLDR_DATA_TABLE_ENTRY TitleModule() {
  if (!XexExecutableModuleHandle) return NULL;
  return *XexExecutableModuleHandle;
}

bool ReadExecutionId(PLDR_DATA_TABLE_ENTRY module, ExecutionId* out) {
  if (!module || !module->XexHeaderBase || !out) return false;
  // XEX_HEADER_EXECUTION_ID: key 0x400, 0x18-byte struct -> 0x00040006.
  // Layout (xkelib XEX_EXECUTION_ID): MediaID, Version, BaseVersion, TitleID, ...
  const DWORD* id = (const DWORD*)RtlImageXexHeaderField(module->XexHeaderBase, 0x00040006);
  if (!id) return false;
  out->media_id = id[0];
  out->version = id[1];
  out->title_id = id[3];
  return true;
}

int CollectImportSlots(PLDR_DATA_TABLE_ENTRY module, DWORD* out, int cap) {
  if (!module || !module->XexHeaderBase || !out) return 0;
  PXEX_IMPORT_DESCRIPTOR desc = (PXEX_IMPORT_DESCRIPTOR)RtlImageXexHeaderField(
      module->XexHeaderBase, XEX_HEADER_IMPORT_LIBRARIES);
  if (!desc) return 0;
  int n = 0;
  BYTE* names = (BYTE*)(desc + 1);
  PXEX_IMPORT_TABLE_ORG table = (PXEX_IMPORT_TABLE_ORG)(names + desc->NameTableSize);
  for (DWORD m = 0; m < desc->ModuleCount; ++m) {
    DWORD* records = (DWORD*)(table + 1);
    const WORD count = table->ImportTable.ImportCount;
    for (WORD y = 0; y < count && n < cap; ++y) {
      const DWORD* slot = (const DWORD*)records[y];
      if (slot) out[n++] = *slot;
    }
    if (!table->TableSize) break;
    table = (PXEX_IMPORT_TABLE_ORG)((BYTE*)table + table->TableSize);
  }
  return n;
}

int PatchModuleImport(PLDR_DATA_TABLE_ENTRY module, DWORD orig_addr, DWORD hook_addr) {
  if (!module || !module->XexHeaderBase || !orig_addr || !hook_addr) return 0;
  PXEX_IMPORT_DESCRIPTOR desc = (PXEX_IMPORT_DESCRIPTOR)RtlImageXexHeaderField(
      module->XexHeaderBase, XEX_HEADER_IMPORT_LIBRARIES);
  if (!desc) return 0;

  int patched = 0;
  BYTE* names = (BYTE*)(desc + 1);
  PXEX_IMPORT_TABLE_ORG table = (PXEX_IMPORT_TABLE_ORG)(names + desc->NameTableSize);
  for (DWORD m = 0; m < desc->ModuleCount; ++m) {
    DWORD* records = (DWORD*)(table + 1);
    const WORD count = table->ImportTable.ImportCount;
    // Function imports come in pairs: the import variable (holds the resolved address) followed
    // by the call stub the title branches to.
    for (WORD y = 0; y + 1 < count; ++y) {
      DWORD* var = (DWORD*)records[y];
      if (!var || *var != orig_addr) continue;
      *var = hook_addr;
      FlushRange(var, 4);
      WriteJump((DWORD*)records[y + 1], hook_addr);
      ++patched;
      ++y;  // skip the stub record we just rewrote
    }
    if (!table->TableSize) break;
    table = (PXEX_IMPORT_TABLE_ORG)((BYTE*)table + table->TableSize);
  }
  return patched;
}

}  // namespace util
