// Host test for the XEX import patcher and execution-ID reader (src/xbox/util.cpp), run against a
// fake title import table built with the same xkelib structures. Checks the table walk (several
// import modules, variable/stub record pairs), idempotence, and the PowerPC jump encoding.
#include <stdio.h>
#include <string.h>

#include "../src/xbox/util.h"

// ---- host stand-ins for the kernel exports util.cpp uses ----
static void* g_import_desc = 0;
static void* g_exec_id = 0;
extern "C" PVOID RtlImageXexHeaderField(PVOID, DWORD key) {
  if (key == 0x000103FF) return g_import_desc;
  if (key == 0x00040006) return g_exec_id;
  return 0;
}
void __dcbst(int, const void*) {}
void __icbi(int, const void*) {}
void __sync() {}
void __isync() {}
void InitializeCriticalSection(CRITICAL_SECTION*) {}  // util.cpp's static log lock
void DeleteCriticalSection(CRITICAL_SECTION*) {}

static int g_fail = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);         \
      ++g_fail;                                                     \
    }                                                               \
  } while (0)

// Decodes our 4-instruction jump back to its target, or 0 if it isn't one.
static DWORD JumpTarget(const DWORD* s) {
  if ((s[0] & 0xFFFF0000) != 0x3D600000 || (s[1] & 0xFFFF0000) != 0x396B0000 ||
      s[2] != 0x7D6903A6 || s[3] != 0x4E800420) {
    return 0;
  }
  const DWORD hi = (s[0] & 0xFFFF) << 16;
  const short lo = (short)(s[1] & 0xFFFF);  // addi sign-extends
  return (DWORD)((hi + (long)lo) & 0xFFFFFFFF);
}

int main() {
  // Two import modules (as in a real title: xboxkrnl.exe then xam.xex), each with variable/stub
  // record pairs. Addresses here are host pointers stored in DWORD records.
  static DWORD vars[8];
  static DWORD stubs[8][4];
  const DWORD kGetRaw = 0x81A0B000, kSetRaw = 0x81A0B100, kOther = 0x81A0C000;
  // module 0: kOther, kGetRaw ; module 1: kSetRaw, kOther, kGetRaw (imported twice)
  const DWORD resolved[5] = {kOther, kGetRaw, kSetRaw, kOther, kGetRaw};
  for (int i = 0; i < 5; ++i) {
    vars[i] = resolved[i];
    stubs[i][0] = 0x3D600000;  // whatever the loader left there
    stubs[i][1] = 0x816B0000;
    stubs[i][2] = 0x7D6903A6;
    stubs[i][3] = 0x4E800420;
  }

  // Build the descriptor: header, name table, then the import tables back to back.
  static unsigned char blob[1024];
  memset(blob, 0, sizeof(blob));
  XEX_IMPORT_DESCRIPTOR* desc = (XEX_IMPORT_DESCRIPTOR*)blob;
  desc->NameTableSize = 16;
  desc->ModuleCount = 2;
  unsigned char* p = blob + sizeof(XEX_IMPORT_DESCRIPTOR) + desc->NameTableSize;
  int imp = 0;
  const int per_module[2] = {2, 3};
  for (int m = 0; m < 2; ++m) {
    XEX_IMPORT_TABLE_ORG* t = (XEX_IMPORT_TABLE_ORG*)p;
    t->ImportTable.ImportCount = (WORD)(per_module[m] * 2);
    DWORD* rec = (DWORD*)(t + 1);
    for (int k = 0; k < per_module[m]; ++k, ++imp) {
      rec[2 * k] = (DWORD)&vars[imp];
      rec[2 * k + 1] = (DWORD)&stubs[imp][0];
    }
    t->TableSize = (DWORD)(sizeof(XEX_IMPORT_TABLE_ORG) + sizeof(DWORD) * per_module[m] * 2);
    p += t->TableSize;
  }
  g_import_desc = blob;

  LDR_DATA_TABLE_ENTRY module;
  memset(&module, 0, sizeof(module));
  module.XexHeaderBase = (PVOID)blob;  // any non-null header base

  // Hook addresses chosen to exercise the addi sign carry (low half >= 0x8000) and not.
  const DWORD kHookGet = 0x8A01F2A8;  // lo 0xF2A8 -> hi must be bumped
  const DWORD kHookSet = 0x8A012340;  // lo 0x2340

  printf("import patching\n");
  CHECK(util::PatchModuleImport(&module, kGetRaw, kHookGet) == 2);  // both GetRaw imports
  CHECK(util::PatchModuleImport(&module, kSetRaw, kHookSet) == 1);
  CHECK(vars[1] == kHookGet && vars[4] == kHookGet);
  CHECK(vars[2] == kHookSet);
  CHECK(vars[0] == kOther && vars[3] == kOther);  // unrelated imports untouched
  CHECK(JumpTarget(stubs[1]) == kHookGet);
  CHECK(JumpTarget(stubs[4]) == kHookGet);
  CHECK(JumpTarget(stubs[2]) == kHookSet);
  CHECK(JumpTarget(stubs[0]) == 0 && JumpTarget(stubs[3]) == 0);
  // Idempotent: a second pass finds nothing left to patch.
  CHECK(util::PatchModuleImport(&module, kGetRaw, kHookGet) == 0);
  CHECK(util::PatchModuleImport(&module, kSetRaw, kHookSet) == 0);
  // Missing pieces are handled.
  CHECK(util::PatchModuleImport(0, kGetRaw, kHookGet) == 0);
  CHECK(util::PatchModuleImport(&module, 0, kHookGet) == 0);
  g_import_desc = 0;
  CHECK(util::PatchModuleImport(&module, kOther, kHookGet) == 0);

  printf("execution id\n");
  // XEX_EXECUTION_ID: MediaID, Version, BaseVersion, TitleID, ...  (32-bit fields)
  static unsigned int exec[6] = {0x4FC9256F, 0x00000501, 0, 0x41560911, 0, 0};
  // The reader indexes DWORDs; on this host DWORD may be 64-bit, so build matching storage.
  static DWORD exec_host[6];
  for (int i = 0; i < 6; ++i) exec_host[i] = exec[i];
  g_exec_id = exec_host;
  util::ExecutionId id;
  CHECK(util::ReadExecutionId(&module, &id));
  CHECK(id.title_id == 0x41560911 && id.media_id == 0x4FC9256F && id.version == 0x501);
  g_exec_id = 0;
  CHECK(!util::ReadExecutionId(&module, &id));
  CHECK(!util::ReadExecutionId(0, &id));

  if (g_fail) {
    printf("%d check(s) failed\n", g_fail);
    return 1;
  }
  printf("all patch tests passed\n");
  return 0;
}
