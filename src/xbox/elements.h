// SkyPortal360 - a figure's element, for the menus.
//
// From the character ID in the dump (block 1, bytes 0x10-0x11, little-endian) for the Spyro's
// Adventure and Giants characters, or from a sub-folder named after an element ("Figures\Air\..."),
// which works for every game. Anything else is kElementUnknown.
#pragma once

namespace elements {

enum Element {
  kElementUnknown = 0,
  kAir, kEarth, kFire, kWater, kMagic, kTech, kLife, kUndead, kLight, kDark,
  kElementCount
};

// Character ID stored in a 1 KB figure dump.
int ToyId(const unsigned char* dump);
// Element from a character ID, or kElementUnknown.
Element FromToyId(int toy_id);
// Element from the first folder of a path relative to Figures ("Air\\Whirlwind.bin"), or unknown.
Element FromPath(const char* rel);

const char* Name(Element e);       // "Air", ... ("" when unknown)
unsigned int Color(Element e);     // 0xRRGGBB accent colour for the drawn menu

}  // namespace elements
