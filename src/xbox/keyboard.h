// Project Kaos-Byte - the nickname keyboard of the full-screen menu (START on a figure): its
// layout, shared by the controller handling (main.cpp) and the drawing (overlay_render.cpp).
// Portable: no console APIs.
//
//   a b c d e f g h i j
//   k l m n o p q r s t
//   u v w x y z - ' . !
//   1 2 3 4 5 6 7 8 9 0
//   [ SPACE ][DEL][CAPS][ DONE ]
#pragma once

namespace kbd {

enum { kRows = 5, kCols = 10, kSpecialRow = 4 };
enum Key { kChar, kSpace, kDelete, kCaps, kDone };

// What the key at (row, col) is. The bottom row has four wide keys: SPACE (columns 0-3),
// DEL (4-5), CAPS (6-7) and DONE (8-9).
inline Key KeyAt(int row, int col) {
  if (row != kSpecialRow) return kChar;
  return col < 4 ? kSpace : (col < 6 ? kDelete : (col < 8 ? kCaps : kDone));
}

// The columns a key covers.
inline void KeySpan(int row, int col, int* first, int* last) {
  if (row != kSpecialRow) {
    *first = *last = col;
    return;
  }
  static const int kFirst[] = {0, 0, 0, 0, 4, 4, 6, 6, 8, 8};
  *first = kFirst[col < 0 ? 0 : (col > 9 ? 9 : col)];
  *last = *first == 0 ? 3 : *first + 1;
}

// The character typed by a key of the top four rows (capitals with CAPS on).
inline char CharAt(int row, int col, bool caps) {
  static const char kText[4][11] = {"abcdefghij", "klmnopqrst", "uvwxyz-'.!", "1234567890"};
  if (row < 0 || row >= kSpecialRow || col < 0 || col >= kCols) return 0;
  const char c = kText[row][col];
  return (caps && c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

inline const char* SpecialLabel(Key k) {
  return k == kSpace ? "SPACE" : (k == kDelete ? "DEL" : (k == kCaps ? "CAPS" : "DONE"));
}

}  // namespace kbd
