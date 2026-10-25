// SkyPortal360 open toolchain: stand-in for the Xbox 360 XDK's <xtl.h>.
//
// Declares the small Win32/XAPI subset the plugin uses. xrt.cpp implements it directly on kernel
// and XAM exports (no Microsoft code), so the plugin builds with clang + lld instead of the XDK.
// Types follow the XDK's sizes on the console: 32-bit DWORD/pointers, 16-bit wchar_t
// (build with -fshort-wchar).
#pragma once

#include <stddef.h>
#include <stdarg.h>

#if !defined(__WCHAR_MAX__) || __WCHAR_MAX__ > 0xFFFF
#error "build with -fshort-wchar: the console's wchar_t is 16 bits"
#endif

typedef unsigned long DWORD, *LPDWORD, *PDWORD;
typedef unsigned short WORD, *PWORD;
typedef unsigned char BYTE, *PBYTE;
typedef unsigned char UCHAR;
typedef long LONG, NTSTATUS, HRESULT;
typedef unsigned long ULONG;
typedef int BOOL;
typedef void VOID, *PVOID, *LPVOID, *HANDLE, **PHANDLE;
typedef char CHAR, *PSZ, *PCHAR;
typedef const char* PCSZ;
typedef wchar_t WCHAR, *PWCHAR;
typedef unsigned long long ULONGLONG;
typedef long long LONGLONG;
typedef struct _SECURITY_ATTRIBUTES { DWORD unused; } SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);

#define WINAPI
#define APIENTRY
#define NTAPI
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define ERROR_SUCCESS 0L
#define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)

#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define CREATE_SUSPENDED 0x00000004

#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3

#define _TRUNCATE ((size_t)-1)

typedef struct _WIN32_FIND_DATA {
  DWORD dwFileAttributes;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  char cFileName[MAX_PATH];
} WIN32_FIND_DATA;

// Same size as the kernel's RTL_CRITICAL_SECTION (7 words); the kernel owns the layout.
typedef struct _CRITICAL_SECTION {
  DWORD opaque[7];
} __attribute__((aligned(8))) CRITICAL_SECTION;

#define XINPUT_GAMEPAD_DPAD_UP 0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN 0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT 0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT 0x0008
#define XINPUT_GAMEPAD_START 0x0010
#define XINPUT_GAMEPAD_BACK 0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB 0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB 0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER 0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XINPUT_GAMEPAD_A 0x1000
#define XINPUT_GAMEPAD_B 0x2000
#define XINPUT_GAMEPAD_X 0x4000
#define XINPUT_GAMEPAD_Y 0x8000

typedef struct _XINPUT_GAMEPAD {
  WORD wButtons;
  BYTE bLeftTrigger;
  BYTE bRightTrigger;
  short sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} XINPUT_GAMEPAD;

typedef struct _XINPUT_STATE {
  DWORD dwPacketNumber;
  XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE, *PXINPUT_STATE;

#ifdef __cplusplus
extern "C" {
#endif

void InitializeCriticalSection(CRITICAL_SECTION* cs);
void DeleteCriticalSection(CRITICAL_SECTION* cs);
void EnterCriticalSection(CRITICAL_SECTION* cs);
void LeaveCriticalSection(CRITICAL_SECTION* cs);

HANDLE CreateFile(const char* path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                  DWORD disposition, DWORD attributes, HANDLE template_file);
BOOL ReadFile(HANDLE file, void* buffer, DWORD bytes, DWORD* done, void* overlapped);
BOOL WriteFile(HANDLE file, const void* buffer, DWORD bytes, DWORD* done, void* overlapped);
BOOL FlushFileBuffers(HANDLE file);
BOOL CloseHandle(HANDLE handle);
DWORD GetFileSize(HANDLE file, DWORD* high);
DWORD SetFilePointer(HANDLE file, LONG distance, LONG* distance_high, DWORD method);
DWORD GetFileAttributes(const char* path);
BOOL CreateDirectory(const char* path, LPSECURITY_ATTRIBUTES sa);
HANDLE FindFirstFile(const char* pattern, WIN32_FIND_DATA* data);
BOOL FindNextFile(HANDLE find, WIN32_FIND_DATA* data);
BOOL FindClose(HANDLE find);

void Sleep(DWORD ms);
DWORD GetTickCount(void);
void OutputDebugStringA(const char* text);
HANDLE CreateThread(LPSECURITY_ATTRIBUTES sa, size_t stack, LPTHREAD_START_ROUTINE start,
                    LPVOID param, DWORD flags, LPDWORD thread_id);
LONG InterlockedIncrement(LONG volatile* value);

DWORD XInputGetState(DWORD user_index, XINPUT_STATE* state);

#ifdef __cplusplus
}
#endif

#include <string.h>
#include <stdio.h>
