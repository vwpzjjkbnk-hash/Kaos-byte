// SkyPortal360 - QOI decoder. See qoi.h. Follows the QOI specification (qoiformat.org, public
// domain reference by Dominic Szablewski); written from the spec.
#include "qoi.h"

namespace qoi {

namespace {

const int kHeader = 14;
const int kEnd = 8;
const unsigned char kOpRgb = 0xFE, kOpRgba = 0xFF;
const unsigned char kMask2 = 0xC0, kOpIndex = 0x00, kOpDiff = 0x40, kOpLuma = 0x80;  // else 0xC0: run

unsigned int Be32(const unsigned char* p) {
  return (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 | (unsigned int)p[2] << 8 | p[3];
}

}  // namespace

bool Info(const unsigned char* data, int size, int* w, int* h) {
  if (!data || size < kHeader + kEnd) return false;
  if (data[0] != 'q' || data[1] != 'o' || data[2] != 'i' || data[3] != 'f') return false;
  const unsigned int width = Be32(data + 4), height = Be32(data + 8);
  if (!width || !height || width > 8192 || height > 8192) return false;
  *w = (int)width;
  *h = (int)height;
  return true;
}

bool Decode(const unsigned char* data, int size, unsigned int* out, int max_pixels) {
  int w = 0, h = 0;
  if (!Info(data, size, &w, &h) || w * h > max_pixels) return false;
  unsigned char index[64][4];
  for (int i = 0; i < 64; ++i) index[i][0] = index[i][1] = index[i][2] = index[i][3] = 0;
  unsigned char r = 0, g = 0, b = 0, a = 255;
  int p = kHeader;
  const int end = size - kEnd;
  int run = 0;
  const int total = w * h;
  for (int px = 0; px < total; ++px) {
    if (run > 0) {
      --run;
    } else if (p < end) {
      const unsigned char b1 = data[p++];
      if (b1 == kOpRgb) {
        if (p + 3 > end) return false;
        r = data[p]; g = data[p + 1]; b = data[p + 2];
        p += 3;
      } else if (b1 == kOpRgba) {
        if (p + 4 > end) return false;
        r = data[p]; g = data[p + 1]; b = data[p + 2]; a = data[p + 3];
        p += 4;
      } else if ((b1 & kMask2) == kOpIndex) {
        const unsigned char* c = index[b1];
        r = c[0]; g = c[1]; b = c[2]; a = c[3];
      } else if ((b1 & kMask2) == kOpDiff) {
        r = (unsigned char)(r + ((b1 >> 4) & 3) - 2);
        g = (unsigned char)(g + ((b1 >> 2) & 3) - 2);
        b = (unsigned char)(b + (b1 & 3) - 2);
      } else if ((b1 & kMask2) == kOpLuma) {
        if (p >= end) return false;
        const unsigned char b2 = data[p++];
        const int vg = (b1 & 0x3F) - 32;
        r = (unsigned char)(r + vg - 8 + ((b2 >> 4) & 15));
        g = (unsigned char)(g + vg);
        b = (unsigned char)(b + vg - 8 + (b2 & 15));
      } else {  // kOpRun
        run = b1 & 0x3F;
      }
      unsigned char* c = index[(r * 3 + g * 5 + b * 7 + a * 11) & 63];
      c[0] = r; c[1] = g; c[2] = b; c[3] = a;
    } else {
      return false;
    }
    out[px] = (unsigned int)a << 24 | (unsigned int)r << 16 | (unsigned int)g << 8 | b;
  }
  return true;
}

Reader::Reader(ReadFn read, void* ctx, unsigned char* buf, int cap)
    : read_(read), ctx_(ctx), buf_(buf), cap_(cap), pos_(0), len_(0), w_(0), h_(0),
      r_(0), g_(0), b_(0), a_(255), run_(0), done_(0) {
  for (int i = 0; i < 64; ++i) index_[i][0] = index_[i][1] = index_[i][2] = index_[i][3] = 0;
}

bool Reader::Need(int n) {
  if (len_ - pos_ >= n) return true;
  // Move what's left to the front and fill the rest.
  int left = len_ - pos_;
  for (int i = 0; i < left; ++i) buf_[i] = buf_[pos_ + i];
  pos_ = 0;
  len_ = left;
  while (len_ < n) {
    const int got = read_(ctx_, buf_ + len_, cap_ - len_);
    if (got <= 0) return false;
    len_ += got;
  }
  return true;
}

bool Reader::Header(int* w, int* h) {
  if (!buf_ || cap_ < 64 || !Need(kHeader)) return false;
  if (!Info(buf_ + pos_, kHeader + kEnd, &w_, &h_)) return false;
  pos_ += kHeader;
  *w = w_;
  *h = h_;
  return true;
}

bool Reader::Decode(unsigned int* out, int max_pixels) {
  if (!w_ || w_ * h_ > max_pixels || done_ != 0) return false;
  return DecodeMore(out, w_ * h_);
}

bool Reader::DecodeMore(unsigned int* out, int count) {
  if (!w_ || count < 0 || done_ + count > w_ * h_) return false;
  unsigned char r = r_, g = g_, b = b_, a = a_;
  int run = run_;
  for (int px = 0; px < count; ++px) {
    if (run > 0) {
      --run;
    } else {
      if (!Need(1)) return false;
      const unsigned char b1 = buf_[pos_++];
      if (b1 == kOpRgb) {
        if (!Need(3)) return false;
        r = buf_[pos_]; g = buf_[pos_ + 1]; b = buf_[pos_ + 2];
        pos_ += 3;
      } else if (b1 == kOpRgba) {
        if (!Need(4)) return false;
        r = buf_[pos_]; g = buf_[pos_ + 1]; b = buf_[pos_ + 2]; a = buf_[pos_ + 3];
        pos_ += 4;
      } else if ((b1 & kMask2) == kOpIndex) {
        const unsigned char* c = index_[b1];
        r = c[0]; g = c[1]; b = c[2]; a = c[3];
      } else if ((b1 & kMask2) == kOpDiff) {
        r = (unsigned char)(r + ((b1 >> 4) & 3) - 2);
        g = (unsigned char)(g + ((b1 >> 2) & 3) - 2);
        b = (unsigned char)(b + (b1 & 3) - 2);
      } else if ((b1 & kMask2) == kOpLuma) {
        if (!Need(1)) return false;
        const unsigned char b2 = buf_[pos_++];
        const int vg = (b1 & 0x3F) - 32;
        r = (unsigned char)(r + vg - 8 + ((b2 >> 4) & 15));
        g = (unsigned char)(g + vg);
        b = (unsigned char)(b + vg - 8 + (b2 & 15));
      } else {  // kOpRun
        run = b1 & 0x3F;
      }
      unsigned char* c = index_[(r * 3 + g * 5 + b * 7 + a * 11) & 63];
      c[0] = r; c[1] = g; c[2] = b; c[3] = a;
    }
    out[px] = (unsigned int)a << 24 | (unsigned int)r << 16 | (unsigned int)g << 8 | b;
  }
  r_ = r; g_ = g; b_ = b; a_ = a;
  run_ = run;
  done_ += count;
  return true;
}

}  // namespace qoi
