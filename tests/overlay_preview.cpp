// Renders the skydrawn menu on the PC (the same code the console runs, strip by strip) into PPM
// files, for looking at the layout with real assets.
//   overlay_preview <assets dir: tools/mkassets.py's SkyPortal folder> <out-prefix>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elements.h"
#include "overlay_render.h"
#include "qoi.h"

namespace menu {
const char* GameName(int game) {
  static const char* const kNames[] = {"", "Spyro's Adventure", "Giants", "Swap Force",
                                       "Trap Team", "SuperChargers", "Imaginators"};
  return game >= 1 && game <= 6 ? kNames[game] : "";
}
}  // namespace menu


static overlay::Image Load(const char* dir, const char* rel) {
  overlay::Image img = {0, 0, 0};
  if (!rel || !rel[0]) return img;
  char path[1024];
  snprintf(path, sizeof(path), "%s/%s", dir, rel);
  for (char* p = path; *p; ++p) {
    if (*p == '\\') *p = '/';
  }
  FILE* f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "missing %s\n", path);
    return img;
  }
  static unsigned char buf[4 << 20];
  const int n = (int)fread(buf, 1, sizeof(buf), f);
  fclose(f);
  int w, h;
  if (!qoi::Info(buf, n, &w, &h)) return img;
  unsigned int* px = (unsigned int*)malloc((size_t)w * h * 4);
  if (!qoi::Decode(buf, n, px, w * h)) return img;
  img.w = w;
  img.h = h;
  img.px = px;
  return img;
}

static void Render(const char* assets, const char* out, int fb_w, int fb_h, const menu::View& v) {
  overlay::Scene s;
  s.view = &v;
  s.background = Load(assets, "UI/background.qoi");
  char rel[300] = "";
  if (v.image[0]) snprintf(rel, sizeof(rel), "Images/%s", v.image);
  s.figure = Load(assets, rel);
  rel[0] = 0;
  if (v.element) {
    char name[24];
    const char* n = elements::Name((elements::Element)v.element);
    int i = 0;
    for (; n[i]; ++i) name[i] = (char)(n[i] >= 'A' && n[i] <= 'Z' ? n[i] + 32 : n[i]);
    name[i] = 0;
    snprintf(rel, sizeof(rel), "UI/elements/%s.qoi", name);
  }
  s.element_icon = Load(assets, rel);
  rel[0] = 0;
  if (v.game) snprintf(rel, sizeof(rel), "UI/games/%d.qoi", v.game);
  s.game_logo = Load(assets, rel);

  unsigned int* strip = (unsigned int*)malloc((size_t)fb_w * 32 * 4);
  FILE* f = fopen(out, "wb");
  fprintf(f, "P6 %d %d 255\n", fb_w, fb_h);
  for (int y0 = 0; y0 < fb_h; y0 += 32) {
    overlay::Strip st;
    st.w = fb_w;
    st.h = fb_h;
    st.y0 = y0;
    st.rows = fb_h - y0 < 32 ? fb_h - y0 : 32;
    st.px = strip;
    overlay::RenderStrip(s, &st);
    for (int i = 0; i < fb_w * st.rows; ++i) {
      const unsigned char rgb[3] = {(unsigned char)(strip[i] >> 16), (unsigned char)(strip[i] >> 8),
                                    (unsigned char)strip[i]};
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
  free(strip);
  printf("%s\n", out);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: overlay_preview <assets SkyPortal dir> <out-prefix>\n");
    return 1;
  }
  const char* assets = argv[1];
  const char* prefix = argv[2];
  char path[512];
  menu::View v;

  // A Swap Force swapper half: long name, folder header, picture.
  memset(&v, 0, sizeof(v));
  v.pos = 312;
  v.count = 960;
  v.id = 5;
  v.name = "Bronze & Silver Trap Shadow Bottom";
  v.element = elements::kDark;
  v.game = 3;
  strcpy(v.series, "Chase Variant");
  strcpy(v.group, "Swap Force / Chase Variants / Bronze & Silver Trap Shadow");
  strcpy(v.image, "3. Swap Force\\Shadow Bottom.qoi");
  v.portal_count = 3;
  strcpy(v.portal_names, "Spyro, Tree Rex, Kaos (Doom Raider), Wildfire, Food Fight");
  strcpy(v.remote, "192.168.1.23");  // the phone remote's address under the brand
  snprintf(path, sizeof(path), "%s_swapper.ppm", prefix);
  Render(assets, path, 1280, 720, v);

  // A magic item: no element, no picture.
  memset(&v, 0, sizeof(v));
  v.pos = 40;
  v.count = 960;
  v.id = 6;
  v.name = "Platinum Treasure Chest";
  v.game = 2;
  strcpy(v.series, "Magic Item");
  strcpy(v.group, "Giants / Magic Items");
  snprintf(path, sizeof(path), "%s_item.ppm", prefix);
  Render(assets, path, 1280, 720, v);

  // A Trap Team villain: element, no picture, on the portal.
  memset(&v, 0, sizeof(v));
  v.pos = 500;
  v.count = 960;
  v.id = 7;
  v.name = "Chef Pepper Jack (Doom Raider)";
  v.element = elements::kFire;
  v.game = 4;
  strcpy(v.series, "Villain");
  strcpy(v.group, "Trap Team / Traps / Trappable Villains / Fire");
  v.on_portal = true;
  v.portal_count = 1;
  strcpy(v.portal_names, "Chef Pepper Jack (Doom Raider)");
  snprintf(path, sizeof(path), "%s_villain.ppm", prefix);
  Render(assets, path, 1280, 720, v);

  // An Imaginators Sensei in a 1024x576 frame.
  memset(&v, 0, sizeof(v));
  v.pos = 700;
  v.count = 960;
  v.id = 8;
  v.name = "King Pen";
  v.element = elements::kWater;
  v.game = 6;
  strcpy(v.series, "Sensei");
  strcpy(v.group, "Imaginators / Senseis");
  strcpy(v.image, "6. Imaginators\\King Pen.qoi");
  snprintf(path, sizeof(path), "%s_576.ppm", prefix);
  Render(assets, path, 1024, 576, v);

  // A figure with a nickname (under its name), START in the hints.
  memset(&v, 0, sizeof(v));
  v.pos = 3;
  v.count = 516;
  v.id = 9;
  v.name = "Chop Chop";
  v.element = elements::kUndead;
  v.game = 1;
  strcpy(v.series, "Series 1");
  strcpy(v.group, "Favorites / Skylanders / Undead");
  strcpy(v.image, "Cards 1/Chop Chop.qoi");
  strcpy(v.nickname, "Sir Chops-a-Lot");
  v.start_hint = true;
  strcpy(v.next_list, "SPEEDRUN");
  strcpy(v.remote, "192.168.1.23");
  snprintf(path, sizeof(path), "%s_nickname.ppm", prefix);
  Render(assets, path, 1280, 720, v);

  // The nickname keyboard (START), part way through typing, CAPS off, on the "o".
  v.kb_open = true;
  strcpy(v.kb_text, "Sir Cho");
  v.kb_row = 1;
  v.kb_col = 4;
  snprintf(path, sizeof(path), "%s_keyboard.ppm", prefix);
  Render(assets, path, 1280, 720, v);
  // ... in a 576-line frame, CAPS on, DONE picked.
  v.kb_caps = true;
  v.kb_row = 4;
  v.kb_col = 8;
  strcpy(v.kb_text, "");
  snprintf(path, sizeof(path), "%s_keyboard576.ppm", prefix);
  Render(assets, path, 1024, 576, v);
  return 0;
}
