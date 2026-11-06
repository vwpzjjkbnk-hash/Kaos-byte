// Native tests for the portal core. Plays the game's side of the Xbox 360 handshake through
// XamBridge exactly as a title would through XamInputNonControllerGetRaw/SetRaw.
//   make -C tests      (builds with -std=c++98 to prove the core stays XDK/VS2010 friendly)
#include <stdio.h>
#include <string.h>

#include "../src/core/portal_core.h"

using namespace sp;

static int g_failures = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);         \
      ++g_failures;                                                 \
    }                                                               \
  } while (0)

// Sends one command the way the game does: 0B 14 + payload.
static void Send(XamBridge& b, u8 c0, u8 c1 = 0, u8 c2 = 0, const u8* blk = 0) {
  u8 f[32];
  memset(f, 0, sizeof(f));
  f[0] = 0x0B;
  f[1] = 0x14;
  f[2] = c0;
  f[3] = c1;
  f[4] = c2;
  if (blk) memcpy(f + 5, blk, 16);
  CHECK(b.Write(f, 32) == kXamSuccess);
}

// Polls until a report arrives (skipping "no new data"); returns false after many empty polls.
static bool Poll(XamBridge& b, u8* report30) {
  for (int i = 0; i < 8; ++i) {
    u8 buf[32];
    u32 n = 32;
    u16 st = 0;
    CHECK(b.Read(buf, 32, &n, &st) == kXamSuccess);
    if (st == 1) {
      CHECK(n == 32);
      CHECK(buf[0] == 0x0B && buf[1] == 0x14);
      memcpy(report30, buf + 2, 30);
      return true;
    }
    CHECK(n == 0);
  }
  return false;
}

// Reads replies until one starting with `cmd` shows up (status reports may interleave).
static bool Expect(XamBridge& b, u8 cmd, u8* out) {
  for (int i = 0; i < 16; ++i) {
    if (!Poll(b, out)) return false;
    if (out[0] == cmd) return true;
  }
  return false;
}

static void MakeFigure(u8* fig, u8 seed) {
  for (int i = 0; i < kFigureSize; ++i) fig[i] = (u8)(i * 7 + seed);
}

static void TestHandshake() {
  printf("handshake\n");
  VirtualPortal p;
  XamBridge b(&p);
  u8 r[30];

  // Inactive portal: status reports are withheld, so idle reads say "no new data".
  CHECK(!Poll(b, r));

  Send(b, 'R');
  CHECK(Expect(b, 'R', r));
  CHECK(r[1] == 0x02 && r[2] == 0x1B);

  Send(b, 'A', 1);
  CHECK(Expect(b, 'A', r));
  CHECK(r[1] == 1 && r[2] == 0xFF && r[3] == 0x77);
  CHECK(p.Active());

  // Active: status reports flow, with the active bit set and a counter that increments.
  CHECK(Poll(b, r) && r[0] == 'S' && (r[6] & 1));
  const u8 c0 = r[5];
  CHECK(Poll(b, r) && r[0] == 'S' && r[5] == (u8)(c0 + 1));

  Send(b, 'M', 1);
  CHECK(Expect(b, 'M', r));
  CHECK(r[2] == 0x00 && r[3] == 0x19);
  p.SetSpeaker(true);
  Send(b, 'M', 1);
  CHECK(Expect(b, 'M', r) && r[2] == 0x01);

  Send(b, 'J');
  CHECK(Expect(b, 'J', r));

  // LED colour commands get no reply.
  Send(b, 'C', 0xFF, 0x00);
  CHECK(Poll(b, r) && r[0] == 'S');
}

static void TestFigureLifecycle() {
  printf("figure place / read / write / remove\n");
  VirtualPortal p;
  XamBridge b(&p);
  u8 r[30];
  u8 fig[kFigureSize];
  MakeFigure(fig, 3);

  // Placed before the game activates the portal: announced on activation.
  CHECK(p.Place(0, fig, 42));
  CHECK(p.FindTag(42) == 0);
  CHECK(p.FirstFreeSlot() == 1);
  Send(b, 'A', 1);
  CHECK(Expect(b, 'A', r));
  CHECK(Poll(b, r) && r[0] == 'S');
  CHECK((r[1] & 3) == 3);  // slot 0 "added"
  for (int i = 0; i < 10; ++i) CHECK(Poll(b, r));
  CHECK(r[0] == 'S' && (r[1] & 3) == 1);  // settled to "ready"

  // Repeated 'A 01' while active must not re-announce.
  Send(b, 'A', 1);
  CHECK(Expect(b, 'A', r));
  CHECK(Poll(b, r) && r[0] == 'S' && (r[1] & 3) == 1);

  // Read every block back.
  for (int blk = 0; blk < kBlockCount; ++blk) {
    Send(b, 'Q', 0x20 | 0, (u8)blk);
    CHECK(Expect(b, 'Q', r));
    CHECK(r[1] == 0x10 && r[2] == blk);
    CHECK(memcmp(r + 3, fig + blk * 16, 16) == 0);
  }
  // Reading an empty slot: no 0x10 flag.
  Send(b, 'Q', 5, 0);
  CHECK(Expect(b, 'Q', r) && r[1] == 5);

  // Write a block, read it back, and see it handed out for saving.
  u8 blk[16];
  for (int i = 0; i < 16; ++i) blk[i] = (u8)(0xA0 + i);
  Send(b, 'W', 0, 0x08, blk);
  CHECK(Expect(b, 'W', r) && r[1] == 0x10 && r[2] == 0x08);
  Send(b, 'Q', 0, 0x08);
  CHECK(Expect(b, 'Q', r) && memcmp(r + 3, blk, 16) == 0);

  int tag = -1;
  u8 saved[kFigureSize];
  CHECK(p.HasUnsaved(42) && !p.HasUnsaved(7));  // (its file is behind until saved)
  CHECK(p.TakeDirty(&tag, saved));
  CHECK(tag == 42);
  CHECK(memcmp(saved + 0x80, blk, 16) == 0);
  CHECK(memcmp(saved, fig, 0x80) == 0);
  CHECK(!p.TakeDirty(&tag, saved));  // nothing left
  CHECK(!p.HasUnsaved(42));

  // A write, then removal before the save: the writes survive in the pending list.
  blk[0] = 0x55;
  Send(b, 'W', 0, 0x09, blk);
  CHECK(Expect(b, 'W', r));
  CHECK(p.Remove(0));
  CHECK(Poll(b, r) && r[0] == 'S' && (r[1] & 3) == 2);  // "removing"
  CHECK(Poll(b, r) && r[0] == 'S' && (r[1] & 3) == 0);  // then empty
  CHECK(p.HasUnsaved(42));  // taken off, still to be saved (put back on, it's saved first)
  CHECK(p.TakeDirty(&tag, saved) && tag == 42 && saved[0x90] == 0x55);
  CHECK(!p.HasUnsaved(42));
  CHECK(!p.TakeDirty(&tag, saved));

  // Swap without the game seeing the removal: the new figure must not reuse the vacated slot.
  CHECK(p.Place(0, fig, 42));
  for (int i = 0; i < 10; ++i) CHECK(Poll(b, r));
  CHECK(p.Remove(0));
  CHECK(p.FirstFreeSlot() == 1);
  CHECK(p.RemovalPending());                             // (the delayed "swap in" waits on this)
  const u32 reports = p.StatusReports();
  CHECK(Poll(b, r) && r[0] == 'S' && (r[1] & 3) == 2);  // removal reported to the game...
  CHECK(!p.RemovalPending() && p.StatusReports() == reports + 1);
  CHECK(p.FirstFreeSlot() == 0);                         // ...so the slot is reusable
  // (discard the dirty copy from this removal; nothing was written)
  CHECK(!p.TakeDirty(&tag, saved));

  // Writing to an empty slot is refused (no 0x10) and changes nothing.
  Send(b, 'W', 0, 0x09, blk);
  CHECK(Expect(b, 'W', r) && r[1] == 0x00);
  CHECK(!p.TakeDirty(&tag, saved));
}

static void TestManySlotsAndSpeaker() {
  printf("16 slots + speaker frames\n");
  VirtualPortal p;
  XamBridge b(&p);
  u8 r[30];
  u8 fig[kFigureSize];
  for (int s = 0; s < kMaxSlots; ++s) {
    MakeFigure(fig, (u8)s);
    CHECK(p.Place(s, fig, 100 + s));
  }
  CHECK(!p.Place(16, fig, 1));
  CHECK(p.FirstFreeSlot() == -1);
  CHECK(p.Count() == 16);
  Send(b, 'A', 1);
  CHECK(Expect(b, 'A', r));
  CHECK(Poll(b, r) && r[0] == 'S');
  CHECK(r[1] == 0xFF && r[2] == 0xFF && r[3] == 0xFF && r[4] == 0xFF);  // all "added"

  // Slot 15 data.
  Send(b, 'Q', 15, 3);
  CHECK(Expect(b, 'Q', r) && r[1] == (0x10 | 15));
  MakeFigure(fig, 15);
  CHECK(memcmp(r + 3, fig + 48, 16) == 0);

  // Speaker frames (0B 17) are accepted and ignored.
  u8 audio[32];
  memset(audio, 0x77, sizeof(audio));
  audio[0] = 0x0B;
  audio[1] = 0x17;
  CHECK(b.Write(audio, 32) == kXamSuccess);
  const u32 before = p.CommandsSeen();
  CHECK(b.Write(audio, 32) == kXamSuccess);
  CHECK(p.CommandsSeen() == before);
}

static void TestResetAndSmallBuffer() {
  printf("session reset + short buffers\n");
  VirtualPortal p;
  XamBridge b(&p);
  u8 r[30];
  u8 fig[kFigureSize];
  MakeFigure(fig, 9);
  p.Place(2, fig, 7);
  Send(b, 'A', 1);
  CHECK(Expect(b, 'A', r));
  p.ResetSession();
  b.Reset();
  CHECK(!p.Active());
  CHECK(!Poll(b, r));  // inactive again
  CHECK(p.Present(2));  // figure kept across sessions
  Send(b, 'A', 1);
  CHECK(Expect(b, 'A', r));
  CHECK(Poll(b, r) && r[0] == 'S' && ((r[1] >> 4) & 3) == 3);  // slot 2 re-announced

  // A game buffer shorter than a frame gets a truncated copy, never an overflow.
  u8 small[8];
  memset(small, 0xEE, sizeof(small));
  u32 n = 0;
  u16 st = 0;
  b.Reset();
  CHECK(b.Read(small, 4, &n, &st) == kXamSuccess);
  CHECK(st == 1 && n == 4);
  CHECK(small[4] == 0xEE);

  // Reply queue overflow drops the oldest instead of growing.
  for (int i = 0; i < 200; ++i) Send(b, 'R');
  int replies = 0;
  b.Reset();
  for (int i = 0; i < 500; ++i) {
    if (!Poll(b, r)) break;
    if (r[0] == 'R') ++replies;
    if (r[0] == 'S') break;
  }
  CHECK(replies == 64);
}

int main() {
  TestHandshake();
  TestFigureLifecycle();
  TestManySlotsAndSpeaker();
  TestResetAndSmallBuffer();
  if (g_failures) {
    printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  printf("all portal tests passed\n");
  return 0;
}
