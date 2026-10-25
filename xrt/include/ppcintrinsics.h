// SkyPortal360 open toolchain: the XDK PowerPC intrinsics the plugin uses, as inline asm.
// clang predefines some of these names as XL-compatible builtins with other signatures, so they
// are undefined first.
#pragma once

#undef __dcbst
#undef __icbi
#undef __sync
#undef __isync
#undef __lwsync

static inline void xrt_dcbst(int offset, const void* p) {
  __asm__ volatile("dcbst %0,%1" : : "b"(offset), "r"(p) : "memory");
}
static inline void xrt_icbi(int offset, const void* p) {
  __asm__ volatile("icbi %0,%1" : : "b"(offset), "r"(p) : "memory");
}
static inline void xrt_sync(void) { __asm__ volatile("sync" : : : "memory"); }
static inline void xrt_isync(void) { __asm__ volatile("isync" : : : "memory"); }
static inline void xrt_lwsync(void) { __asm__ volatile("lwsync" : : : "memory"); }

#define __dcbst(offset, p) xrt_dcbst((offset), (p))
#define __icbi(offset, p) xrt_icbi((offset), (p))
#define __sync() xrt_sync()
#define __isync() xrt_isync()
#define __lwsync() xrt_lwsync()
