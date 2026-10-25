// SkyPortal360 - decoder for QOI ("Quite OK Image", qoiformat.org) files: the drawn menu's
// figure images, logos and background are stored as QOI on the hard drive (made on the PC by
// tools/mkassets.py). Integer-only and portable.
#pragma once

namespace qoi {

// Reads the header. False if `data` isn't a QOI file.
bool Info(const unsigned char* data, int size, int* w, int* h);
// Decodes into `out` (w*h pixels, 0xAARRGGBB). False on a malformed file.
bool Decode(const unsigned char* data, int size, unsigned int* out, int max_pixels);

// Decodes a QOI file read a piece at a time through `read` (returns bytes read, 0 at the end, < 0 on
// an error) into a small buffer, so the whole file never has to be in memory.
typedef int (*ReadFn)(void* ctx, unsigned char* buf, int cap);
class Reader {
 public:
  Reader(ReadFn read, void* ctx, unsigned char* buf, int cap);
  bool Header(int* w, int* h);                    // call first
  bool Decode(unsigned int* out, int max_pixels);  // then this (w*h pixels, 0xAARRGGBB)
  // Or a part at a time: the next `count` pixels (rows in order).
  bool DecodeMore(unsigned int* out, int count);

 private:
  bool Need(int n);  // at least n unread bytes in the buffer
  ReadFn read_;
  void* ctx_;
  unsigned char* buf_;
  int cap_, pos_, len_;
  int w_, h_;
  unsigned char index_[64][4];
  unsigned char r_, g_, b_, a_;
  int run_, done_;
};

}  // namespace qoi
