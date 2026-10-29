// SkyPortal360 open toolchain: stand-in for xkelib.h (kernel / XAM export declarations).
//
// Each function here is an import from xboxkrnl.exe or xam.xex (ordinals in xrt/imports.txt,
// checked against Xenia's export tables). The generated wrappers in imports.S call them by the
// console's own convention: arguments zero-extended in r3-r10, a 9th in the 0x54(r1) slot, and a
// 0x60-byte frame so a callee that spills into its caller's argument slots can't reach ours.
#pragma once

#define OBJ_SYS_STRING "\\System??\\%s"
#define OBJ_USR_STRING "\\??\\%s"
#define EX_CREATE_FLAG_SUSPENDED 0x1
#define EX_CREATE_FLAG_SYSTEM 0x2

typedef struct _STRING {
  WORD Length;
  WORD MaximumLength;
  char* Buffer;
} STRING, *PSTRING, ANSI_STRING, *PANSI_STRING;

typedef struct _HV_IMAGE_IMPORT_TABLE {
  BYTE NextImportDigest[0x14];
  DWORD ModuleNumber;
  DWORD Version[2];
  BYTE Unused;
  BYTE ModuleIndex;
  WORD ImportCount;
} HV_IMAGE_IMPORT_TABLE;

typedef struct _XEX_IMPORT_TABLE_ORG {
  DWORD TableSize;
  HV_IMAGE_IMPORT_TABLE ImportTable;
} XEX_IMPORT_TABLE_ORG, *PXEX_IMPORT_TABLE_ORG;

typedef struct _XEX_IMPORT_DESCRIPTOR {
  DWORD Size;
  DWORD NameTableSize;
  DWORD ModuleCount;
} XEX_IMPORT_DESCRIPTOR, *PXEX_IMPORT_DESCRIPTOR;

// Only XexHeaderBase (offset 0x58, xkelib keXexInfo.h) is used.
typedef struct _LDR_DATA_TABLE_ENTRY {
  BYTE pad[0x58];
  PVOID XexHeaderBase;
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

typedef enum { XNOTIFYUI_TYPE_PREFERRED_REVIEW = 14 } XNOTIFYQUEUEUI_TYPE;
enum { XNOTIFYUI_PRIORITY_HIGH = 2 };

#ifdef __cplusplus
extern "C" {
#endif

DWORD ExCreateThread(PHANDLE handle, DWORD stack, LPDWORD thread_id, PVOID api_startup,
                     LPTHREAD_START_ROUTINE start, LPVOID param, DWORD flags);
HRESULT ObCreateSymbolicLink(PSTRING link, PSTRING device);
HRESULT ObDeleteSymbolicLink(PSTRING link);
PVOID RtlImageXexHeaderField(PVOID header_base, DWORD field);
VOID RtlInitAnsiString(PANSI_STRING dest, PCSZ source);
// Kernel variable: the import slot holds its address; *XexExecutableModuleHandle is the
// running title's loader entry.
extern PLDR_DATA_TABLE_ENTRY* const XexExecutableModuleHandle;
NTSTATUS XexGetModuleHandle(PSZ name, PHANDLE handle);
NTSTATUS XexGetProcedureAddress(HANDLE module, DWORD ordinal, PVOID address);
BOOL MmIsAddressValid(PVOID address);
DWORD XamGetCurrentTitleId(VOID);

// XAM's qwAreas is a 64-bit argument the console passes whole in one register (r5); a 32-bit
// value, zero-extended by the wrapper, is that same register.
VOID XNotifyQueueUI_raw(DWORD type, DWORD user_index, DWORD areas, PWCHAR text, PVOID context);
static inline VOID XNotifyQueueUI(XNOTIFYQUEUEUI_TYPE type, DWORD user_index, ULONGLONG areas,
                                  PWCHAR text, PVOID context) {
  XNotifyQueueUI_raw((DWORD)type, user_index, (DWORD)areas, text, context);
}

// Thread startup routine the kernel calls as startup(start, param); keeps the console's
// registers whole around the plugin's code (xrt/entry.S).
void xrt_thread_startup_guest(void);
#define XapiThreadStartup xrt_thread_startup_guest

#ifdef __cplusplus
}
#endif
