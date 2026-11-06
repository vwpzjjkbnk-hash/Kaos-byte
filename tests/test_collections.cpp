// Native tests for nicknames and lists (src/core/collections.cpp): reading and writing
// nicknames.ini and lists.ini, cleaning up what's typed, list editing, and keeping the lines of
// figures that aren't in the Figures folder right now.
#include <stdio.h>
#include <string.h>

#include "../src/core/collections.h"

static int g_fail = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);         \
      ++g_fail;                                                     \
    }                                                               \
  } while (0)

// A pretend figure library.
static const char* const kRels[] = {
    "Skylanders\\1. Spyro's Adventure\\Undead\\Chop Chop.dump",
    "Skylanders\\1. Spyro's Adventure\\Magic\\Spyro.dump",
    "Skylanders\\4. Trap Team\\Air\\Gusto.dump",
    "My Hex.dump",
};
static const int kCount = (int)(sizeof(kRels) / sizeof(kRels[0]));
static int Find(const char* rel) {
  for (int i = 0; i < kCount; ++i) {
    if (!strcmp(kRels[i], rel)) return i;
  }
  return -1;
}
static const char* Rel(int id) { return id >= 0 && id < kCount ? kRels[id] : ""; }

static coll::Store g_store;  // big: kept off the stack
static coll::Store g_again;

int main() {
  coll::Store& s = g_store;
  char out[8192];

  printf("cleaning up what's typed\n");
  char c[32];
  CHECK(coll::Store::Clean("  Choppy  ", c, 20, false) == 6 && !strcmp(c, "Choppy"));
  CHECK(coll::Store::Clean("Big   Bad\tWolf", c, 20, false) && !strcmp(c, "Big Bad Wolf"));
  CHECK(coll::Store::Clean("caf\xC3\xA9\x01!", c, 20, false) && !strcmp(c, "caf!"));  // only ASCII
  CHECK(coll::Store::Clean("abcdefghijklmnopqrstuvwxyz", c, 20, false) == 20);
  CHECK(coll::Store::Clean("abcdefghijklmnopqrs tu", c, 20, false) == 19 && !strcmp(c, "abcdefghijklmnopqrs"));
  CHECK(coll::Store::Clean("[Speed] run", c, 24, true) && !strcmp(c, "Speed run"));
  CHECK(coll::Store::Clean("   ", c, 20, false) == 0 && !c[0]);
  CHECK(coll::Store::Clean(0, c, 20, false) == 0);

  printf("nicknames\n");
  s.Clear();
  CHECK(!s.Nick(0)[0]);
  CHECK(s.SetNick(0, " Choppy "));
  CHECK(!strcmp(s.Nick(0), "Choppy"));
  CHECK(!s.SetNick(0, "Choppy"));  // no change
  CHECK(s.SetNick(0, "Sir Chops"));
  CHECK(!strcmp(s.Nick(0), "Sir Chops"));
  CHECK(s.SetNick(3, "Hexy = the best"));
  CHECK(s.NickCount() == 2);
  CHECK(s.SetNick(0, ""));  // taken away
  CHECK(!s.Nick(0)[0] && s.NickCount() == 1 && !strcmp(s.Nick(3), "Hexy = the best"));
  CHECK(!s.SetNick(0, "  "));  // nothing to take away
  CHECK(!s.SetNick(-1, "x"));

  printf("nicknames.ini\n");
  const char* nick_file =
      "; comment\r\n"
      "[Nicknames]\r\n"
      "Skylanders\\1. Spyro's Adventure\\Undead\\Chop Chop.dump = Choppy\r\n"
      "  My Hex.dump=Hexy = the best  \n"
      "Gone\\Old Figure.dump = Old Pal\n"
      "no equals sign here\n"
      "Skylanders\\4. Trap Team\\Air\\Gusto.dump =   \n";
  s.LoadNicknames(nick_file, (int)strlen(nick_file), Find);
  CHECK(s.NickCount() == 2);
  CHECK(!strcmp(s.Nick(0), "Choppy") && !strcmp(s.Nick(3), "Hexy = the best") && !s.Nick(2)[0]);
  int n = s.SaveNicknames(out, sizeof(out), Rel);
  CHECK(n > 0 && (int)strlen(out) == n);
  CHECK(strstr(out, "Skylanders\\1. Spyro's Adventure\\Undead\\Chop Chop.dump = Choppy\r\n") != 0);
  CHECK(strstr(out, "My Hex.dump = Hexy = the best\r\n") != 0);
  CHECK(strstr(out, "Gone\\Old Figure.dump = Old Pal\r\n") != 0);  // kept, though it's not there
  g_again.LoadNicknames(out, n, Find);  // round trip
  CHECK(g_again.NickCount() == 2 && !strcmp(g_again.Nick(0), "Choppy"));
  CHECK(s.SaveNicknames(out, 40, Rel) == -1);  // doesn't fit: says so
  s.LoadNicknames(0, 0, Find);
  CHECK(s.NickCount() == 0);

  printf("lists\n");
  s.Clear();
  CHECK(s.ListCount() == 1 && !strcmp(s.ListName(0), "Favorites") && s.ListSize(0) == 0);
  CHECK(s.Add(0, 2) == 1 && s.Add(0, 0) == 1 && s.Add(0, 2) == 0);
  CHECK(s.ListSize(0) == 2 && s.ListIds(0)[0] == 2 && s.ListIds(0)[1] == 0);
  CHECK(s.InList(0, 0) && !s.InList(0, 1));
  CHECK(s.NewList("  Speedrun ") == 1 && !strcmp(s.ListName(1), "Speedrun"));
  CHECK(s.NewList("speedrun") == coll::Store::kNameTaken);
  CHECK(s.NewList("favorites") == coll::Store::kNameTaken);
  CHECK(s.NewList("  ") == coll::Store::kNoName);
  CHECK(s.NewList("Trap Team run") == 2);
  CHECK(s.Add(1, 1) == 1 && s.Add(1, 3) == 1 && s.Add(2, 2) == 1);
  CHECK(s.Add(7, 1) == -1 && s.Add(1, -3) == -1);
  CHECK(s.Rename(1, "Any% run") == 0 && !strcmp(s.ListName(1), "Any% run"));
  CHECK(s.Rename(1, "trap team RUN") == coll::Store::kNameTaken);
  CHECK(s.Rename(1, "ANY% RUN") == 0);  // its own name in other capitals is fine
  CHECK(s.Rename(0, "Faves") == coll::Store::kNoList);  // Favorites keeps its name
  CHECK(s.Remove(1, 1) && !s.Remove(1, 1) && s.ListSize(1) == 1);
  CHECK(!s.Delete(0));
  CHECK(s.Delete(1) && s.ListCount() == 2 && !strcmp(s.ListName(1), "Trap Team run") &&
        s.ListSize(1) == 1 && s.ListIds(1)[0] == 2);
  CHECK(s.FindList("TRAP TEAM RUN") == 1 && s.FindList("nope") == -1);
  for (int k = 0; k < 11; ++k) {
    char name[16] = "List x";
    name[5] = (char)('a' + k);
    CHECK(s.NewList(name) == 2 + k);
  }
  CHECK(s.ListCount() == coll::kMaxLists && s.NewList("One too many") == coll::Store::kFull);
  s.Clear();
  for (int k = 0; k < coll::kMaxEntries; ++k) CHECK(s.Add(0, k) == 1);
  CHECK(s.Add(0, 9999) == -1);  // full

  printf("lists.ini\n");
  const char* list_file =
      "; Your lists\r\n"
      "[Favorites]\r\n"
      "Skylanders\\4. Trap Team\\Air\\Gusto.dump\r\n"
      "Skylanders\\1. Spyro's Adventure\\Undead\\Chop Chop.dump\r\n"
      "Skylanders\\4. Trap Team\\Air\\Gusto.dump\r\n"   // twice: once
      "\r\n"
      "[Speed[run]]\r\n"
      "Gone\\Old Figure.dump\r\n"
      "My Hex.dump\r\n"
      "[]\r\n"
      "Skylanders\\1. Spyro's Adventure\\Magic\\Spyro.dump\r\n"   // a nameless section: skipped
      "[Second]\r\n"
      "Skylanders\\1. Spyro's Adventure\\Magic\\Spyro.dump\r\n";
  s.LoadLists(list_file, (int)strlen(list_file), Find);
  CHECK(s.ListCount() == 3);
  CHECK(s.ListSize(0) == 2 && s.ListIds(0)[0] == 2 && s.ListIds(0)[1] == 0);
  CHECK(!strcmp(s.ListName(1), "Speedrun") && s.ListSize(1) == 1 && s.ListIds(1)[0] == 3);
  CHECK(!strcmp(s.ListName(2), "Second") && s.ListSize(2) == 1 && s.ListIds(2)[0] == 1);
  n = s.SaveLists(out, sizeof(out), Rel);
  CHECK(n > 0);
  CHECK(strstr(out, "[Favorites]\r\nSkylanders\\4. Trap Team\\Air\\Gusto.dump\r\n"
                    "Skylanders\\1. Spyro's Adventure\\Undead\\Chop Chop.dump\r\n") != 0);
  CHECK(strstr(out, "[Speedrun]\r\nMy Hex.dump\r\nGone\\Old Figure.dump\r\n") != 0);  // kept
  g_again.LoadLists(out, n, Find);  // round trip
  CHECK(g_again.ListCount() == 3 && g_again.ListSize(0) == 2 && !strcmp(g_again.ListName(2), "Second"));
  CHECK(s.Delete(1));  // the kept line goes with its list
  n = s.SaveLists(out, sizeof(out), Rel);
  CHECK(n > 0 && !strstr(out, "Old Figure") && strstr(out, "[Second]\r\nSkylanders\\1. Spyro's Adventure\\Magic\\Spyro.dump"));
  CHECK(s.SaveLists(out, 60, Rel) == -1);
  s.LoadLists(0, 0, Find);
  CHECK(s.ListCount() == 1 && s.ListSize(0) == 0);
  // No Favorites section in the file: it's still there, first.
  const char* only_mine = "[Mine]\nMy Hex.dump\n";
  s.LoadLists(only_mine, (int)strlen(only_mine), Find);
  CHECK(s.ListCount() == 2 && !strcmp(s.ListName(0), "Favorites") && s.ListSize(1) == 1);

  if (g_fail) {
    printf("%d check(s) failed\n", g_fail);
    return 1;
  }
  printf("all nickname and list tests passed\n");
  return 0;
}
