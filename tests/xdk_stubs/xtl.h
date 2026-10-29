// SYNTAX-CHECK STUB ONLY. Minimal stand-in for the Xbox 360 XDK's xtl.h so the plugin sources can
// be compiled with -fsyntax-only on a PC. Signatures mirror the XDK / Win32 ones the code uses.
#pragma once
#include <stddef.h>
#include <stdarg.h>
#include <wchar.h>
typedef unsigned long DWORD, *LPDWORD, *PDWORD;
typedef unsigned short WORD, *PWORD;
typedef unsigned char BYTE, *PBYTE;
typedef long LONG, NTSTATUS, HRESULT;
typedef int BOOL;
typedef void VOID, *PVOID, *LPVOID, *HANDLE, **PHANDLE;
typedef char CHAR, *PSZ, *PCHAR;
typedef const char* PCSZ;
typedef wchar_t WCHAR, *PWCHAR;
typedef unsigned long long ULONGLONG;
typedef struct { DWORD x; } SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);
#define WINAPI
#define APIENTRY
#define NTAPI
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define ERROR_SUCCESS 0L
#define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)
#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define FILE_SHARE_READ 1
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_END 2
#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0
#define _TRUNCATE ((size_t)-1)
typedef struct { DWORD dwFileAttributes; DWORD a[6]; DWORD nFileSizeHigh; DWORD nFileSizeLow; DWORD r[2]; char cFileName[MAX_PATH]; } WIN32_FIND_DATA;
typedef struct { void* p[6]; } CRITICAL_SECTION;
void InitializeCriticalSection(CRITICAL_SECTION*); void DeleteCriticalSection(CRITICAL_SECTION*);
void EnterCriticalSection(CRITICAL_SECTION*); void LeaveCriticalSection(CRITICAL_SECTION*);
HANDLE CreateFile(const char*, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
BOOL ReadFile(HANDLE, void*, DWORD, DWORD*, void*);
BOOL WriteFile(HANDLE, const void*, DWORD, DWORD*, void*);
BOOL FlushFileBuffers(HANDLE); BOOL CloseHandle(HANDLE);
DWORD GetFileSize(HANDLE, DWORD*); DWORD SetFilePointer(HANDLE, LONG, LONG*, DWORD);
DWORD GetFileAttributes(const char*); BOOL CreateDirectory(const char*, LPSECURITY_ATTRIBUTES);
HANDLE FindFirstFile(const char*, WIN32_FIND_DATA*); BOOL FindNextFile(HANDLE, WIN32_FIND_DATA*); BOOL FindClose(HANDLE);
void Sleep(DWORD); DWORD GetTickCount(); void OutputDebugStringA(const char*);
DWORD ResumeThread(HANDLE); DWORD XSetThreadProcessor(HANDLE, DWORD);
HANDLE CreateThread(LPSECURITY_ATTRIBUTES, size_t, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
LONG InterlockedIncrement(LONG volatile*);
int sprintf_s(char*, size_t, const char*, ...);
int _vsnprintf_s(char*, size_t, size_t, const char*, va_list);
int _vsnwprintf_s(wchar_t*, size_t, size_t, const wchar_t*, va_list);
int strncpy_s(char*, size_t, const char*, size_t);
char* strtok_s(char*, const char*, char**);
int _stricmp(const char*, const char*);
extern "C" int _strnicmp(const char*, const char*, size_t);
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
typedef struct { WORD wButtons; BYTE bLeftTrigger; BYTE bRightTrigger; short sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XINPUT_GAMEPAD;
typedef struct { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; } XINPUT_STATE, *PXINPUT_STATE;
DWORD XInputGetState(DWORD, XINPUT_STATE*);
#define MAXULONG_PTR 0xFFFFFFFFu
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOMBINE 0x400
#define MEM_LARGE_PAGES 0x20000000
void* XPhysicalAlloc(unsigned long, unsigned long, unsigned long, DWORD);
void XPhysicalFree(void*);
HANDLE GetCurrentThread(void);
BOOL SetThreadPriority(HANDLE thread, int priority);
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
