// SkyPortal360 - virtual Portal of Power for Xbox 360 Skylanders games.
// Tiny lock wrapper so the portal core builds both in the Xbox 360 XDK (C++03-era MSVC 2010)
// and natively on a PC for the unit tests.
#pragma once

#if defined(_XBOX)
#include <xtl.h>
namespace sp {
class Lock {
 public:
  Lock() { InitializeCriticalSection(&cs_); }
  ~Lock() { DeleteCriticalSection(&cs_); }
  void Enter() { EnterCriticalSection(&cs_); }
  void Leave() { LeaveCriticalSection(&cs_); }
 private:
  CRITICAL_SECTION cs_;
  Lock(const Lock&);
  Lock& operator=(const Lock&);
};
}  // namespace sp
#else
#include <pthread.h>
namespace sp {
class Lock {
 public:
  Lock() {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m_, &a);
    pthread_mutexattr_destroy(&a);
  }
  ~Lock() { pthread_mutex_destroy(&m_); }
  void Enter() { pthread_mutex_lock(&m_); }
  void Leave() { pthread_mutex_unlock(&m_); }
 private:
  pthread_mutex_t m_;
  Lock(const Lock&);
  Lock& operator=(const Lock&);
};
}  // namespace sp
#endif

namespace sp {
// Scoped guard (no C++11 needed).
class Guard {
 public:
  explicit Guard(Lock& l) : l_(l) { l_.Enter(); }
  ~Guard() { l_.Leave(); }
 private:
  Lock& l_;
  Guard(const Guard&);
  Guard& operator=(const Guard&);
};
}  // namespace sp
