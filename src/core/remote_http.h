// Project Kaos-Byte - the phone remote's HTTP helpers (portable: no Xbox headers, C++03).
//
// The plugin serves one page (The Static Rift\UI\remote.html), its pictures (UI\..., Images\...)
// and a small JSON API to a phone browser on the same network. These are the pieces that don't
// touch sockets, so they build natively for tests/test_remote.cpp too.
#pragma once

namespace rh {

// "GET /api/list?v=0&from=0 HTTP/1.1\r\n..." -> method "GET", path "/api/list" (URL-decoded),
// query "v=0&from=0" (still encoded). False when the line is malformed or too long.
bool ParseRequestLine(const char* request, int len, char* method, int method_cap, char* path,
                      int path_cap, char* query, int query_cap);

// %XX escapes (and '+' as a space when plus_is_space) decoded in place.
void UrlDecode(char* s, bool plus_is_space);

// The value of `key` in "a=1&b=two" (decoded). False when it isn't there.
bool QueryValue(const char* query, const char* key, char* out, int cap);
int QueryInt(const char* query, const char* key, int fallback);

// A URL path of a file the remote may hand out, as a path under the data folder:
// "/UI/elements/magic.qoi" -> "UI\elements\magic.qoi". Only UI\, Images\ and Phone\ (the phone's
// small pictures), no "..", no drive letters or other tricks. False when refused.
bool SafeFilePath(const char* url_path, char* out, int cap);

// Where a request's headers end in buf (the index just past "\r\n\r\n"), or -1 if they haven't
// all arrived yet. buf needn't be NUL-terminated.
int HeadersEnd(const char* buf, int len);
// True when the request (its headers, `len` bytes) asks for "Connection: close".
bool WantsClose(const char* request, int len);

// Content type for a file name, by extension.
const char* ContentType(const char* path);

// A bounded JSON writer: never writes past the buffer; Overflow() says it ran out.
class Json {
 public:
  Json(char* buf, int cap);
  void Raw(const char* text);
  void Char(char c);
  void Str(const char* text);   // quoted and escaped; backslashes in paths become '/'
  void PathStr(const char* text);
  void Int(int value);
  void Bool(bool value) { Raw(value ? "true" : "false"); }
  int Len() const { return len_; }
  int Room() const { return cap_ - 1 - len_; }
  bool Overflow() const { return overflow_; }
 private:
  void Put(char c);
  char* buf_;
  int cap_;
  int len_;
  bool overflow_;
};

// How long the browser may keep a response.
enum Cache { kNoStore = 0, kCacheDay, kCacheWeek };

// "HTTP/1.1 200 OK\r\nContent-Type: ...\r\nContent-Length: N\r\n...\r\n\r\n" into out.
// keep_alive: the connection stays open for the next request (else "Connection: close").
// Returns its length.
int ResponseHeader(char* out, int cap, int status, const char* content_type, unsigned length,
                   Cache cache, bool keep_alive);

}  // namespace rh
