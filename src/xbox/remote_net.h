// Project Kaos-Byte - the slice of XAM's socket API the phone remote uses (xam.xex NetDll_*
// exports; ordinals in xrt/imports.txt, checked against Xenia's xam_table.inc). Every call takes
// the caller type first: 1 = the running title, which the Peer Pressure build is part of.
#pragma once

#include "stdafx.h"

#define XNCALLER_TITLE 1

typedef DWORD XSOCKET;
#define XINVALID_SOCKET ((XSOCKET)0xFFFFFFFF)
#define XSOCKET_ERROR (-1)

#define XAF_INET 2
#define XSOCK_STREAM 1
#define XIPPROTO_TCP 6
#define XSOL_SOCKET 0xFFFF
#define XSO_RCVTIMEO 0x1006
#define XSO_SNDTIMEO 0x1005
#define XSO_SNDBUF 0x1001
#define XSO_REUSEADDR 0x0004
#define XTCP_NODELAY 0x0001   // level XIPPROTO_TCP
#define XFIONBIO 0x8004667E   // ioctlsocket: non-blocking on/off
#define XWSAEWOULDBLOCK 10035
// Turns off the console's encryption of the socket's traffic (an "insecure" socket, which plain
// TCP to a phone needs). Peer Pressure removes the xex privilege check that normally guards it.
#define XSO_MARKINSECURE 0x5801
#define XSD_SEND 1

typedef struct {
  WORD sin_family;
  WORD sin_port;   // network order = the console's own (big-endian) order
  DWORD sin_addr;
  BYTE sin_zero[8];
} XSOCKADDR_IN;

typedef struct {
  DWORD fd_count;
  XSOCKET fd_array[64];
} XFD_SET;

typedef struct {
  LONG tv_sec;
  LONG tv_usec;
} XTIMEVAL;

typedef struct {
  WORD wVersion;
  WORD wHighVersion;
  char szDescription[257];
  char szSystemStatus[129];
  WORD iMaxSockets;
  WORD iMaxUdpDg;
  DWORD lpVendorInfo;
} XWSADATA;  // 0x190 bytes

typedef struct {
  DWORD ina;        // the console's address on the local network
  DWORD inaOnline;
  WORD wPortOnline;
  BYTE abEnet[6];
  BYTE abOnline[20];
} XXNADDR;

#ifdef __cplusplus
extern "C" {
#endif
XSOCKET NetDll_socket(DWORD caller, DWORD af, DWORD type, DWORD protocol);
int NetDll_closesocket(DWORD caller, XSOCKET s);
int NetDll_shutdown(DWORD caller, XSOCKET s, DWORD how);
int NetDll_ioctlsocket(DWORD caller, XSOCKET s, DWORD cmd, DWORD* arg);
int NetDll_setsockopt(DWORD caller, XSOCKET s, DWORD level, DWORD optname, const void* optval,
                      DWORD optlen);
int NetDll_bind(DWORD caller, XSOCKET s, const XSOCKADDR_IN* name, DWORD namelen);
int NetDll_listen(DWORD caller, XSOCKET s, DWORD backlog);
XSOCKET NetDll_accept(DWORD caller, XSOCKET s, XSOCKADDR_IN* addr, DWORD* addrlen);
int NetDll_select(DWORD caller, DWORD nfds, XFD_SET* readfds, XFD_SET* writefds, XFD_SET* exceptfds,
                  const XTIMEVAL* timeout);
int NetDll_recv(DWORD caller, XSOCKET s, void* buf, DWORD len, DWORD flags);
int NetDll_send(DWORD caller, XSOCKET s, const void* buf, DWORD len, DWORD flags);
DWORD NetDll_WSAGetLastError(void);
int NetDll_WSAStartup(DWORD caller, DWORD version, XWSADATA* data);
int NetDll_XNetStartup(DWORD caller, const void* params);
DWORD NetDll_XNetGetTitleXnAddr(DWORD caller, XXNADDR* addr);
#ifdef __cplusplus
}
#endif
