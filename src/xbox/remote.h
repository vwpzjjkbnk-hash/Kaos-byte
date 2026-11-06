// Project Kaos-Byte - the phone remote: a small web server inside the plugin.
//
// While a Skylanders game runs, a phone (or any browser) on the same network opens
// http://<the Xbox's address>/ and gets The Static Rift\UI\remote.html, which lists the figures
// and puts them on the rift. The server thread only moves bytes: anything that touches the figure
// library or the rift is handed to the plugin's worker thread (Service), the same thread the
// controller menu runs on, so the phone and the menu never step on each other.
#pragma once

#include "stdafx.h"

namespace remote {

// Answers one API call ("/api/state", "/api/list", "/api/do") on the worker thread: writes JSON
// into out (at most cap bytes) and returns its length, or -1 for an unknown call.
typedef int (*ApiHandler)(const char* path, const char* query, char* out, int cap);

// Starts the server thread: it waits `delay_ms` (so the game sets up its own networking first),
// then listens on `port` (falling back to 8080 if that's taken). `root` is the data folder.
// Dashlaunch build: the thread is one of the game's (started from a hook the game passes through,
// see util::RequestGameThread), so it and its sockets end with the game; call TitleEnded then,
// and Start again with the next game.
bool Start(const char* root, int port, DWORD delay_ms);
#if !defined(SKYPORTAL_SIDELOAD)
void TitleEnded();
#endif

// Worker thread, every loop: answers a waiting API call, if there is one.
void Service(ApiHandler handler);
// A call is waiting for Service (the worker wakes up for it straight away).
bool Waiting();

// The rift changed (worker thread): the phones' event streams get the new state straight away.
void Changed();
LONG Revision();

// "192.168.1.23" (port 80) or "192.168.1.23:8080" once the server is listening; "" until then.
const char* Address();

enum State { kOff = 0, kStarting, kListening, kFailed };
State GetState();
DWORD Requests();  // requests answered so far

}  // namespace remote
