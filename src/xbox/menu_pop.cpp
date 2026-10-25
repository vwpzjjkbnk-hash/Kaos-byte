// SkyPortal360 - skypop menu. See menu_pop.h.
#include "menu_pop.h"

#include <stdarg.h>

#include "elements.h"
#include "menu_api.h"
#include "util.h"

// XamShowMessageBoxUI (xam 0x2CA): (user, title, text, button count, buttons, focused button,
// flags, MESSAGEBOX_RESULT*, XOVERLAPPED*). Asynchronous: returns ERROR_IO_PENDING and finishes
// by writing the status into the XOVERLAPPED (polled here, no event needed).
extern "C" DWORD XamShowMessageBoxUI(DWORD user, const wchar_t* title, const wchar_t* text,
                                     DWORD button_count, const wchar_t** buttons, DWORD focus,
                                     DWORD flags, DWORD* result, void* overlapped);

namespace popmenu {

namespace {

const DWORD kIoPending = 997;      // ERROR_IO_PENDING
const DWORD kCancelled = 1223;     // ERROR_CANCELLED
enum { kButtonPrev = 0, kButtonAct = 1, kButtonNext = 2 };

struct Overlapped {  // XOVERLAPPED
  volatile DWORD internal_low;
  DWORD internal_high;
  DWORD internal_context;
  HANDLE event;
  void* completion_routine;
  DWORD completion_context;
  DWORD extended_error;
};

struct State {
  bool open;
  int pad;
  DWORD focus;
  bool act_removes;  // the middle button takes the figure off
  bool act_adds;     // the middle button adds without clearing (portal empty)
  Overlapped ov;
  DWORD result;      // MESSAGEBOX_RESULT.dwButtonPressed
  wchar_t title[80];
  wchar_t text[400];
  wchar_t buttons[3][32];
  const wchar_t* button_ptrs[3];
};
State g = {};

void Widen(wchar_t* out, int cap, const char* in) {
  int i = 0;
  for (; in[i] && i < cap - 1; ++i) out[i] = (wchar_t)(unsigned char)in[i];
  out[i] = 0;
}

void Appendf(wchar_t* out, int cap, const char* fmt, ...) {
  char tmp[256];
  va_list ap;
  va_start(ap, fmt);
  _vsnprintf_s(tmp, sizeof(tmp), _TRUNCATE, fmt, ap);
  va_end(ap);
  int len = 0;
  while (out[len]) ++len;
  Widen(out + len, cap - len, tmp);
}

void Show() {
  menu::View v;
  menu::GetView(menu::Pos(), &v);
  if (v.id < 0) {
    util::NotifyA("No figures found - copy dumps to The Static Rift\\Figures");
    g.open = false;
    return;
  }
  char upper[64];
  int i = 0;
  for (; v.name[i] && i < 63; ++i) {
    char c = v.name[i];
    upper[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
  }
  upper[i] = 0;
  Widen(g.title, 80, upper);

  g.text[0] = 0;
  Appendf(g.text, 400, "Figure %d of %d", v.pos + 1, v.count);
  const char* el = elements::Name((elements::Element)v.element);
  if (el[0]) Appendf(g.text, 400, "\nElement: %s", el);
  Appendf(g.text, 400, "\n%s", v.on_portal ? "On the rift now" : "Not on the rift");
  if (v.portal_count) {
    Appendf(g.text, 400, "\n\nOn the rift (%d): %s", v.portal_count, v.portal_names);
  } else {
    Appendf(g.text, 400, "\n\nThe rift is empty.");
  }
  Appendf(g.text, 400, "\n\nD-pad: choose a button   A: press   B: close");

  g.act_removes = v.on_portal;
  g.act_adds = !v.on_portal && v.portal_count == 0;
  Widen(g.buttons[kButtonPrev], 32, "\xAB Previous");
  Widen(g.buttons[kButtonAct], 32,
        g.act_removes ? "Take off the rift" : (g.act_adds ? "Put on the rift" : "Swap in"));
  Widen(g.buttons[kButtonNext], 32, "Next \xBB");
  for (int b = 0; b < 3; ++b) g.button_ptrs[b] = g.buttons[b];

  memset(&g.ov, 0, sizeof(g.ov));
  g.result = 0;
  DWORD r = 0;
  for (int attempt = 0; attempt < 6; ++attempt) {
    memset(&g.ov, 0, sizeof(g.ov));
    r = XamShowMessageBoxUI((DWORD)g.pad, g.title, g.text, 3, g.button_ptrs, g.focus,
                            0 /* XMB_NOICON */, &g.result, &g.ov);
    if (r != 5) break;  // ERROR_ACCESS_DENIED: another console box or toast is still closing
    Sleep(250);
  }
  if (r != kIoPending && r != 0) {
    util::Log("XamShowMessageBoxUI failed: %u", r);
    util::NotifyA("The Static Rift: couldn't open the menu (error %u)", r);
    g.open = false;
    return;
  }
  g.open = true;
}

}  // namespace

void Open(int pad) {
  if (g.open) return;
  g.pad = (pad >= 0 && pad < 4) ? pad : 0;
  g.focus = kButtonAct;
  Show();
}

bool IsOpen() { return g.open; }

void Poll() {
  if (!g.open || g.ov.internal_low == kIoPending) return;
  const DWORD status = g.ov.internal_low;
  g.open = false;
  if (status == kCancelled || status != 0) return;  // B / Guide closed it
  switch (g.result) {
    case kButtonPrev:
      menu::SetPos(menu::Pos() - 1);
      g.focus = kButtonPrev;
      Sleep(150);  // let the closing animation finish before the next box
      Show();
      break;
    case kButtonNext:
      menu::SetPos(menu::Pos() + 1);
      g.focus = kButtonNext;
      Sleep(150);
      Show();
      break;
    case kButtonAct:
      menu::Do(g.act_removes ? menu::kRemove : (g.act_adds ? menu::kAdd : menu::kSwap), true);
      break;
  }
}

}  // namespace popmenu
