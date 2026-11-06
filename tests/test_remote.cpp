// Native tests for the phone remote's HTTP helpers (src/core/remote_http.cpp): request parsing,
// query values, which files may be handed out, JSON escaping, response headers.
#include <stdio.h>
#include <string.h>

#include "../src/core/remote_http.h"

static int g_fail = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);         \
      ++g_fail;                                                     \
    }                                                               \
  } while (0)

static bool Parse(const char* req, char* m, char* p, char* q) {
  return rh::ParseRequestLine(req, (int)strlen(req), m, 8, p, 256, q, 256);
}

int main() {
  printf("request line\n");
  char m[8], p[256], q[256];
  CHECK(Parse("GET / HTTP/1.1\r\nHost: x\r\n\r\n", m, p, q) && !strcmp(m, "GET") && !strcmp(p, "/") && !q[0]);
  CHECK(Parse("POST /api/do?a=add&i=12 HTTP/1.1\r\n\r\n", m, p, q) && !strcmp(m, "POST") &&
        !strcmp(p, "/api/do") && !strcmp(q, "a=add&i=12"));
  CHECK(Parse("GET /Images/Cards%201/Chop%20Chop.qoi HTTP/1.1\r\n", m, p, q) &&
        !strcmp(p, "/Images/Cards 1/Chop Chop.qoi"));
  CHECK(Parse("GET /Images/Cards%201/Eon's%20Elite%20Spyro.qoi HTTP/1.1\r\n", m, p, q) &&
        !strcmp(p, "/Images/Cards 1/Eon's Elite Spyro.qoi"));
  CHECK(!Parse("GET\r\n", m, p, q));
  CHECK(!Parse("GET nothing HTTP/1.1\r\n", m, p, q));
  CHECK(!Parse("GET /no-version\r\n", m, p, q));
  CHECK(!Parse("VERYLONGMETHOD / HTTP/1.1\r\n", m, p, q));
  char longreq[600] = "GET /";
  for (int i = 0; i < 300; ++i) strcat(longreq, "a");
  strcat(longreq, " HTTP/1.1\r\n");
  CHECK(!Parse(longreq, m, p, q));  // longer than the path buffer: refused, not cut short

  printf("query values\n");
  char v[64];
  CHECK(rh::QueryValue("a=add&i=12", "a", v, sizeof(v)) && !strcmp(v, "add"));
  CHECK(rh::QueryValue("a=add&i=12", "i", v, sizeof(v)) && !strcmp(v, "12"));
  CHECK(!rh::QueryValue("aa=1&b=2", "a", v, sizeof(v)));
  CHECK(rh::QueryValue("q=Chop+Chop%21", "q", v, sizeof(v)) && !strcmp(v, "Chop Chop!"));
  CHECK(rh::QueryInt("v=3&from=180", "from", -1) == 180);
  CHECK(rh::QueryInt("v=3", "from", -1) == -1);
  CHECK(rh::QueryInt("i=-1", "i", 7) == -1);
  CHECK(rh::QueryInt("i=12x", "i", 7) == 7);
  CHECK(rh::QueryInt("i=99999999999", "i", 7) == 7);

  printf("files the remote may hand out\n");
  char rel[260];
  CHECK(rh::SafeFilePath("/UI/remote.html", rel, sizeof(rel)) && !strcmp(rel, "UI\\remote.html"));
  CHECK(rh::SafeFilePath("/Images/Cards 1/Chop Chop.qoi", rel, sizeof(rel)) &&
        !strcmp(rel, "Images\\Cards 1\\Chop Chop.qoi"));
  CHECK(!rh::SafeFilePath("/Figures/Spyro.dump", rel, sizeof(rel)));      // never the dumps
  CHECK(!rh::SafeFilePath("/staticrift.ini", rel, sizeof(rel)));
  CHECK(!rh::SafeFilePath("/UI/../staticrift.ini", rel, sizeof(rel)));
  CHECK(!rh::SafeFilePath("/Images/..\\x", rel, sizeof(rel)));
  CHECK(!rh::SafeFilePath("/UI/x:y", rel, sizeof(rel)));
  CHECK(!rh::SafeFilePath("/UI//x", rel, sizeof(rel)));
  CHECK(!rh::SafeFilePath("/UI/", rel, sizeof(rel)));
  CHECK(!rh::SafeFilePath("UI/remote.html", rel, sizeof(rel)));
  CHECK(rh::SafeFilePath("/Phone/Cards 1/Chop Chop.jpg", rel, sizeof(rel)) && !strcmp(rel, "Phone\\Cards 1\\Chop Chop.jpg"));
  CHECK(!strcmp(rh::ContentType("x.jpg"), "image/jpeg"));
  CHECK(!strcmp(rh::ContentType("UI\\remote.html"), "text/html; charset=utf-8"));
  CHECK(!strcmp(rh::ContentType("x.qoi"), "application/octet-stream"));
  CHECK(!strcmp(rh::ContentType("x.PNG"), "image/png"));

  printf("JSON\n");
  char buf[128];
  rh::Json j(buf, sizeof(buf));
  j.Raw("[");
  j.Str("Eon's \"Elite\" \\ Spyro");
  j.Char(',');
  j.PathStr("Images\\Cards 1\\Spyro.qoi");
  j.Char(',');
  j.Int(-42);
  j.Char(',');
  j.Bool(true);
  j.Char(',');
  j.Str("a\nb\x01");
  j.Raw("]");
  CHECK(!j.Overflow());
  CHECK(!strcmp(buf, "[\"Eon's \\\"Elite\\\" \\\\ Spyro\",\"Images/Cards 1/Spyro.qoi\",-42,true,\"a\\u000ab\\u0001\"]"));
  char tiny[8];
  rh::Json t(tiny, sizeof(tiny));
  t.Str("too long for this");
  CHECK(t.Overflow() && t.Len() == 7 && strlen(tiny) == 7);

  printf("response header\n");
  char head[256];
  const int n = rh::ResponseHeader(head, sizeof(head), 200, "application/json", 123, rh::kNoStore, true);
  CHECK(n == (int)strlen(head));
  CHECK(!strncmp(head, "HTTP/1.1 200 OK\r\n", 17) && strstr(head, "Content-Length: 123\r\n") &&
        strstr(head, "Cache-Control: no-store\r\n") && strstr(head, "Connection: keep-alive\r\n") &&
        !strcmp(head + n - 4, "\r\n\r\n"));
  rh::ResponseHeader(head, sizeof(head), 404, "text/plain", 9, rh::kCacheDay, false);
  CHECK(!strncmp(head, "HTTP/1.1 404 Not Found", 22) && strstr(head, "max-age=86400") &&
        strstr(head, "Connection: close\r\n"));
  rh::ResponseHeader(head, sizeof(head), 200, "image/jpeg", 9, rh::kCacheWeek, true);
  CHECK(strstr(head, "max-age=604800") != NULL);

  printf("keep-alive\n");
  const char kReq[] = "GET / HTTP/1.1\r\nHost: x\r\nConnection: Close\r\n\r\nGET /next";
  const int end = rh::HeadersEnd(kReq, (int)strlen(kReq));
  CHECK(end > 0 && !strncmp(kReq + end, "GET /next", 9));
  CHECK(rh::WantsClose(kReq, end));
  CHECK(!rh::WantsClose("GET / HTTP/1.1\r\nConnection: keep-alive\r\n\r\n", 44));
  CHECK(!rh::WantsClose("GET / HTTP/1.1\r\nX-Note: close\r\n\r\n", 36));
  CHECK(rh::HeadersEnd("GET / HTTP/1.1\r\nHost: x\r\n", 25) == -1);

  if (g_fail) {
    printf("%d check(s) failed\n", g_fail);
    return 1;
  }
  printf("all remote tests passed\n");
  return 0;
}
