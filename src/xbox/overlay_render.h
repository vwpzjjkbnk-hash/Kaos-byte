// SkyPortal360 - skydrawn: renders the full-screen figure menu (portal background, figure picture
// in a ring, element / game / series cards, arrows, portal state, button hints).
//
// Portable (no console APIs, integer maths only: the open build has no floating point), so it is
// also built on the PC to render previews. The frame is drawn in horizontal strips: the console
// side renders 32 lines at a time (one row of the front buffer's 32x32 tiles, a contiguous block
// of memory) into a small buffer and writes it out, so no full-size work frame is needed.
#pragma once

#include "menu_api.h"

namespace overlay {

struct Image {
  int w, h;
  const unsigned int* px;  // 0xAARRGGBB, NULL when the picture is missing
};

struct Scene {
  const menu::View* view;
  Image background;    // the portal photo, stretched to cover the frame
  Image figure;        // the figure's picture
  Image element_icon;  // round element icon
  Image game_logo;     // the figure's game logo
};

// A horizontal strip [y0, y0 + rows) of a w x h frame; px holds w * rows pixels (0x00RRGGBB).
struct Strip {
  int w, h;
  int y0, rows;
  unsigned int* px;
};

void RenderStrip(const Scene& scene, Strip* strip);

// The rows [*first, *last] of a bw x bh background that drawing rows [y0, y1) of a w x h frame
// reads (so the console can decode just those rows of the background as it goes down the frame).
void BackgroundRows(int bw, int bh, int w, int h, int y0, int y1, int* first, int* last);

}  // namespace overlay
