// Renders menu screenshots with the console's own drawing code (overlay_render.cpp), one per line
// of a spec file:  out.ppm|w|h|name|element|game|series|group|image|pos|count|on_portal|portal names
//   promo_shots <The Static Rift dir> <spec file>
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
  for (char* p = path; *p; ++p)
    if (*p == '\\') *p = '/';
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

static char* Field(char** p) {
  char* s = *p;
  char* bar = strchr(s, '|');
  if (bar) {
    *bar = 0;
    *p = bar + 1;
  } else {
    char* nl = strchr(s, '\n');
    if (nl) *nl = 0;
    *p = s + strlen(s);
  }
  return s;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: promo_shots <The Static Rift dir> <spec file>\n");
    return 1;
  }
  FILE* spec = fopen(argv[2], "r");
  if (!spec) return 1;
  static char line[2048];
  static char names[64][128];
  int k = 0;
  while (fgets(line, sizeof(line), spec) && k < 64) {
    char* p = line;
    menu::View v;
    memset(&v, 0, sizeof(v));
    const char* out = Field(&p);
    const int w = atoi(Field(&p));
    const int h = atoi(Field(&p));
    strncpy(names[k], Field(&p), sizeof(names[k]) - 1);
    v.name = names[k++];
    v.element = atoi(Field(&p));
    v.game = atoi(Field(&p));
    strncpy(v.series, Field(&p), sizeof(v.series) - 1);
    strncpy(v.group, Field(&p), sizeof(v.group) - 1);
    strncpy(v.image, Field(&p), sizeof(v.image) - 1);
    v.pos = atoi(Field(&p));
    v.count = atoi(Field(&p));
    v.id = v.pos;
    v.on_portal = atoi(Field(&p)) != 0;
    strncpy(v.portal_names, Field(&p), sizeof(v.portal_names) - 1);
    v.portal_count = 0;
    if (v.portal_names[0]) {
      v.portal_count = 1;
      for (const char* c = v.portal_names; *c; ++c)
        if (*c == ',') ++v.portal_count;
    }
    Render(argv[1], out, w, h, v);
  }
  fclose(spec);
  return 0;
}
