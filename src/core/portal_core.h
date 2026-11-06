// SkyPortal360 - virtual Portal of Power for Xbox 360 Skylanders games.
//
// Portable portal core: no Xbox headers, C++03 only, so it builds in the Xbox 360 XDK
// (Visual Studio 2010) and natively for the unit tests in tests/.
//
// The protocol behaviour (command replies, status-report layout, the Xbox 360 "0B 14" frame and
// the read/"no new data" alternation XAM expects) follows TheBiemGamer/skylanders-portal (MIT),
// which in turn documented it from Dolphin, RPCS3, Cemu and Xenia Canary. See THIRD_PARTY.md.
#pragma once

#include "sp_lock.h"

namespace sp {

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

// A raw portal report (what a USB portal sends/receives, without the Xbox 360 frame header).
const int kReportSize = 32;

// The Xbox 360 game exchanges 32-byte frames with XAM: 0B 14 + a 30-byte report payload.
// Speaker audio for Trap Team arrives as 0B 17 frames.
const int kFrameSize = 32;
const int kFramePayload = kFrameSize - 2;
const u8 kFrameHeader0 = 0x0B;
const u8 kFrameCommand = 0x14;
const u8 kFrameSpeaker = 0x17;

// A figure: 64 blocks of 16 bytes (raw MIFARE 1K dump, encrypted exactly as on the toy).
const int kBlockSize = 16;
const int kBlockCount = 64;
const int kFigureSize = kBlockSize * kBlockCount;

// A portal can report up to 16 figures at once (Trap Team traps count as figures).
const int kMaxSlots = 16;

// Win32-style results returned by XAM's raw non-controller API.
const u32 kXamSuccess = 0x00000000;
const u32 kXamInvalidParameter = 0x00000057;
const u32 kXamDeviceNotConnected = 0x0000048F;

class VirtualPortal {
 public:
  VirtualPortal();

  // ---- Game side (called from the game's polling thread through XamBridge) ----

  // One command report from the game (kReportSize bytes, no frame header).
  void Command(const u8* report);
  // The next report for the game: a queued command reply, else a status report.
  void NextReport(u8* out /* kReportSize */);

  // ---- Control side (thread-safe, called from the plugin's worker thread) ----

  // Puts a figure on the portal. `tag` is an opaque id the caller uses to know where the figure
  // came from (the plugin uses its figure-library index). Returns false if `slot` is invalid.
  bool Place(int slot, const u8* figure /* kFigureSize */, int tag);
  // Takes the figure off. Unsaved game writes are kept and still handed out by TakeDirty.
  bool Remove(int slot);
  void RemoveAll();

  bool Present(int slot) const;
  int Tag(int slot) const;      // -1 if the slot is empty
  int FindTag(int tag) const;   // slot holding `tag`, or -1
  int FirstFreeSlot() const;    // -1 if full
  int Count() const;

  // Hands out one figure the game has written to since the last call (including figures that
  // were removed with unsaved writes). Returns false when nothing is pending.
  bool TakeDirty(int* tag, u8* data /* kFigureSize */);
  // Whether figure `tag` has game writes not handed out by TakeDirty yet (on the portal, or taken
  // off): its file is behind until they're saved.
  bool HasUnsaved(int tag) const;

  // Whether 'M' reports a speaker (Trap Team audio). Audio itself is always discarded.
  void SetSpeaker(bool speaker);

  // A new game session: the portal goes back to inactive and queued replies are dropped. Figures
  // stay on the portal and are announced again when the game activates it.
  void ResetSession();
  void RecoverLock() { lock_.Reinit(); }  // see sp::Lock::Reinit

  // Diagnostics.
  u32 CommandsSeen() const;
  bool Active() const;
  // A removal the game hasn't been told about yet (a status report still has to say so).
  bool RemovalPending() const;
  // Status reports handed to the game so far (counts the game's polls while the portal is active).
  u32 StatusReports() const;

 private:
  enum SlotState { kEmpty = 0, kReady = 1, kRemoving = 2, kAdded = 3 };
  struct Slot {
    bool present;
    bool dirty;
    int state;
    int reports_left;
    int tag;
    u8 data[kFigureSize];
  };
  struct Pending {  // removed figure with writes not yet saved
    bool used;
    int tag;
    u8 data[kFigureSize];
  };

  void PushReply(const u8* bytes, int n);
  void StatusReportLocked(u8* out);

  mutable Lock lock_;
  Slot slots_[kMaxSlots];
  Pending pending_[kMaxSlots];

  enum { kQueueSize = 64 };
  u8 queue_[kQueueSize][kReportSize];
  int q_head_;
  int q_count_;

  bool active_;
  bool speaker_;
  u8 counter_;
  u32 commands_seen_;
  u32 status_reports_;
};

// Implements the game's side of XamInputNonControllerGetRaw/SetRaw on top of a VirtualPortal:
// adds/strips the 0B 14 header, drops 0B 17 speaker frames, and alternates "here is a report"
// with "no new data" the way XAM does with a real portal.
class XamBridge {
 public:
  explicit XamBridge(VirtualPortal* portal);

  // buffer/capacity: the game's buffer and its size (<= 32). On success fills bytes_read/state.
  u32 Read(u8* buffer, u32 capacity, u32* bytes_read, u16* state);
  u32 Write(const u8* buffer, u32 length);
  void Reset();
  void RecoverLock() { lock_.Reinit(); }  // see sp::Lock::Reinit

 private:
  VirtualPortal* portal_;
  bool delivered_last_;
  Lock lock_;
};

}  // namespace sp
