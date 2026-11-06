// SkyPortal360 open toolchain: the C string functions the plugin uses (xrt/xrt_libc.cpp).
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* memcpy(void* dst, const void* src, size_t n);
void* memmove(void* dst, const void* src, size_t n);
void* memset(void* dst, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
int strcmp(const char* a, const char* b);
char* strcpy(char* dst, const char* src);
char* strcat(char* dst, const char* src);
char* strncat(char* dst, const char* src, size_t n);
// MSVC CRT secure variants used by the plugin.
int strncpy_s(char* dst, size_t cap, const char* src, size_t count);
char* strtok_s(char* s, const char* delims, char** context);
int _stricmp(const char* a, const char* b);
int _strnicmp(const char* a, const char* b, size_t n);
int strncmp(const char* a, const char* b, size_t n);
char* strstr(const char* haystack, const char* needle);
#ifdef __cplusplus
}
#endif
