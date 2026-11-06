// SkyPortal360 - skydrawn menu renderer. See overlay_render.h.
#include "overlay_render.h"

#include <string.h>

#include "elements.h"
#include "keyboard.h"
#include "overlay_font.h"

namespace overlay {

namespace {

typedef unsigned int u32;

// ---- colours (0xRRGGBB) ----
const u32 kWhite = 0xFFFFFF;
const u32 kInk = 0x0A1020;
const u32 kCardFill = 0x0B1838;
const u32 kCardEdge = 0x9FD4FF;
const u32 kDim = 0xB9CCEB;
const u32 kHeader = 0xBFE3FF;
const u32 kOnPortal = 0x5CFF8E;
const u32 kOffPortal = 0xD2DCEE;
const u32 kBtnA = 0x5BD15B, kBtnB = 0xFF4D4D, kBtnX = 0x3FA0FF, kBtnY = 0xFFD23F;
const u32 kNick = 0xFFD86B;      // a figure's nickname
const u32 kKey = 0x16244A, kKeyPicked = 0x3D8BFF;

Strip* F = 0;      // the strip being drawn
int S = 256;       // layout scale, 256 = a 720-line frame
int Y0 = 0, Y1 = 0;

int Sc(int v) { return (v * S + 128) >> 8; }

u32 Mix(u32 a, u32 b, int t /* 0..255 weight of b */) {
  const int r = (((a >> 16) & 255) * (255 - t) + ((b >> 16) & 255) * t) / 255;
  const int g = (((a >> 8) & 255) * (255 - t) + ((b >> 8) & 255) * t) / 255;
  const int bl = ((a & 255) * (255 - t) + (b & 255) * t) / 255;
  return (u32)(r << 16 | g << 8 | bl);
}

// Source-over of `rgb` with coverage `a` (0..255) onto the strip (the frame is opaque).
inline void Blend(int x, int y, u32 rgb, int a) {
  if (a <= 0 || x < 0 || x >= F->w || y < Y0 || y >= Y1) return;
  u32& d = F->px[(y - Y0) * F->w + x];
  d = a >= 255 ? rgb : Mix(d, rgb, a);
}

// Rows [top, bottom) clipped to the strip.
bool ClipRows(int top, int bottom, int* ys, int* ye) {
  *ys = top > Y0 ? top : Y0;
  *ye = bottom < Y1 ? bottom : Y1;
  return *ys < *ye;
}

// ---- background ----

u32 Bilinear(const Image& img, int fx, int fy /* 16.16 source coordinates */) {
  int x = fx >> 16, y = fy >> 16;
  const int ax = (fx >> 8) & 255, ay = (fy >> 8) & 255;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  const int x1 = x + 1 < img.w ? x + 1 : img.w - 1, y1 = y + 1 < img.h ? y + 1 : img.h - 1;
  if (x >= img.w) x = img.w - 1;
  if (y >= img.h) y = img.h - 1;
  const u32 p00 = img.px[y * img.w + x], p10 = img.px[y * img.w + x1];
  const u32 p01 = img.px[y1 * img.w + x], p11 = img.px[y1 * img.w + x1];
  u32 out = 0;
  for (int sh = 0; sh <= 24; sh += 8) {
    const int c00 = (p00 >> sh) & 255, c10 = (p10 >> sh) & 255;
    const int c01 = (p01 >> sh) & 255, c11 = (p11 >> sh) & 255;
    const int top = c00 * (256 - ax) + c10 * ax, bot = c01 * (256 - ax) + c11 * ax;
    const int c = (top * (256 - ay) + bot * ay) >> 16;
    out |= (u32)(c > 255 ? 255 : c) << sh;
  }
  return out;
}

// The background's crop ("cover": fills the frame, the overflow cut evenly) and vertical step.
void CoverCrop(int bw, int bh, int w, int h, int* sw, int* sh, int* ox, int* oy) {
  *ox = *oy = 0;
  if (bw * h > bh * w) {  // picture wider than the frame
    *sh = bh;
    *sw = (int)(bh * w / h);
    *ox = (bw - *sw) / 2;
  } else {
    *sw = bw;
    *sh = (int)(bw * h / w);
    *oy = (bh - *sh) / 2;
  }
}

void Background(const Image& bg) {
  const int w = F->w, h = F->h;
  if (!bg.px) {  // no picture: a deep blue gradient
    for (int y = Y0; y < Y1; ++y) {
      const u32 c = Mix(0x0E2A6B, 0x04081A, y * 255 / (h > 1 ? h - 1 : 1));
      for (int x = 0; x < w; ++x) F->px[(y - Y0) * w + x] = c;
    }
    return;
  }
  // "Cover": scale so the picture fills the frame, cropping the overflow evenly.
  int sw, sh, ox, oy;
  CoverCrop(bg.w, bg.h, w, h, &sw, &sh, &ox, &oy);
  const int stepx = (int)((sw << 16) / w), stepy = (int)((sh << 16) / h);
  for (int y = Y0; y < Y1; ++y) {
    const int fy = (oy << 16) + y * stepy;
    // Darken the top and bottom bands a little so the text reads on the bright portal.
    const int band = y < h / 5 ? (h / 5 - y) * 110 / (h / 5) : (y > h * 3 / 4 ? (y - h * 3 / 4) * 150 / (h / 4) : 0);
    u32* row = &F->px[(y - Y0) * w];
    for (int x = 0; x < w; ++x) {
      u32 c = Bilinear(bg, (ox << 16) + x * stepx, fy) & 0xFFFFFF;
      if (band) c = Mix(c, 0x020612, band);
      row[x] = c;
    }
  }
}

// Darkens the whole frame (behind a panel).
void Dim(int alpha) {
  for (int y = Y0; y < Y1; ++y) {
    u32* row = &F->px[(y - Y0) * F->w];
    for (int x = 0; x < F->w; ++x) row[x] = Mix(row[x], 0x020612, alpha);
  }
}

// ---- shapes (4x4 supersampled edges, coordinates in 1/8 pixel) ----

bool InRoundRect(int sx, int sy, int x0, int y0, int x1, int y1, int r) {
  if (sx < x0 || sx >= x1 || sy < y0 || sy >= y1) return false;
  const int cx = sx < x0 + r ? x0 + r : (sx > x1 - r ? x1 - r : sx);
  const int cy = sy < y0 + r ? y0 + r : (sy > y1 - r ? y1 - r : sy);
  const int dx = sx - cx, dy = sy - cy;
  return dx * dx + dy * dy <= r * r;
}

int RoundRectCoverage(int px, int py, int x0, int y0, int x1, int y1, int r) {
  const int cx = px * 8 + 4, cy = py * 8 + 4;
  if (cx >= x0 + r && cx < x1 - r && cy >= y0 + 8 && cy < y1 - 8) return 255;
  if (cy >= y0 + r && cy < y1 - r && cx >= x0 + 8 && cx < x1 - 8) return 255;
  if (cx < x0 - 8 || cx >= x1 + 8 || cy < y0 - 8 || cy >= y1 + 8) return 0;
  int n = 0;
  for (int j = 0; j < 4; ++j) {
    for (int i = 0; i < 4; ++i) {
      if (InRoundRect(px * 8 + 1 + i * 2, py * 8 + 1 + j * 2, x0, y0, x1, y1, r)) ++n;
    }
  }
  return n * 255 / 16;
}

void FillRoundRect(int x, int y, int w, int h, int r, u32 c, int alpha) {
  int ys, ye;
  if (!ClipRows(y - 1, y + h + 1, &ys, &ye)) return;
  const int x0 = x * 8, y0 = y * 8, x1 = (x + w) * 8, y1 = (y + h) * 8, rr = r * 8;
  for (int py = ys; py < ye; ++py) {
    for (int px = x - 1; px <= x + w; ++px) {
      Blend(px, py, c, RoundRectCoverage(px, py, x0, y0, x1, y1, rr) * alpha / 255);
    }
  }
}

void StrokeRoundRect(int x, int y, int w, int h, int r, int t, u32 c, int alpha) {
  int ys, ye;
  if (!ClipRows(y - 1, y + h + 1, &ys, &ye)) return;
  const int x0 = x * 8, y0 = y * 8, x1 = (x + w) * 8, y1 = (y + h) * 8, rr = r * 8;
  const int i0 = x0 + t * 8, j0 = y0 + t * 8, i1 = x1 - t * 8, j1 = y1 - t * 8;
  const int ir = rr > t * 8 ? rr - t * 8 : 0;
  for (int py = ys; py < ye; ++py) {
    for (int px = x - 1; px <= x + w; ++px) {
      const int cx = px * 8 + 4, cy = py * 8 + 4;
      if (cx >= i0 + ir + 8 && cx < i1 - ir - 8 && cy >= j0 + 8 && cy < j1 - 8) continue;
      if (cy >= j0 + ir + 8 && cy < j1 - ir - 8 && cx >= i0 + 8 && cx < i1 - 8) continue;
      int n = 0;
      for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
          const int sx = px * 8 + 1 + i * 2, sy = py * 8 + 1 + j * 2;
          if (InRoundRect(sx, sy, x0, y0, x1, y1, rr) && !InRoundRect(sx, sy, i0, j0, i1, j1, ir)) ++n;
        }
      }
      Blend(px, py, c, n * 255 / 16 * alpha / 255);
    }
  }
}

// Disc (r_in == 0) or ring [r_in, r_out] around (cx, cy).
void Ring(int cx, int cy, int r_out, int r_in, u32 c, int alpha) {
  int ys, ye;
  if (!ClipRows(cy - r_out - 1, cy + r_out + 2, &ys, &ye)) return;
  const int ro = r_out * 8, ri = r_in * 8;
  const int ro2 = ro * ro, ri2 = ri * ri;
  const int fast_out = (ro + 12) * (ro + 12), fast_in_o = ro > 12 ? (ro - 12) * (ro - 12) : 0;
  const int fast_in_i = (ri + 12) * (ri + 12), fast_out_i = ri > 12 ? (ri - 12) * (ri - 12) : 0;
  for (int py = ys; py < ye; ++py) {
    for (int px = cx - r_out - 1; px <= cx + r_out + 1; ++px) {
      const int dx = px * 8 + 4 - cx * 8, dy = py * 8 + 4 - cy * 8;
      const int d2 = dx * dx + dy * dy;
      if (d2 > fast_out) continue;
      if (ri && d2 < fast_out_i) continue;
      if (d2 < fast_in_o && (!ri || d2 > fast_in_i)) {
        Blend(px, py, c, alpha);
        continue;
      }
      int n = 0;
      for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
          const int sx = dx - 4 + 1 + i * 2, sy = dy - 4 + 1 + j * 2;
          const int s2 = sx * sx + sy * sy;
          if (s2 <= ro2 && (!ri || s2 >= ri2)) ++n;
        }
      }
      Blend(px, py, c, n * 255 / 16 * alpha / 255);
    }
  }
}

void Triangle(int ax, int ay, int bx, int by, int cx, int cy, u32 c, int alpha) {
  ax *= 8; ay *= 8; bx *= 8; by *= 8; cx *= 8; cy *= 8;
  int minx = ax < bx ? ax : bx; minx = minx < cx ? minx : cx;
  int maxx = ax > bx ? ax : bx; maxx = maxx > cx ? maxx : cx;
  int miny = ay < by ? ay : by; miny = miny < cy ? miny : cy;
  int maxy = ay > by ? ay : by; maxy = maxy > cy ? maxy : cy;
  int ys, ye;
  if (!ClipRows(miny / 8 - 1, maxy / 8 + 2, &ys, &ye)) return;
  const int area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
  const int sign = area < 0 ? -1 : 1;
  for (int py = ys; py < ye; ++py) {
    for (int px = minx / 8 - 1; px <= maxx / 8 + 1; ++px) {
      int n = 0;
      for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
          const int sx = px * 8 + 1 + i * 2, sy = py * 8 + 1 + j * 2;
          const int e0 = ((bx - ax) * (sy - ay) - (by - ay) * (sx - ax)) * sign;
          const int e1 = ((cx - bx) * (sy - by) - (cy - by) * (sx - bx)) * sign;
          const int e2 = ((ax - cx) * (sy - cy) - (ay - cy) * (sx - cx)) * sign;
          if (e0 >= 0 && e1 >= 0 && e2 >= 0) ++n;
        }
      }
      Blend(px, py, c, n * 255 / 16 * alpha / 255);
    }
  }
}

// ---- pictures ----

// Draws `img` scaled into the box (dx, dy, dw, dh), optionally clipped to a circle (r > 0).
void DrawImage(const Image& img, int dx, int dy, int dw, int dh, int ccx, int ccy, int cr) {
  if (!img.px || dw <= 0 || dh <= 0) return;
  int ys, ye;
  if (!ClipRows(dy, dy + dh, &ys, &ye)) return;
  const int stepx = (int)((img.w << 16) / dw), stepy = (int)((img.h << 16) / dh);
  const int r2o = (cr * 2 + 1) * (cr * 2 + 1), r2i = (cr * 2 - 1) * (cr * 2 - 1);
  for (int py = ys; py < ye; ++py) {
    const int fy = (py - dy) * stepy + stepy / 2 - 32768;
    for (int px = dx; px < dx + dw; ++px) {
      int cover = 255;
      if (cr > 0) {  // circle edge, measured in half pixels
        const int ex = (px - ccx) * 2 + 1, ey = (py - ccy) * 2 + 1;
        const int d2 = ex * ex + ey * ey;
        if (d2 >= r2o) continue;
        if (d2 > r2i) cover = 255 * (r2o - d2) / (r2o - r2i);
      }
      const u32 c = Bilinear(img, (px - dx) * stepx + stepx / 2 - 32768 < 0 ? 0 : (px - dx) * stepx + stepx / 2 - 32768,
                             fy < 0 ? 0 : fy);
      Blend(px, py, c & 0xFFFFFF, (int)(c >> 24) * cover / 255);
    }
  }
}

// Fits `img` inside (x, y, w, h), centred, keeping its proportions.
void DrawImageFit(const Image& img, int x, int y, int w, int h) {
  if (!img.px) return;
  int dw = w, dh = (int)(img.h * w / img.w);
  if (dh > h) {
    dh = h;
    dw = (int)(img.w * h / img.h);
  }
  DrawImage(img, x + (w - dw) / 2, y + (h - dh) / 2, dw, dh, 0, 0, 0);
}

// Draws `img` stretched over exactly (x, y, w, h) with rounded corners of radius r.
void DrawImageRounded(const Image& img, int x, int y, int w, int h, int r) {
  if (!img.px || w <= 0 || h <= 0) return;
  int ys, ye;
  if (!ClipRows(y, y + h, &ys, &ye)) return;
  const int stepx = (int)((img.w << 16) / w), stepy = (int)((img.h << 16) / h);
  const int x0 = x * 8, y0 = y * 8, x1 = (x + w) * 8, y1 = (y + h) * 8, rr = r * 8;
  for (int py = ys; py < ye; ++py) {
    const int fy = (py - y) * stepy + stepy / 2 - 32768;
    for (int px = x; px < x + w; ++px) {
      const int cover = RoundRectCoverage(px, py, x0, y0, x1, y1, rr);
      if (!cover) continue;
      const int fx = (px - x) * stepx + stepx / 2 - 32768;
      const u32 c = Bilinear(img, fx < 0 ? 0 : fx, fy < 0 ? 0 : fy);
      Blend(px, py, c & 0xFFFFFF, (int)(c >> 24) * cover / 255);
    }
  }
}

// Soft glow around a rounded rectangle: opacity fades from `alpha` at its edge to 0 `spread`
// pixels out.
void GlowRect(int x, int y, int w, int h, int r, int spread, u32 c, int alpha) {
  int ys, ye;
  if (!ClipRows(y - spread, y + h + spread, &ys, &ye)) return;
  const int ro = r + spread, ro2 = ro * ro, ri2 = r * r;
  for (int py = ys; py < ye; ++py) {
    const int qy = py < y + r ? y + r - py : (py >= y + h - r ? py - (y + h - r - 1) : 0);
    for (int px = x - spread; px < x + w + spread; ++px) {
      const int qx = px < x + r ? x + r - px : (px >= x + w - r ? px - (x + w - r - 1) : 0);
      const int d2 = qx * qx + qy * qy;
      if (d2 >= ro2 || d2 < ri2) continue;
      Blend(px, py, c, alpha * (ro2 - d2) / (ro2 - ri2));
    }
  }
}

// ---- text ----

const OverlayGlyph* Glyph(const OverlayFont& f, char ch) {
  int code = (unsigned char)ch;
  if (code < f.first || code > f.last) code = '?';
  return &f.glyphs[code - f.first];
}

int TextWidth(const OverlayFont& f, const char* s, int spacing) {
  int w = 0;
  for (; *s; ++s) w += Glyph(f, *s)->advance + spacing;
  return w > 0 ? w - spacing : 0;
}

void Text(const OverlayFont& f, int x, int y, const char* s, u32 c, int spacing, int alpha) {
  int ys, ye;
  if (!ClipRows(y - 4, y + f.line_height + 4, &ys, &ye)) return;
  for (; *s; ++s) {
    const OverlayGlyph* g = Glyph(f, *s);
    const unsigned char* src = f.pixels + g->offset;
    for (int j = 0; j < g->h; ++j) {
      const int py = y + g->y + j;
      if (py < ys || py >= ye) continue;
      for (int i = 0; i < g->w; ++i) Blend(x + g->x + i, py, c, src[j * g->w + i] * alpha / 255);
    }
    x += g->advance + spacing;
  }
}

// Text with a soft dark shadow, so it reads on the bright portal picture.
void ShadowText(const OverlayFont& f, int x, int y, const char* s, u32 c, int spacing) {
  const int o = Sc(2) > 1 ? Sc(2) : 1;
  Text(f, x + o, y + o, s, 0x000000, spacing, 150);
  Text(f, x, y, s, c, spacing, 255);
}

void CenteredShadowText(const OverlayFont& f, int cx, int y, const char* s, u32 c, int spacing) {
  ShadowText(f, cx - TextWidth(f, s, spacing) / 2, y, s, c, spacing);
}

void Upper(char* out, int cap, const char* in) {
  int i = 0;
  for (; in[i] && i < cap - 1; ++i) {
    const char ch = in[i];
    out[i] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 'a' + 'A') : ch;
  }
  out[i] = 0;
}

void Fit(const OverlayFont& f, char* s, int max_w, int spacing) {
  int len = (int)strlen(s);
  if (TextWidth(f, s, spacing) <= max_w) return;
  while (len > 0) {
    s[--len] = 0;
    char tmp[200];
    strcpy(tmp, s);
    strcat(tmp, "...");
    if (TextWidth(f, tmp, spacing) <= max_w) {
      strcpy(s, tmp);
      return;
    }
  }
}

// ---- pieces ----

void Arrow(int cx, int cy, bool right) {
  const int w = Sc(74), h = Sc(110), r = Sc(16);
  FillRoundRect(cx - w / 2, cy - h / 2, w, h, r, 0x061026, 150);
  StrokeRoundRect(cx - w / 2, cy - h / 2, w, h, r, Sc(2) > 0 ? Sc(2) : 1, kWhite, 110);
  const int a = Sc(16);
  if (right) {
    Triangle(cx - a + Sc(4), cy - a - Sc(6), cx - a + Sc(4), cy + a + Sc(6), cx + a + Sc(2), cy, kWhite, 255);
  } else {
    Triangle(cx + a - Sc(4), cy - a - Sc(6), cx - a - Sc(2), cy, cx + a - Sc(4), cy + a + Sc(6), kWhite, 255);
  }
}

void Card(int x, int y, int w, int h) {
  FillRoundRect(x, y, w, h, Sc(18), kCardFill, 175);
  StrokeRoundRect(x, y, w, h, Sc(18), Sc(2) > 0 ? Sc(2) : 1, kCardEdge, 90);
}

void ButtonHint(const OverlayFont& f, int* x, int y, const char* key, u32 color, const char* label) {
  const int r = Sc(13);
  const int cy = y + f.ascent / 2 + Sc(1);
  int kw = TextWidth(f, key, 0);
  if (color) {
    Ring(*x + r, cy, r, 0, color, 255);
    Text(f, *x + r - kw / 2, y, key, kInk, 0, 255);
    *x += 2 * r + Sc(7);
  } else {  // text key ("LB+RB")
    FillRoundRect(*x, cy - r, kw + Sc(14), 2 * r, Sc(8), 0xFFFFFF, 220);
    Text(f, *x + Sc(7), y, key, kInk, 0, 255);
    *x += kw + Sc(14) + Sc(7);
  }
  ShadowText(f, *x, y, label, kWhite, 0);
  *x += TextWidth(f, label, 0) + Sc(24);
}

int HintWidth(const OverlayFont& f, const char* key, bool round, const char* label) {
  const int kw = round ? 2 * Sc(13) : TextWidth(f, key, 0) + Sc(14);
  return kw + Sc(7) + TextWidth(f, label, 0) + Sc(24);
}

void Number(char* out, int v) {
  char tmp[12];
  int n = 0;
  if (v < 0) v = 0;
  do {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v && n < 11);
  int i = 0;
  while (n) out[i++] = tmp[--n];
  out[i] = 0;
}

// The nickname keyboard (START on a figure): a panel over the dimmed menu.
void KeyboardPanel(const menu::View& v, const OverlayFont& large, const OverlayFont& medium,
                   const OverlayFont& small) {
  const int W = F->w, H = F->h, cx = W / 2;
  Dim(165);
  const int pw = Sc(900), ph = Sc(566);
  const int px = cx - pw / 2, py = (H - ph) / 2;
  FillRoundRect(px, py, pw, ph, Sc(24), 0x0B1838, 245);
  StrokeRoundRect(px, py, pw, ph, Sc(24), Sc(2) > 0 ? Sc(2) : 1, kCardEdge, 150);
  // Whose nickname.
  Text(small, px + Sc(32), py + Sc(24), "NICKNAME FOR", kDim, Sc(2), 255);
  char name[96];
  Upper(name, sizeof(name), v.name ? v.name : "");
  Fit(medium, name, pw - Sc(64) - TextWidth(small, "NICKNAME FOR", Sc(2)) - Sc(16), 0);
  ShadowText(medium, px + Sc(32) + TextWidth(small, "NICKNAME FOR", Sc(2)) + Sc(16),
             py + Sc(24) + small.ascent - medium.ascent, name, kWhite, 0);
  // What's typed, with a cursor, and how many characters are left.
  const int fx = px + Sc(32), fy = py + Sc(72), fw = pw - Sc(64), fh = Sc(76);
  FillRoundRect(fx, fy, fw, fh, Sc(14), 0x050B1E, 255);
  StrokeRoundRect(fx, fy, fw, fh, Sc(14), Sc(2) > 0 ? Sc(2) : 1, kNick, 140);
  const int ty = fy + (fh - large.line_height) / 2;
  int tw = 0;
  if (v.kb_text[0]) {
    ShadowText(large, fx + Sc(22), ty, v.kb_text, kNick, 0);
    tw = TextWidth(large, v.kb_text, 0);
  } else {
    Text(medium, fx + Sc(22) + Sc(12), fy + (fh - medium.line_height) / 2, "Type a nickname", kDim, 0, 160);
  }
  const int bar = Sc(3) > 1 ? Sc(3) : 2;
  FillRoundRect(fx + Sc(22) + tw + Sc(4), ty + Sc(6), bar, large.ascent + Sc(4), 1, kWhite, 230);
  char count[12];
  Number(count, (int)strlen(v.kb_text));
  strcat(count, "/20");
  Text(small, fx + fw - Sc(20) - TextWidth(small, count, 0), fy + fh - small.line_height - Sc(8), count, kDim, 0, 255);
  // The keys.
  const int kw = Sc(76), kh = Sc(52), gap = Sc(8);
  const int gw = kbd::kCols * kw + (kbd::kCols - 1) * gap;
  const int gx = cx - gw / 2, gy = fy + fh + Sc(26);
  int picked_first, picked_last;
  kbd::KeySpan(v.kb_row, v.kb_col, &picked_first, &picked_last);
  for (int r = 0; r < kbd::kRows; ++r) {
    for (int c = 0; c < kbd::kCols;) {
      int first, last;
      kbd::KeySpan(r, c, &first, &last);
      const int x = gx + first * (kw + gap), y = gy + r * (kh + gap);
      const int w = (last - first + 1) * kw + (last - first) * gap;
      const bool picked = r == v.kb_row && first == picked_first;
      FillRoundRect(x, y, w, kh, Sc(10), picked ? kKeyPicked : kKey, 255);
      if (picked) StrokeRoundRect(x, y, w, kh, Sc(10), Sc(3) > 1 ? Sc(3) : 2, kWhite, 255);
      const kbd::Key key = kbd::KeyAt(r, c);
      if (key == kbd::kChar) {
        char label[2] = {kbd::CharAt(r, c, v.kb_caps), 0};
        Text(medium, x + (w - TextWidth(medium, label, 0)) / 2, y + (kh - medium.line_height) / 2 + Sc(2),
             label, kWhite, 0, 255);
      } else {
        const char* label = kbd::SpecialLabel(key);
        const u32 lc = key == kbd::kCaps && v.kb_caps ? kNick : (key == kbd::kDone ? kOnPortal : kWhite);
        Text(small, x + (w - TextWidth(small, label, Sc(2))) / 2, y + (kh - small.line_height) / 2 + Sc(2),
             label, lc, Sc(2), 255);
      }
      c = last + 1;
    }
  }
  // What the buttons do.
  const int hy = py + ph - Sc(46);
  int hw = HintWidth(small, "A", true, "TYPE") + HintWidth(small, "B", true, "DELETE") +
           HintWidth(small, "X", true, "SPACE") + HintWidth(small, "Y", true, "CAPS") +
           HintWidth(small, "START", false, "SAVE") + HintWidth(small, "BACK", false, "CANCEL") - Sc(24);
  int hx = cx - hw / 2;
  ButtonHint(small, &hx, hy, "A", kBtnA, "TYPE");
  ButtonHint(small, &hx, hy, "B", kBtnB, "DELETE");
  ButtonHint(small, &hx, hy, "X", kBtnX, "SPACE");
  ButtonHint(small, &hx, hy, "Y", kBtnY, "CAPS");
  ButtonHint(small, &hx, hy, "START", 0, "SAVE");
  ButtonHint(small, &hx, hy, "BACK", 0, "CANCEL");
}

}  // namespace

void RenderStrip(const Scene& s, Strip* strip) {
  F = strip;
  Y0 = strip->y0;
  Y1 = strip->y0 + strip->rows;
  S = strip->h * 256 / 720;
  if (S < 150) S = 150;
  const bool big = strip->h >= 600;
  const OverlayFont& huge = big ? kFontHuge : kFontLarge;
  const OverlayFont& large = big ? kFontLarge : kFontMedium;
  const OverlayFont& medium = big ? kFontMedium : kFontSmall;
  const OverlayFont& small = kFontSmall;
  const menu::View& v = *s.view;
  const int W = strip->w, H = strip->h, cx = W / 2;

  Background(s.background);

  // Header: brand, folder, position.
  static const char kBrand[] = "THE STATIC RIFT";
  ShadowText(small, Sc(40), Sc(22), kBrand, kHeader, Sc(4));
  if (v.remote[0]) {  // where the phone remote is (under the brand)
    char phone[48] = "PHONE  ";
    strncat(phone, v.remote, sizeof(phone) - strlen(phone) - 1);
    ShadowText(small, Sc(40), Sc(22) + small.line_height, phone, kDim, Sc(1));
  }
  char count[32] = "";
  if (v.count > 0) {
    char a[12], b[12];
    Number(a, v.pos + 1);
    Number(b, v.count);
    strcpy(count, a);
    strcat(count, " / ");
    strcat(count, b);
  }
  ShadowText(small, W - Sc(40) - TextWidth(small, count, 0), Sc(22), count, kHeader, 0);
  if (v.group[0]) {
    char group[96];
    Upper(group, sizeof(group), v.group);
    // Centred between the brand and the count, clear of both.
    const int left = Sc(40) + TextWidth(small, kBrand, Sc(4)) + Sc(28);
    const int right = W - Sc(40) - TextWidth(small, count, 0) - Sc(28);
    int room = 2 * (cx - left < right - cx ? cx - left : right - cx);
    if (room < Sc(120)) room = Sc(120);
    Fit(small, group, room, Sc(1));
    CenteredShadowText(small, cx, Sc(22), group, kDim, Sc(1));
  }

  if (v.id < 0) {
    CenteredShadowText(large, cx, H / 2 - large.line_height, "NO FIGURES FOUND", kWhite, 0);
    CenteredShadowText(small, cx, H / 2 + Sc(10), "Copy figure dumps to The Static Rift\\Figures", kDim, 0);
    return;
  }

  char name[96];
  Upper(name, sizeof(name), v.name);

  // Figure picture: the card art, framed in its element's colour, the frame sized to the picture
  // (portrait cards and landscape Swap Force halves alike).
  const elements::Element el = (elements::Element)v.element;
  const u32 ec = elements::Color(el);
  const int top = Sc(140), box_h = Sc(332), box_w = Sc(520);
  const int ccy = top + box_h / 2;
  int fw = box_h * 204 / 320, fh = box_h;  // no picture: a card-shaped panel
  if (s.figure.px) {
    fw = s.figure.w * box_h / s.figure.h;
    if (fw > box_w) {
      fw = box_w;
      fh = s.figure.h * box_w / s.figure.w;
    }
  }
  const int fx = cx - fw / 2, fy = ccy - fh / 2;
  const int rad = Sc(14), bt = Sc(6) > 2 ? Sc(6) : 2;
  GlowRect(fx - bt, fy - bt, fw + 2 * bt, fh + 2 * bt, rad + bt, Sc(42), ec, 150);
  FillRoundRect(fx - bt, fy - bt, fw + 2 * bt, fh + 2 * bt, rad + bt, ec, 255);
  if (s.figure.px) {
    FillRoundRect(fx, fy, fw, fh, rad, 0x0A1430, 255);  // behind any transparent edges
    DrawImageRounded(s.figure, fx, fy, fw, fh, rad);
  } else {
    FillRoundRect(fx, fy, fw, fh, rad, 0xF6F8FC, 255);
    if (s.element_icon.px) {
      const int d = fw * 3 / 4;
      DrawImage(s.element_icon, cx - d / 2, ccy - d / 2, d, d, 0, 0, 0);
    } else if (s.game_logo.px) {  // no picture or element (a magic item, a chest): the game's logo
      DrawImageFit(s.game_logo, fx + Sc(16), ccy - fh / 4, fw - Sc(32), fh / 2);
    } else {
      char initial[2] = {name[0], 0};
      Text(huge, cx - TextWidth(huge, initial, 0) / 2, ccy - huge.ascent / 2 - Sc(4), initial, 0x7A8BA8, 0, 255);
    }
  }
  StrokeRoundRect(fx, fy, fw, fh, rad, Sc(2) > 0 ? Sc(2) : 1, Mix(ec, 0xFFFFFF, 90), 170);
  Arrow(cx - Sc(330), ccy, false);
  Arrow(cx + Sc(330), ccy, true);

  // Name (over the picture's glow), and the nickname you gave it under it.
  if (v.nickname[0]) {
    const OverlayFont* nf = &large;
    if (TextWidth(*nf, name, 0) > W - Sc(120)) nf = &medium;
    Fit(*nf, name, W - Sc(120), 0);
    CenteredShadowText(*nf, cx, Sc(48) + (large.line_height - nf->line_height) / 2, name, kWhite, 0);
    char nick[40] = "\"";
    strncat(nick, v.nickname, sizeof(nick) - 3);
    strcat(nick, "\"");
    Fit(medium, nick, W - Sc(200), 0);
    CenteredShadowText(medium, cx, Sc(48) + large.line_height + Sc(2), nick, kNick, 0);
  } else {
    const OverlayFont* nf = &huge;
    if (TextWidth(*nf, name, 0) > W - Sc(120)) nf = &large;
    Fit(*nf, name, W - Sc(120), 0);
    CenteredShadowText(*nf, cx, Sc(58) + (huge.line_height - nf->line_height) / 2, name, kWhite, 0);
  }

  // Element / game / series cards.
  const int cy = Sc(492), ch = Sc(96), cw = Sc(300), gap = Sc(20);
  const int x1 = cx - cw - cw / 2 - gap, x2 = cx - cw / 2, x3 = cx + cw / 2 + gap;
  Card(x1, cy, cw, ch);
  Card(x2, cy, cw, ch);
  Card(x3, cy, cw, ch);
  {  // element
    const int icon = Sc(72);
    if (s.element_icon.px) DrawImage(s.element_icon, x1 + Sc(14), cy + (ch - icon) / 2, icon, icon, 0, 0, 0);
    else Ring(x1 + Sc(14) + icon / 2, cy + ch / 2, icon / 2, 0, ec, 255);
    const int tx = x1 + Sc(14) + icon + Sc(14);
    Text(small, tx, cy + Sc(18), "ELEMENT", kDim, Sc(2), 255);
    char en[24];
    Upper(en, sizeof(en), el != elements::kElementUnknown ? elements::Name(el) : "None");
    const int room = x1 + cw - Sc(14) - tx;
    const OverlayFont* ef = TextWidth(large, en, 0) <= room ? &large : &medium;
    Fit(*ef, en, room, 0);
    ShadowText(*ef, tx, cy + Sc(40) + (large.line_height - ef->line_height) / 2, en,
               el != elements::kElementUnknown ? Mix(ec, kWhite, 110) : kDim, 0);
  }
  {  // game
    if (s.game_logo.px) {
      DrawImageFit(s.game_logo, x2 + Sc(12), cy + Sc(8), cw - Sc(24), ch - Sc(16));
    } else {
      Text(small, x2 + Sc(20), cy + Sc(18), "GAME", kDim, Sc(2), 255);
      char gn[48];
      Upper(gn, sizeof(gn), v.game ? menu::GameName(v.game) : "Unknown");
      Fit(medium, gn, cw - Sc(40), 0);
      ShadowText(medium, x2 + Sc(20), cy + Sc(46), gn, kWhite, 0);
    }
  }
  {  // series
    Text(small, x3 + Sc(20), cy + Sc(18), "SERIES", kDim, Sc(2), 255);
    char se[48];
    Upper(se, sizeof(se), v.series[0] ? v.series : "-");
    const OverlayFont* sf = &large;
    if (TextWidth(*sf, se, 0) > cw - Sc(40)) sf = &medium;
    Fit(*sf, se, cw - Sc(40), 0);
    ShadowText(*sf, x3 + Sc(20), cy + Sc(40) + (large.line_height - sf->line_height) / 2, se, kWhite, 0);
  }

  // Portal state.
  const int sy = Sc(608);
  const char* status = v.on_portal ? "ON THE RIFT" : "NOT ON THE RIFT";
  const u32 sc = v.on_portal ? kOnPortal : kOffPortal;
  char line[220];
  strcpy(line, status);
  strcat(line, "   |   ");
  if (v.portal_count) {
    strcat(line, "On the rift: ");
    strncat(line, v.portal_names, sizeof(line) - strlen(line) - 1);
  } else {
    strcat(line, "The rift is empty");
  }
  Fit(medium, line, W - Sc(200), 0);
  const int lw = TextWidth(medium, line, 0);
  const int dot = Sc(7);
  Ring(cx - lw / 2 - dot * 2 - Sc(6), sy + medium.ascent / 2 + Sc(1), dot, 0, sc, 255);
  ShadowText(medium, cx - lw / 2, sy, line, sc, 0);

  // Button hints. START (a nickname) and R3 (the next list: your lists, My Picks, one game's
  // figures, everything) when there's room; D-PAD BROWSE makes way for them if it has to.
  const int hy = H - Sc(52);
  const char* r3 = v.next_list;
  const int room = W - Sc(40);
  const int base = HintWidth(small, "A", true, "ADD") + HintWidth(small, "Y", true, "SWAP IN") +
                   HintWidth(small, "B", true, "REMOVE") + HintWidth(small, "X", true, "CLEAR") +
                   HintWidth(small, "LB+RB", false, "CLOSE") - Sc(24);
  const int dpad_w = HintWidth(small, "D-PAD", false, "BROWSE");
  const int start_w = v.start_hint ? HintWidth(small, "START", false, "NICKNAME") : 0;
  const int r3_w = r3[0] ? HintWidth(small, "R3", false, r3) : 0;
  bool show_dpad = true, show_start = v.start_hint, show_r3 = r3[0] != 0;
  int need = base + dpad_w + start_w + r3_w;
  if (need > room) {  // D-PAD makes way first, then START, then R3
    show_dpad = false;
    need -= dpad_w;
  }
  if (need > room && show_start) {
    show_start = false;
    need -= start_w;
  }
  if (need > room && show_r3) {
    show_r3 = false;
    need -= r3_w;
  }
  if (!show_dpad && need + dpad_w <= room) show_dpad = true;
  const int hw = base + (show_dpad ? dpad_w : 0) + (show_start ? start_w : 0) + (show_r3 ? r3_w : 0);
  int hx = cx - hw / 2;
  ButtonHint(small, &hx, hy, "A", kBtnA, "ADD");
  ButtonHint(small, &hx, hy, "Y", kBtnY, "SWAP IN");
  ButtonHint(small, &hx, hy, "B", kBtnB, "REMOVE");
  ButtonHint(small, &hx, hy, "X", kBtnX, "CLEAR");
  if (show_dpad) ButtonHint(small, &hx, hy, "D-PAD", 0, "BROWSE");
  if (show_start) ButtonHint(small, &hx, hy, "START", 0, "NICKNAME");
  if (show_r3) ButtonHint(small, &hx, hy, "R3", 0, r3);
  ButtonHint(small, &hx, hy, "LB+RB", 0, "CLOSE");

  if (v.kb_open) KeyboardPanel(v, large, medium, small);
}

void BackgroundRows(int bw, int bh, int w, int h, int y0, int y1, int* first, int* last) {
  int sw, sh, ox, oy;
  CoverCrop(bw, bh, w, h, &sw, &sh, &ox, &oy);
  const int stepy = (int)((sh << 16) / h);
  int a = ((oy << 16) + y0 * stepy) >> 16;
  int b = (((oy << 16) + (y1 - 1) * stepy) >> 16) + 1;
  if (a < 0) a = 0;
  if (b > bh - 1) b = bh - 1;
  if (a > b) a = b;
  *first = a;
  *last = b;
}

}  // namespace overlay
