// SkyPortal360 - virtual Portal of Power for Xbox 360 Skylanders games.
// See portal_core.h for what this is and where the protocol knowledge comes from.
#include "portal_core.h"

#include <string.h>

namespace sp {

namespace {
// A figure placed while the portal is active shows as "added" for this many status reports
// before settling to "ready", so the game reliably notices it.
const int kAddedReports = 8;
// Byte 6 of a status report: bit 0 set while the portal is active.
const int kStatusActiveByte = 6;
}  // namespace

// --------------------------------------------------------------------------------------------
// VirtualPortal
// --------------------------------------------------------------------------------------------

VirtualPortal::VirtualPortal()
    : q_head_(0), q_count_(0), active_(false), speaker_(false), counter_(0), commands_seen_(0) {
  memset(slots_, 0, sizeof(slots_));
  memset(pending_, 0, sizeof(pending_));
  memset(queue_, 0, sizeof(queue_));
  for (int i = 0; i < kMaxSlots; ++i) slots_[i].tag = -1;
}

void VirtualPortal::PushReply(const u8* bytes, int n) {
  if (q_count_ == kQueueSize) {  // game stopped reading: drop the oldest reply
    q_head_ = (q_head_ + 1) % kQueueSize;
    --q_count_;
  }
  const int tail = (q_head_ + q_count_) % kQueueSize;
  memset(queue_[tail], 0, kReportSize);
  memcpy(queue_[tail], bytes, n > kReportSize ? kReportSize : n);
  ++q_count_;
}

void VirtualPortal::Command(const u8* in) {
  Guard g(lock_);
  ++commands_seen_;
  u8 out[kReportSize];
  memset(out, 0, sizeof(out));

  switch (in[0]) {
    case 'R': {  // "ready": the reply identifies the portal
      out[0] = 0x52;
      out[1] = 0x02;
      out[2] = 0x1B;
      PushReply(out, 3);
      break;
    }
    case 'A': {  // activate (arg != 0) / deactivate (arg == 0)
      const bool activate = in[1] != 0;
      if (activate && !active_) {
        // Announce every figure when the portal goes from inactive to active. The game repeats
        // 'A 01' every few seconds while already active; re-announcing then would look like every
        // figure was lifted and put straight back.
        for (int i = 0; i < kMaxSlots; ++i) {
          if (slots_[i].present) {
            slots_[i].state = kAdded;
            slots_[i].reports_left = kAddedReports;
          }
        }
      } else if (!activate) {
        for (int i = 0; i < kMaxSlots; ++i) slots_[i].state = kEmpty;  // kept, just not reported
      }
      active_ = activate;
      out[0] = 0x41;
      out[1] = in[1];
      out[2] = 0xFF;
      out[3] = 0x77;
      PushReply(out, 4);
      break;
    }
    case 'M': {  // audio firmware version: tells the game whether the portal has a speaker
      out[0] = 0x4D;
      out[1] = in[1];
      out[2] = speaker_ ? 0x01 : 0x00;
      out[3] = 0x19;
      PushReply(out, 4);
      break;
    }
    case 'J': {  // Trap Team portal sync
      out[0] = 0x4A;
      PushReply(out, 1);
      break;
    }
    case 'Q': {  // read one block of a figure
      const u8 slot = in[1] & 0x0F;
      const u8 block = in[2];
      out[0] = 0x51;
      out[1] = slot;
      out[2] = block;
      const Slot& s = slots_[slot];
      if (s.present && block < kBlockCount) {
        out[1] |= 0x10;
        memcpy(out + 3, s.data + block * kBlockSize, kBlockSize);
      }
      PushReply(out, kReportSize);
      break;
    }
    case 'W': {  // write one block of a figure (the game saving progress to the toy)
      const u8 slot = in[1] & 0x0F;
      const u8 block = in[2];
      out[0] = 0x57;
      out[1] = slot;
      out[2] = block;
      Slot& s = slots_[slot];
      if (s.present && block < kBlockCount) {
        out[1] |= 0x10;
        memcpy(s.data + block * kBlockSize, in + 3, kBlockSize);
        s.dirty = true;
      }
      PushReply(out, 3);
      break;
    }
    default:
      // 'S' / 'V' (status: answered by idle reads), LED commands 'C' / 'L', anything unknown:
      // no reply, like a real portal.
      break;
  }
}

void VirtualPortal::StatusReportLocked(u8* r) {
  u32 states = 0;
  if (active_) {
    for (int i = 0; i < kMaxSlots; ++i) states |= (u32)(slots_[i].state & 3) << (2 * i);
  }
  memset(r, 0, kReportSize);
  r[0] = 0x53;  // 'S'
  r[1] = (u8)(states);
  r[2] = (u8)(states >> 8);
  r[3] = (u8)(states >> 16);
  r[4] = (u8)(states >> 24);
  r[5] = counter_++;
  r[kStatusActiveByte] = active_ ? 1 : 0;
  // Move transitions along once they have been reported.
  for (int i = 0; i < kMaxSlots; ++i) {
    Slot& s = slots_[i];
    if (s.state == kAdded) {
      if (--s.reports_left <= 0) s.state = kReady;
    } else if (s.state == kRemoving) {
      s.state = kEmpty;
    }
  }
}

void VirtualPortal::NextReport(u8* out) {
  Guard g(lock_);
  if (q_count_ > 0) {
    memcpy(out, queue_[q_head_], kReportSize);
    q_head_ = (q_head_ + 1) % kQueueSize;
    --q_count_;
    return;
  }
  StatusReportLocked(out);
}

bool VirtualPortal::Place(int slot, const u8* figure, int tag) {
  if (slot < 0 || slot >= kMaxSlots || !figure) return false;
  Guard g(lock_);
  Slot& s = slots_[slot];
  if (s.present && s.dirty) {
    // Replacing a figure the game wrote to: keep its writes for TakeDirty.
    for (int i = 0; i < kMaxSlots; ++i) {
      if (!pending_[i].used) {
        pending_[i].used = true;
        pending_[i].tag = s.tag;
        memcpy(pending_[i].data, s.data, kFigureSize);
        break;
      }
    }
  }
  s.present = true;
  s.dirty = false;
  s.tag = tag;
  memcpy(s.data, figure, kFigureSize);
  if (active_) {
    s.state = kAdded;
    s.reports_left = kAddedReports;
  } else {
    s.state = kEmpty;  // announced when the game activates the portal
  }
  return true;
}

bool VirtualPortal::Remove(int slot) {
  if (slot < 0 || slot >= kMaxSlots) return false;
  Guard g(lock_);
  Slot& s = slots_[slot];
  if (!s.present) return false;
  if (s.dirty) {
    int free_index = -1;
    for (int i = 0; i < kMaxSlots; ++i) {
      if (!pending_[i].used) {
        free_index = i;
        break;
      }
    }
    if (free_index >= 0) {
      pending_[free_index].used = true;
      pending_[free_index].tag = s.tag;
      memcpy(pending_[free_index].data, s.data, kFigureSize);
    }
  }
  s.present = false;
  s.dirty = false;
  s.tag = -1;
  memset(s.data, 0, kFigureSize);
  s.state = active_ ? kRemoving : kEmpty;
  return true;
}

void VirtualPortal::RemoveAll() {
  for (int i = 0; i < kMaxSlots; ++i) Remove(i);
}

bool VirtualPortal::Present(int slot) const {
  if (slot < 0 || slot >= kMaxSlots) return false;
  Guard g(lock_);
  return slots_[slot].present;
}

int VirtualPortal::Tag(int slot) const {
  if (slot < 0 || slot >= kMaxSlots) return -1;
  Guard g(lock_);
  return slots_[slot].present ? slots_[slot].tag : -1;
}

int VirtualPortal::FindTag(int tag) const {
  Guard g(lock_);
  for (int i = 0; i < kMaxSlots; ++i) {
    if (slots_[i].present && slots_[i].tag == tag) return i;
  }
  return -1;
}

int VirtualPortal::FirstFreeSlot() const {
  Guard g(lock_);
  // Prefer a slot the game has already seen go empty: reusing a slot whose removal hasn't been
  // reported yet would turn "figure A lifted, figure B placed" into a silent swap.
  for (int i = 0; i < kMaxSlots; ++i) {
    if (!slots_[i].present && slots_[i].state != kRemoving) return i;
  }
  for (int i = 0; i < kMaxSlots; ++i) {
    if (!slots_[i].present) return i;
  }
  return -1;
}

int VirtualPortal::Count() const {
  Guard g(lock_);
  int n = 0;
  for (int i = 0; i < kMaxSlots; ++i) n += slots_[i].present ? 1 : 0;
  return n;
}

bool VirtualPortal::TakeDirty(int* tag, u8* data) {
  Guard g(lock_);
  for (int i = 0; i < kMaxSlots; ++i) {
    if (pending_[i].used) {
      *tag = pending_[i].tag;
      memcpy(data, pending_[i].data, kFigureSize);
      pending_[i].used = false;
      return true;
    }
  }
  for (int i = 0; i < kMaxSlots; ++i) {
    if (slots_[i].present && slots_[i].dirty) {
      *tag = slots_[i].tag;
      memcpy(data, slots_[i].data, kFigureSize);
      slots_[i].dirty = false;
      return true;
    }
  }
  return false;
}

void VirtualPortal::SetSpeaker(bool speaker) {
  Guard g(lock_);
  speaker_ = speaker;
}

void VirtualPortal::ResetSession() {
  Guard g(lock_);
  active_ = false;
  q_head_ = 0;
  q_count_ = 0;
  counter_ = 0;
  for (int i = 0; i < kMaxSlots; ++i) {
    slots_[i].state = kEmpty;
    slots_[i].reports_left = 0;
  }
}

u32 VirtualPortal::CommandsSeen() const {
  Guard g(lock_);
  return commands_seen_;
}

bool VirtualPortal::Active() const {
  Guard g(lock_);
  return active_;
}

// --------------------------------------------------------------------------------------------
// XamBridge
// --------------------------------------------------------------------------------------------

XamBridge::XamBridge(VirtualPortal* portal) : portal_(portal), delivered_last_(false) {}

void XamBridge::Reset() {
  Guard g(lock_);
  delivered_last_ = false;
}

u32 XamBridge::Read(u8* buffer, u32 capacity, u32* bytes_read, u16* state) {
  Guard g(lock_);
  if (!portal_) {
    delivered_last_ = false;
    return kXamDeviceNotConnected;
  }
  // The game keeps reading until a read says "no new data" (state 0) and only then sends its own
  // commands. The virtual portal always has a report (status when nothing is queued), so every
  // other read answers "no new data": each poll gets exactly one report, then the game writes.
  if (delivered_last_) {
    delivered_last_ = false;
    *bytes_read = 0;
    *state = 0;
    return kXamSuccess;
  }
  u8 report[kReportSize];
  portal_->NextReport(report);
  // A real portal only streams status reports once activated ('A 01'). Trap Team restarts its
  // handshake when it sees an inactive portal, so those are withheld as "no new data".
  if (report[0] == 'S' && !(report[kStatusActiveByte] & 1)) {
    delivered_last_ = false;
    *bytes_read = 0;
    *state = 0;
    return kXamSuccess;
  }
  u8 frame[kFrameSize];
  frame[0] = kFrameHeader0;
  frame[1] = kFrameCommand;
  memcpy(frame + 2, report, kFramePayload);
  const u32 n = capacity < (u32)kFrameSize ? capacity : (u32)kFrameSize;
  memcpy(buffer, frame, n);
  *bytes_read = n;
  *state = 1;
  delivered_last_ = true;
  return kXamSuccess;
}

u32 XamBridge::Write(const u8* buffer, u32 length) {
  Guard g(lock_);
  if (!portal_) return kXamDeviceNotConnected;
  if (length > 2 && buffer[0] == kFrameHeader0 && buffer[1] == kFrameSpeaker) {
    return kXamSuccess;  // Trap Team speaker audio (G.726): no speaker on a virtual portal
  }
  u8 frame[kFrameSize];
  memset(frame, 0, sizeof(frame));
  memcpy(frame, buffer, length < (u32)kFrameSize ? length : (u32)kFrameSize);
  if (frame[0] != kFrameHeader0 || frame[1] != kFrameCommand) return kXamSuccess;  // ignore
  u8 report[kReportSize];
  memset(report, 0, sizeof(report));
  memcpy(report, frame + 2, kFramePayload);
  portal_->Command(report);
  return kXamSuccess;
}

}  // namespace sp
