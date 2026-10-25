// SkyPortal360 - figure elements. See elements.h.
#include "elements.h"

#include <string.h>

namespace elements {

namespace {

const char* const kNames[kElementCount] = {
    "", "Air", "Earth", "Fire", "Water", "Magic", "Tech", "Life", "Undead", "Light", "Dark"};

const unsigned int kColors[kElementCount] = {
    0x7A8BA8,  // unknown: slate
    0x8FD3FF,  // air
    0xC08A4A,  // earth
    0xFF6A2B,  // fire
    0x2F8DFF,  // water
    0xB25BFF,  // magic
    0xFFB21F,  // tech
    0x4FD65A,  // life
    0x9FA8C0,  // undead
    0xFFF3A0,  // light
    0x7F5BC9,  // dark
};

bool SameIgnoringCase(const char* a, const char* b, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
    if (x != y) return false;
    if (!x) return true;
  }
  return true;
}

}  // namespace

int ToyId(const unsigned char* dump) { return dump[0x10] | (dump[0x11] << 8); }

Element FromToyId(int id) {
  // Spyro's Adventure: 0-32, in groups of four per element, except Trigger Happy (19, Tech) and
  // Wrecking Ball (23, Magic), Dark Spyro (28, Magic) and Cynder (32, Undead).
  if (id == 19) return kTech;
  if (id == 23 || id == 28) return kMagic;
  if (id == 32) return kUndead;
  if (id >= 0 && id <= 31) {
    static const Element kGroups[8] = {kAir, kEarth, kFire, kWater, kMagic, kTech, kLife, kUndead};
    return kGroups[id / 4];
  }
  // Giants: 100-115, two per element in the same order.
  if (id >= 100 && id <= 115) {
    static const Element kPairs[8] = {kAir, kEarth, kFire, kWater, kMagic, kTech, kLife, kUndead};
    return kPairs[(id - 100) / 2];
  }
  return kElementUnknown;
}

Element FromPath(const char* rel) {
  // Any folder named after an element ("...\\1) Elements\\Air\\Air Angel 0.dump"), else a file name
  // starting with one ("Air Angel 0", "Undead Skull (Spectral Skull)").
  if (!rel) return kElementUnknown;
  const char* p = rel;
  while (*p) {
    const char* end = p;
    while (*end && *end != '\\') ++end;
    const bool last = *end == 0;
    size_t len = (size_t)(end - p);
    if (last) {  // file name: only its first word
      size_t w = 0;
      while (w < len && p[w] != ' ') ++w;
      len = w;
    }
    for (int e = 1; e < kElementCount; ++e) {
      if (strlen(kNames[e]) == len && SameIgnoringCase(p, kNames[e], len)) return (Element)e;
    }
    if (last) break;
    p = end + 1;
  }
  return kElementUnknown;
}

const char* Name(Element e) { return (e > 0 && e < kElementCount) ? kNames[e] : ""; }

unsigned int Color(Element e) { return kColors[(e > 0 && e < kElementCount) ? e : 0]; }

}  // namespace elements
