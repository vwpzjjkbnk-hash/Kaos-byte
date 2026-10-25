// SYNTAX-CHECK STUB ONLY. Declarations copied in shape from xkelib (kernel\_kernelext.h,
// kernel\keXexInfo.h, xam\_xamext.h, xapiext.h) for the handful of exports the plugin uses.
#pragma once
#define OBJ_SYS_STRING "\\System??\\%s"
#define EX_CREATE_FLAG_SUSPENDED 0x1
#define EX_CREATE_FLAG_SYSTEM 0x2
typedef struct _STRING { WORD Length; WORD MaximumLength; char* Buffer; } STRING, *PSTRING, ANSI_STRING, *PANSI_STRING;
typedef struct _HV_IMAGE_IMPORT_TABLE { BYTE NextImportDigest[0x14]; DWORD ModuleNumber; DWORD Version[2]; BYTE Unused; BYTE ModuleIndex; WORD ImportCount; } HV_IMAGE_IMPORT_TABLE;
typedef struct _XEX_IMPORT_TABLE_ORG { DWORD TableSize; HV_IMAGE_IMPORT_TABLE ImportTable; } XEX_IMPORT_TABLE_ORG, *PXEX_IMPORT_TABLE_ORG;
typedef struct _XEX_IMPORT_DESCRIPTOR { DWORD Size; DWORD NameTableSize; DWORD ModuleCount; } XEX_IMPORT_DESCRIPTOR, *PXEX_IMPORT_DESCRIPTOR;
typedef struct _LDR_DATA_TABLE_ENTRY { BYTE pad[0x58]; PVOID XexHeaderBase; } LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;
typedef enum { XNOTIFYUI_TYPE_PREFERRED_REVIEW = 14 } XNOTIFYQUEUEUI_TYPE;
enum { XNOTIFYUI_PRIORITY_HIGH = 2 };
extern "C" {
DWORD ExCreateThread(PHANDLE, DWORD, LPDWORD, PVOID, LPTHREAD_START_ROUTINE, LPVOID, DWORD);
HRESULT ObCreateSymbolicLink(PSTRING, PSTRING);
HRESULT ObDeleteSymbolicLink(PSTRING);
PVOID RtlImageXexHeaderField(PVOID, DWORD);
VOID RtlInitAnsiString(PANSI_STRING, PCSZ);
extern PLDR_DATA_TABLE_ENTRY* XexExecutableModuleHandle;
NTSTATUS XexGetModuleHandle(PSZ, PHANDLE);
NTSTATUS XexGetProcedureAddress(HANDLE, DWORD, PVOID);
DWORD XamGetCurrentTitleId(VOID);
VOID XNotifyQueueUI(XNOTIFYQUEUEUI_TYPE, DWORD, ULONGLONG, PWCHAR, PVOID);
}
VOID XapiThreadStartup(VOID (__cdecl *StartRoutine)(VOID *), PVOID StartContext, DWORD dwExitCode);
