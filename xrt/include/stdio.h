// SkyPortal360 open toolchain: formatted output (xrt/xrt_printf.cpp). Supports %d %i %u %x %X
// %p %s %c %%, with flags '-', '0', width, precision for strings, and the l/ll/h size prefixes.
#pragma once
#include <stddef.h>
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
int vsnprintf(char* buf, size_t cap, const char* fmt, va_list ap);
int snprintf(char* buf, size_t cap, const char* fmt, ...);
int sprintf_s(char* buf, size_t cap, const char* fmt, ...);
int _vsnprintf_s(char* buf, size_t cap, size_t count, const char* fmt, va_list ap);
int _vsnwprintf_s(wchar_t* buf, size_t cap, size_t count, const wchar_t* fmt, va_list ap);
#ifdef __cplusplus
}
#endif
