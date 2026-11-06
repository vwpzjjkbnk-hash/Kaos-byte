#pragma once
#include <stddef.h>
extern "C" { void* memcpy(void*, const void*, size_t); void* memmove(void*, const void*, size_t); void* memset(void*, int, size_t); int memcmp(const void*, const void*, size_t);
size_t strlen(const char*); char* strchr(const char*, int); char* strrchr(const char*, int);
char* strstr(const char* haystack, const char* needle);
int strcmp(const char*, const char*); char* strcpy(char*, const char*); char* strcat(char*, const char*);
char* strncat(char*, const char*, size_t); int strncmp(const char*, const char*, size_t);
int _strnicmp(const char*, const char*, size_t); }
