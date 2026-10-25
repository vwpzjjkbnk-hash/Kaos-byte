// SkyPortal360 - low-level helpers: logging, drive mounting, toasts, import patching.
#pragma once

#include "stdafx.h"

namespace util {

// ---- Logging (SkyPortal\skyportal.log, truncated each boot) ----
void LogOpen(const char* path);
void Log(const char* fmt, ...);

// ---- Load trace (Peer Pressure build) ----
// Appends a line to Hdd1:\SkyPortal_trace.txt (written by device path, so it works from DllMain
// before any drive letter exists). Shows how far the DLL got if it never reaches its log.
void Trace(const char* fmt, ...);

// ---- Drives ----
// Creates a drive letter, e.g. MountDrive("SKYHDD:", "\\Device\\Harddisk0\\Partition1", true).
// system_link: true for a system-wide link (Dashlaunch plugin, a system DLL), false for one
// visible to the current title only (Peer Pressure sideloaded title DLL).
bool MountDrive(const char* drive, const char* device, bool system_link);
bool FileExists(const char* path);
bool DirExists(const char* path);
// Reads a whole file of exactly `size` bytes. False if missing or a different size.
bool ReadExact(const char* path, void* out, DWORD size);
// Reads up to `cap` bytes; returns bytes read or -1.
int ReadAll(const char* path, char* out, int cap);
// Creates/overwrites `path` with `size` bytes. If `fail_if_exists`, never overwrites.
bool WriteAll(const char* path, const void* data, DWORD size, bool fail_if_exists);

// ---- On-screen toast (the Guide notification pop-up) ----
void Notify(const wchar_t* fmt, ...);
void NotifyA(const char* fmt, ...);  // ASCII convenience

// ---- XEX import patching ----
// Address of `ordinal` exported by `module` ("xam.xex", "xboxkrnl.exe"), or 0.
DWORD ResolveExport(const char* module, DWORD ordinal);
// Points every import of `orig_addr` in `module`'s import tables at `hook_addr` (both the
// import variable and its call stub). Idempotent: imports already pointing at the hook are
// skipped. Returns how many imports were newly patched.
int PatchModuleImport(PLDR_DATA_TABLE_ENTRY module, DWORD orig_addr, DWORD hook_addr);
// Copies the current value of every import record slot of `module` (variables and the first
// word of each stub) into `out`. Returns how many were written.
int CollectImportSlots(PLDR_DATA_TABLE_ENTRY module, DWORD* out, int cap);
// The running title's main module (NULL while none is loaded).
PLDR_DATA_TABLE_ENTRY TitleModule();

// Reads a module's XEX execution ID (what Peer Pressure's sideload .ini matches against).
// `version` is the packed XEX version: major<<28 | minor<<24 | build<<8 | qfe.
struct ExecutionId {
  DWORD title_id;
  DWORD media_id;
  DWORD version;
};
bool ReadExecutionId(PLDR_DATA_TABLE_ENTRY module, ExecutionId* out);

}  // namespace util
