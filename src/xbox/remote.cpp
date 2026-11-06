// Project Kaos-Byte - the phone remote's web server. See remote.h.
//
// Peer Pressure: started once, from the plugin's thread in the game. Dashlaunch (RGH / JTAG /
// BadUpdate): started with each Skylanders game as one of that game's threads, gone with it.
//
// One thread serves up to kMaxConns connections at once, all non-blocking, with select():
//   - connections stay open between requests (HTTP keep-alive), so a phone loading the page and
//     its pictures reuses the same few connections instead of opening one per picture. The
//     console has a small pool of sockets, and every socket the Xbox closes first lingers for a
//     while afterwards (TIME_WAIT) still holding its place in that pool: one connection per
//     request used it up within a minute ("connection lost").
//   - each response goes out as one send where it can (headers and body together) with
//     TCP_NODELAY, so nothing waits on the phone's delayed ACKs.
//   - errors never stop the server: a broken connection is dropped, a broken listening socket is
//     opened again.
//   - live updates (3.2.0v): the page keeps one GET /api/events open (Server-Sent Events). Whenever
//     the rift changes (the phone, the TV menu, a swap finishing) the new state is pushed down it at
//     once, and a ping goes every 5 s so the page notices a dead connection within seconds.
//     Browsers reconnect an event stream by themselves within a second ("retry: 1000"), so a
//     Wi-Fi blip mends itself.
//   - when every slot is taken (browsers open spare connections in advance), the longest-idle
//     connection that hasn't asked for anything gives up its slot.
// Requests:
//   GET  /                     UI\remote.html (the page)
//   GET  /UI/..., /Images/..., /Phone/...   files from those folders of the data folder
//   GET  /api/events           the state now and on every change (text/event-stream)
//   GET  /api/state            what's on the rift, the running game, the lists   } answered by the
//   GET  /api/list?v=&from=    one page of a list (All, your lists, My Picks, a  } worker thread
//                              game)                                            } (main.cpp)
//   POST /api/do?a=&i=         put on / swap in / take off / clear               }
//   GET  /api/meta             nicknames, and what's in Favorites and your lists }
//   POST /api/nick?i=&n=       a figure's nickname                               }
//   POST /api/lists?a=&l=&i=&n= add to / take out of a list, new, rename, delete }
#include "remote.h"

#include "../core/remote_http.h"
#include "../core/sp_lock.h"
#include "remote_net.h"
#include "util.h"

#if defined(SKYPORTAL_XRT)
extern "C" LONG KeSetBasePriorityThread(DWORD thread, LONG increment);
#endif

namespace remote {

namespace {

char g_root[MAX_PATH] = {0};
int g_port = 80;
DWORD g_delay_ms = 0;
volatile State g_state = kOff;
char g_address[32] = {0};
volatile DWORD g_requests = 0;
volatile LONG g_rev = 0;  // goes up whenever the rift changes (Changed(), from the worker thread)

// ---- the hand-off to the worker thread ----
// The server posts one API call at a time; the worker answers it in Service() into g_mail_out
// after kHeadRoom bytes, so the server can put the response headers right in front and send the
// whole response in one go. The connection that posted it holds the slot until it's sent.
enum { kMailIdle = 0, kMailWaiting, kMailAnswered };
enum { kApiCap = 24 * 1024, kHeadRoom = 512 };
sp::Lock g_mail_lock;
volatile int g_mail_state = kMailIdle;  // (volatile: Waiting() looks without the lock)
char g_mail_path[64];
char g_mail_query[256];
char g_mail_out[kHeadRoom + kApiCap];
int g_mail_len = -1;
int g_mail_owner = -1;  // connection using the slot (server thread only)

// ---- connections (server thread only) ----
enum { kMaxConns = 8, kRequestCap = 2048, kSmallCap = 640 };
const DWORD kIdleMs = 60000;      // a quiet connection is closed after this (the phone usually first)
const DWORD kRequestMs = 10000;   // a request has to arrive in full within this
const DWORD kPingMs = 5000;       // an event stream hears something at least this often
enum ConnState {
  kFree = 0,
  kReading,        // waiting for (the rest of) a request
  kWaitApi,        // an API call waiting for the worker
  kSending,        // a response going out
  kStreaming,      // an event stream with nothing to send right now
  kStreamWaitApi,  // an event stream waiting for the worker's state
};

struct Conn {
  XSOCKET s;
  ConnState state;
  char request[kRequestCap];
  int request_len;
  DWORD last_active;
  DWORD request_started;
  bool keep_alive;
  // What's being sent: `out` (headers + body in memory), then the rest of a file.
  const char* out;
  int out_len, out_sent;
  char small[kSmallCap];  // headers (and a short body) of responses that aren't API answers
  bool holds_mail;
  HANDLE file;
  DWORD file_pos, file_left;
  char api_path[64];
  char api_query[256];
  bool stream;      // an event stream (GET /api/events)
  LONG stream_rev;  // the g_rev it last sent (-1: nothing yet)
  LONG pending_rev; // the g_rev its state was asked for at
  DWORD last_send;
};
Conn g_conns[kMaxConns];
char g_chunk[16 * 1024];  // file data on its way out (shared: filled and sent in one step)

bool SetOption(XSOCKET s, DWORD level, DWORD name, DWORD value) {
  return NetDll_setsockopt(XNCALLER_TITLE, s, level, name, &value, sizeof(value)) == 0;
}

bool SetNonBlocking(XSOCKET s) {
  DWORD on = 1;
  return NetDll_ioctlsocket(XNCALLER_TITLE, s, XFIONBIO, &on) == 0;
}

bool WouldBlock() { return NetDll_WSAGetLastError() == XWSAEWOULDBLOCK; }

void CloseConn(int i) {
  Conn& c = g_conns[i];
  if (c.file && c.file != INVALID_HANDLE_VALUE) CloseHandle(c.file);
  c.file = NULL;
  if (c.holds_mail) {
    sp::Guard g(g_mail_lock);
    g_mail_state = kMailIdle;
    g_mail_owner = -1;
  }
  c.holds_mail = false;
  if ((c.state == kWaitApi || c.state == kStreamWaitApi) && g_mail_owner == i) {
    sp::Guard g(g_mail_lock);
    // Posted but not answered yet: the answer is thrown away when it comes (see PumpApi).
    g_mail_owner = -1;
  }
  if (c.s != XINVALID_SOCKET) NetDll_closesocket(XNCALLER_TITLE, c.s);
  c.s = XINVALID_SOCKET;
  c.state = kFree;
}

void StartSend(Conn& c, const char* out, int len) {
  c.out = out;
  c.out_len = len;
  c.out_sent = 0;
  c.state = kSending;
}

// A short response with its body (errors and the like).
void RespondSmall(Conn& c, int status, const char* text) {
  const int body = (int)strlen(text);
  int n = rh::ResponseHeader(c.small, kSmallCap - body - 1, status, "text/plain; charset=utf-8",
                             (unsigned)body, rh::kNoStore, c.keep_alive);
  memcpy(c.small + n, text, body);
  StartSend(c, c.small, n + body);
}

void RespondFile(Conn& c, const char* rel) {
  char path[MAX_PATH];
  sprintf_s(path, sizeof(path), "%s\\%s", g_root, rel);
  HANDLE f = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) {
    RespondSmall(c, 404, "Not found");
    return;
  }
  const DWORD size = GetFileSize(f, NULL);
  // The page itself is never cached (a new remote.html shows at once); pictures are, for a week.
  const bool page = !strncmp(rel, "UI\\remote", 9);
  const int n = rh::ResponseHeader(c.small, kSmallCap, 200, rh::ContentType(rel), size,
                                   page ? rh::kNoStore : rh::kCacheWeek, c.keep_alive);
  c.file = f;
  c.file_pos = 0;
  c.file_left = size;
  StartSend(c, c.small, n);
}

// The next request on a connection, if it has all arrived.
void HandleRequest(int i) {
  Conn& c = g_conns[i];
  const int end = rh::HeadersEnd(c.request, c.request_len);
  if (end < 0) {
    if (c.request_len >= kRequestCap) {  // headers far too long
      c.keep_alive = false;
      RespondSmall(c, 400, "Bad request");
    }
    return;
  }
  char method[8], path[256], query[256];
  const bool ok = rh::ParseRequestLine(c.request, end, method, sizeof(method), path, sizeof(path),
                                       query, sizeof(query));
  c.keep_alive = ok && !rh::WantsClose(c.request, end);
  // Whatever followed this request (a pipelined next one) waits its turn.
  memmove(c.request, c.request + end, c.request_len - end);
  c.request_len -= end;
  c.request_started = GetTickCount();
  if (!ok) {
    RespondSmall(c, 400, "Bad request");
    return;
  }
  ++g_requests;
  const bool get = !strcmp(method, "GET");
  const bool post = !strcmp(method, "POST");
  if (!get && !post) {
    RespondSmall(c, 405, "Only GET and POST");
    return;
  }
  if (!strcmp(path, "/") || !strcmp(path, "/index.html") || !strcmp(path, "/remote.html")) {
    RespondFile(c, "UI\\remote.html");
  } else if (!strcmp(path, "/api/events")) {
    // An event stream: no length, it lasts until one side closes it.
    static const char kHead[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: keep-alive\r\n"
        "X-Accel-Buffering: no\r\n"
        "\r\n"
        "retry: 1000\n\n";
    c.stream = true;
    c.keep_alive = true;
    c.stream_rev = -1;
    memcpy(c.small, kHead, sizeof(kHead) - 1);
    StartSend(c, c.small, (int)sizeof(kHead) - 1);
  } else if (!strncmp(path, "/api/", 5)) {
    // Actions only by POST, so a browser preloading a link never puts a figure on.
    if ((!strcmp(path, "/api/do") || !strcmp(path, "/api/nick") || !strcmp(path, "/api/lists")) && !post) {
      RespondSmall(c, 405, "Use POST");
      return;
    }
    strncpy_s(c.api_path, sizeof(c.api_path), path, _TRUNCATE);
    strncpy_s(c.api_query, sizeof(c.api_query), query, _TRUNCATE);
    c.state = kWaitApi;  // posted to the worker when the slot is free (PumpApi)
  } else if (!strcmp(path, "/favicon.ico")) {
    RespondSmall(c, 404, "No icon");
  } else {
    char rel[MAX_PATH];
    if (!rh::SafeFilePath(path, rel, sizeof(rel))) {
      RespondSmall(c, 404, "Not found");
      return;
    }
    RespondFile(c, rel);
  }
}

// API calls: post the next waiting one; hand an answer to its connection. Requests come first;
// event streams whose rift state is out of date get theirs after.
void PumpApi() {
  sp::Guard g(g_mail_lock);
  if (g_mail_state == kMailAnswered && g_mail_owner >= 0) {
    Conn& c = g_conns[g_mail_owner];
    if (c.state == kWaitApi) {
      if (g_mail_len < 0) {
        g_mail_state = kMailIdle;
        g_mail_owner = -1;
        RespondSmall(c, 404, "Unknown call");
        return;
      }
      char head[kHeadRoom];
      const int n = rh::ResponseHeader(head, sizeof(head), 200, "application/json",
                                       (unsigned)g_mail_len, rh::kNoStore, c.keep_alive);
      char* start = g_mail_out + kHeadRoom - n;
      memcpy(start, head, n);
      c.holds_mail = true;  // until it's sent
      StartSend(c, start, n + g_mail_len);
      return;
    }
    if (c.state == kStreamWaitApi && g_mail_len > 0) {
      // "data: {...}\n\n": the prefix in the head room, the blank line after the JSON.
      char* start = g_mail_out + kHeadRoom - 6;
      memcpy(start, "data: ", 6);
      start[6 + g_mail_len] = '\n';
      start[7 + g_mail_len] = '\n';
      c.stream_rev = c.pending_rev;
      c.holds_mail = true;
      StartSend(c, start, g_mail_len + 8);
      return;
    }
  }
  if (g_mail_state == kMailAnswered) {  // its connection went away (or the state didn't fit)
    if (g_mail_owner >= 0 && g_conns[g_mail_owner].state == kStreamWaitApi) {
      g_conns[g_mail_owner].state = kStreaming;
    }
    g_mail_state = kMailIdle;
    g_mail_owner = -1;
  }
  if (g_mail_state != kMailIdle) return;
  for (int k = 0; k < kMaxConns; ++k) {
    Conn& c = g_conns[k];
    if (c.state != kWaitApi) continue;
    strncpy_s(g_mail_path, sizeof(g_mail_path), c.api_path, _TRUNCATE);
    strncpy_s(g_mail_query, sizeof(g_mail_query), c.api_query, _TRUNCATE);
    g_mail_len = -1;
    g_mail_owner = k;
    g_mail_state = kMailWaiting;
    return;
  }
  const LONG rev = g_rev;
  for (int k = 0; k < kMaxConns; ++k) {
    Conn& c = g_conns[k];
    if (c.state != kStreaming || c.stream_rev == rev) continue;
    strncpy_s(g_mail_path, sizeof(g_mail_path), "/api/state", _TRUNCATE);
    g_mail_query[0] = 0;
    g_mail_len = -1;
    g_mail_owner = k;
    g_mail_state = kMailWaiting;
    c.pending_rev = rev;
    c.state = kStreamWaitApi;
    return;
  }
}

// Sends what it can without waiting. False when the connection is finished with (closed).
bool PumpSend(int i) {
  Conn& c = g_conns[i];
  for (;;) {
    int n;
    if (c.out_sent < c.out_len) {
      // Headers (and a small body) from memory; a file's first data goes in the same send.
      int len = c.out_len - c.out_sent;
      const char* data = c.out + c.out_sent;
      if (c.file && c.file_left > 0 && len < (int)sizeof(g_chunk)) {
        memcpy(g_chunk, data, len);
        DWORD got = 0;
        DWORD want = (DWORD)sizeof(g_chunk) - (DWORD)len;
        if (want > c.file_left) want = c.file_left;
        SetFilePointer(c.file, (LONG)c.file_pos, NULL, FILE_BEGIN);
        if (ReadFile(c.file, g_chunk + len, want, &got, NULL) && got > 0) {
          n = NetDll_send(XNCALLER_TITLE, c.s, g_chunk, (DWORD)(len + got), 0);
          if (n < 0) return WouldBlock() ? true : (CloseConn(i), false);
          if (n <= len) {
            c.out_sent += n;
          } else {
            c.out_sent = c.out_len;
            c.file_pos += n - len;
            c.file_left -= n - len;
          }
          continue;
        }
      }
      n = NetDll_send(XNCALLER_TITLE, c.s, data, (DWORD)len, 0);
      if (n < 0) return WouldBlock() ? true : (CloseConn(i), false);
      c.out_sent += n;
      continue;
    }
    if (c.file && c.file_left > 0) {
      DWORD want = c.file_left < sizeof(g_chunk) ? c.file_left : (DWORD)sizeof(g_chunk);
      DWORD got = 0;
      SetFilePointer(c.file, (LONG)c.file_pos, NULL, FILE_BEGIN);
      if (!ReadFile(c.file, g_chunk, want, &got, NULL) || got == 0) {
        CloseConn(i);  // the file went short: the response can't be finished
        return false;
      }
      n = NetDll_send(XNCALLER_TITLE, c.s, g_chunk, got, 0);
      if (n < 0) return WouldBlock() ? true : (CloseConn(i), false);
      c.file_pos += n;
      c.file_left -= n;
      if ((DWORD)n < got) return true;  // the rest when there's room
      continue;
    }
    break;
  }
  // All sent.
  if (c.file) {
    CloseHandle(c.file);
    c.file = NULL;
  }
  if (c.holds_mail) {
    sp::Guard g(g_mail_lock);
    g_mail_state = kMailIdle;
    g_mail_owner = -1;
    c.holds_mail = false;
  }
  c.last_active = c.last_send = GetTickCount();
  if (c.stream) {
    c.state = kStreaming;
    return true;
  }
  if (!c.keep_alive) {
    NetDll_shutdown(XNCALLER_TITLE, c.s, XSD_SEND);
    CloseConn(i);
    return false;
  }
  c.state = kReading;
  if (c.request_len > 0) HandleRequest(i);  // a pipelined request was already here
  return true;
}

// Reads what has arrived. False when the connection is finished with (closed).
bool PumpRead(int i) {
  Conn& c = g_conns[i];
  if (c.request_len >= kRequestCap && !c.stream) return true;
  char discard[64];
  const int n = c.stream ? NetDll_recv(XNCALLER_TITLE, c.s, discard, sizeof(discard), 0)
                         : NetDll_recv(XNCALLER_TITLE, c.s, c.request + c.request_len,
                                       (DWORD)(kRequestCap - c.request_len), 0);
  if (n == 0) {  // the phone closed it (as browsers do with idle connections)
    CloseConn(i);
    return false;
  }
  if (n < 0) {
    if (WouldBlock()) return true;
    CloseConn(i);
    return false;
  }
  if (c.stream) return true;  // nothing is expected on an event stream; only its closing matters
  if (c.request_len == 0) c.request_started = GetTickCount();
  c.request_len += n;
  c.last_active = GetTickCount();
  if (c.state == kReading) HandleRequest(i);
  return true;
}

void Accept(XSOCKET listener) {
  int slot = -1;
  for (int k = 0; k < kMaxConns; ++k) {
    if (g_conns[k].state == kFree) {
      slot = k;
      break;
    }
  }
  if (slot < 0) {
    // All busy: the longest-idle connection that hasn't asked for anything yet (a browser's spare)
    // makes room; otherwise the new one waits in the backlog until a slot frees up.
    const DWORD now = GetTickCount();
    DWORD idlest = 0;
    for (int k = 0; k < kMaxConns; ++k) {
      const Conn& c = g_conns[k];
      if (c.state == kReading && c.request_len == 0 && now - c.last_active > 300 &&
          now - c.last_active >= idlest) {
        idlest = now - c.last_active;
        slot = k;
      }
    }
    if (slot < 0) return;
    CloseConn(slot);
  }
  XSOCKADDR_IN from;
  DWORD from_len = sizeof(from);
  const XSOCKET s = NetDll_accept(XNCALLER_TITLE, listener, &from, &from_len);
  if (s == XINVALID_SOCKET) return;
  SetNonBlocking(s);
  SetOption(s, XSOL_SOCKET, XSO_MARKINSECURE, 1);
  SetOption(s, XIPPROTO_TCP, XTCP_NODELAY, 1);
  SetOption(s, XSOL_SOCKET, XSO_SNDBUF, 64 * 1024);
  Conn& c = g_conns[slot];
  c.s = s;
  c.state = kReading;
  c.request_len = 0;
  c.keep_alive = true;
  c.out = NULL;
  c.out_len = c.out_sent = 0;
  c.holds_mail = false;
  c.file = NULL;
  c.stream = false;
  c.stream_rev = c.pending_rev = -1;
  c.last_active = c.request_started = c.last_send = GetTickCount();
}

// The console's address on the home network ("" while it has none yet).
bool LocalAddress(char* out, int cap) {
  XXNADDR addr;
  memset(&addr, 0, sizeof(addr));
  const DWORD status = NetDll_XNetGetTitleXnAddr(XNCALLER_TITLE, &addr);
  if (status == 0 /* pending */ || (status & 1) /* none */ || !addr.ina) return false;
  const DWORD a = addr.ina;  // big-endian: the first byte is the top one
  sprintf_s(out, cap, "%u.%u.%u.%u", (a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255, a & 255);
  return true;
}

XSOCKET Listen(int port) {
  const XSOCKET s = NetDll_socket(XNCALLER_TITLE, XAF_INET, XSOCK_STREAM, XIPPROTO_TCP);
  if (s == XINVALID_SOCKET) {
    util::Log("Phone remote: no socket (error %u)", NetDll_WSAGetLastError());
    return XINVALID_SOCKET;
  }
  if (!SetOption(s, XSOL_SOCKET, XSO_MARKINSECURE, 1)) {
    util::Log("Phone remote: the console refused a plain (insecure) socket (error %u)",
              NetDll_WSAGetLastError());
  }
  SetOption(s, XSOL_SOCKET, XSO_REUSEADDR, 1);
  XSOCKADDR_IN a;
  memset(&a, 0, sizeof(a));
  a.sin_family = XAF_INET;
  a.sin_port = (WORD)port;
  a.sin_addr = 0;  // every address the console has
  if (NetDll_bind(XNCALLER_TITLE, s, &a, sizeof(a)) != 0) {
    util::Log("Phone remote: port %d is taken (error %u)", port, NetDll_WSAGetLastError());
    NetDll_closesocket(XNCALLER_TITLE, s);
    return XINVALID_SOCKET;
  }
  if (NetDll_listen(XNCALLER_TITLE, s, 16) != 0) {
    util::Log("Phone remote: can't listen on port %d (error %u)", port, NetDll_WSAGetLastError());
    NetDll_closesocket(XNCALLER_TITLE, s);
    return XINVALID_SOCKET;
  }
  SetNonBlocking(s);
  return s;
}

void AddToSet(XFD_SET* set, XSOCKET s) {
  if (set->fd_count < 64) set->fd_array[set->fd_count++] = s;
}

bool InSet(const XFD_SET* set, XSOCKET s) {
  for (DWORD k = 0; k < set->fd_count; ++k) {
    if (set->fd_array[k] == s) return true;
  }
  return false;
}

DWORD WINAPI ServerThread(LPVOID) {
  // A little above the game's normal threads: it sleeps in select() almost all the time, and
  // when the phone asks for something it should get it without waiting behind the game.
#if defined(SKYPORTAL_XRT)
  DWORD kthread = 0;
  __asm__ volatile("lwz %0, 0x100(13)" : "=r"(kthread));  // KeGetCurrentThread(): PCR->CurrentThread
  if (kthread) KeSetBasePriorityThread(kthread, 2);
#else
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
  for (int k = 0; k < kMaxConns; ++k) {
    g_conns[k].s = XINVALID_SOCKET;
    g_conns[k].state = kFree;
    g_conns[k].file = NULL;
  }
  // Let the game start up (and set up its own networking) first.
  for (DWORD waited = 0; waited < g_delay_ms; waited += 100) Sleep(100);
  // Both just count up when the game has already started them.
  const int xnet = NetDll_XNetStartup(XNCALLER_TITLE, NULL);
  static XWSADATA wsa;
  const int wsa_result = NetDll_WSAStartup(XNCALLER_TITLE, 0x0202, &wsa);
  if (xnet != 0 || wsa_result != 0) {
    util::Log("Phone remote: network start returned %d / %d", xnet, wsa_result);
  }
  int port = g_port;
  XSOCKET listener = XINVALID_SOCKET;
  for (int tries = 0; tries < 30 && listener == XINVALID_SOCKET; ++tries) {
    listener = Listen(port);
    if (listener == XINVALID_SOCKET && port != 8080) {
      port = 8080;
      listener = Listen(port);
    }
    if (listener == XINVALID_SOCKET) Sleep(2000);
  }
  if (listener == XINVALID_SOCKET) {
    g_state = kFailed;
    util::Log("Phone remote: off (couldn't open a port)");
    return 0;
  }
  // The address can take a few seconds after the network comes up (DHCP).
  char ip[20] = "";
  for (int tries = 0; tries < 100 && !LocalAddress(ip, sizeof(ip)); ++tries) Sleep(100);
  if (!ip[0]) strncpy_s(ip, sizeof(ip), "this Xbox's IP address", _TRUNCATE);
  if (port == 80) {
    strncpy_s(g_address, sizeof(g_address), ip, _TRUNCATE);
  } else {
    sprintf_s(g_address, sizeof(g_address), "%s:%d", ip, port);
  }
  g_state = kListening;
  util::Log("Phone remote: listening - open http://%s on a phone on the same network", g_address);

  DWORD failures = 0;
  for (;;) {
    PumpApi();
    static XFD_SET readable, writable;
    readable.fd_count = writable.fd_count = 0;
    bool room = false, waiting_api = false;
    for (int k = 0; k < kMaxConns; ++k) {
      const Conn& c = g_conns[k];
      if (c.state == kFree) room = true;
      else if (c.state == kReading || c.state == kStreaming) AddToSet(&readable, c.s);
      else if (c.state == kSending) AddToSet(&writable, c.s);
      else waiting_api = true;  // kWaitApi, kStreamWaitApi
      if (c.state == kStreaming && c.stream_rev != g_rev) waiting_api = true;
      if (c.state == kReading && c.request_len == 0) room = true;  // can be made room for
    }
    if (room) AddToSet(&readable, listener);
    // Short waits while the worker has an answer coming (it wakes for the phone within 2 ms);
    // otherwise a quarter of a second.
    XTIMEVAL wait = {0, waiting_api ? 1000 : 250000};
    const int ready = (readable.fd_count || writable.fd_count)
                          ? NetDll_select(XNCALLER_TITLE, 0, &readable, &writable, NULL, &wait)
                          : (Sleep(waiting_api ? 1 : 50), 0);
    if (ready < 0) {
      // Something's wrong with a socket: drop the connections, and if it keeps happening, open
      // the listening socket again. The server never gives up while the game runs.
      ++failures;
      util::Log("Phone remote: select failed (error %u)", NetDll_WSAGetLastError());
      for (int k = 0; k < kMaxConns; ++k) {
        if (g_conns[k].state != kFree) CloseConn(k);
      }
      if (failures % 3 == 0) {
        NetDll_closesocket(XNCALLER_TITLE, listener);
        listener = XINVALID_SOCKET;
        while (listener == XINVALID_SOCKET) {
          Sleep(2000);
          listener = Listen(port);
        }
      } else {
        Sleep(200);
      }
      continue;
    }
    if (ready > 0) {
      for (int k = 0; k < kMaxConns; ++k) {
        Conn& c = g_conns[k];
        if ((c.state == kReading || c.state == kStreaming) && InSet(&readable, c.s)) PumpRead(k);
        else if (c.state == kSending && InSet(&writable, c.s)) PumpSend(k);
      }
      if (room && InSet(&readable, listener)) Accept(listener);
    }
    // Quiet connections, and requests that never finished arriving.
    const DWORD now = GetTickCount();
    for (int k = 0; k < kMaxConns; ++k) {
      Conn& c = g_conns[k];
      if (c.state == kReading && (now - c.last_active > kIdleMs ||
                                  (c.request_len > 0 && now - c.request_started > kRequestMs))) {
        CloseConn(k);
      } else if (c.state == kSending && now - c.last_active > kIdleMs) {
        CloseConn(k);  // the phone stopped taking data
      } else if (c.state == kStreaming && now - c.last_send > kPingMs) {
        static const char kPing[] = "event: ping\ndata: 1\n\n";
        memcpy(c.small, kPing, sizeof(kPing) - 1);
        StartSend(c, c.small, (int)sizeof(kPing) - 1);  // a dead connection fails here
      }
    }
  }
}

}  // namespace

bool Start(const char* root, int port, DWORD delay_ms) {
  if (g_state != kOff) return true;
  strncpy_s(g_root, sizeof(g_root), root, _TRUNCATE);
  g_port = port > 0 && port < 65536 ? port : 80;
  g_delay_ms = delay_ms;
  g_state = kStarting;
#if defined(SKYPORTAL_SIDELOAD)
  if (!util::StartThread(ServerThread, 0x10000)) {  // (a thread of the game: this DLL lives in it)
    g_state = kFailed;
    util::Log("Phone remote: couldn't start its thread");
    return false;
  }
#else
  // Dashlaunch: the worker is a system thread, but the server uses the game's network (as the
  // game's own code would) and has to end with it, so it runs as one of the game's threads.
  util::RequestGameThread(ServerThread, 0x10000);
  util::Log("Phone remote: starting with the game");
#endif
  return true;
}

#if !defined(SKYPORTAL_SIDELOAD)
void TitleEnded() {
  // The game's threads are gone, the server's among them, and its sockets and open files with
  // the game's network and process: forget them (never close them: those handles are the
  // game's, and gone). The mailbox lock is made free again in case the server held it.
  util::CancelGameThread();
  g_mail_lock.Reinit();
  g_mail_state = kMailIdle;
  g_mail_owner = -1;
  g_mail_len = -1;
  for (int k = 0; k < kMaxConns; ++k) {
    g_conns[k].s = XINVALID_SOCKET;
    g_conns[k].state = kFree;
    g_conns[k].file = NULL;
    g_conns[k].holds_mail = false;
  }
  const bool was = g_state != kOff;
  g_address[0] = 0;
  g_state = kOff;
  if (was) util::Log("Phone remote: stopped with the game");
}
#endif

void Service(ApiHandler handler) {
  sp::Guard g(g_mail_lock);
  if (g_mail_state != kMailWaiting) return;
  // (2 bytes kept free after the answer, for an event stream's closing blank line)
  g_mail_len = handler(g_mail_path, g_mail_query, g_mail_out + kHeadRoom, kApiCap - 2);
  g_mail_state = kMailAnswered;
}

bool Waiting() { return g_mail_state == kMailWaiting; }  // (a hint: read without the lock)

void Changed() { InterlockedIncrement(&g_rev); }

LONG Revision() { return g_rev; }

const char* Address() { return g_state == kListening ? g_address : ""; }

State GetState() { return g_state; }

DWORD Requests() { return g_requests; }

}  // namespace remote
