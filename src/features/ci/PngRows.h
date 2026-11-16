// PngRows.h — the pixel rows of a PNG, as RGB565.
//
// A PNG is a header, an optional palette and a zlib stream of filtered rows.
// This class takes the header and the *inflated* bytes of that stream, undoes
// the row filters and hands each row over as RGB565. Inflating is the
// caller's: the device uses the inflater in the ESP32's ROM, the desktop tests
// use zlib.
//
// Handles every non-interlaced PNG up to kMaxWidth pixels wide: greyscale,
// truecolour and palette, with or without alpha, 1 to 16 bits a sample.
// Avatars from a Git server come as 8-bit and as 16-bit truecolour, and the
// PNG library used before read only the first kind. Alpha is laid over
// black. A palette's own transparency (tRNS) is ignored.
//
// Plain C++, no Arduino types. About 6.5 KB: keep it on the heap.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace smalltv {

class PngRows {
 public:
  static const uint32_t kMaxWidth = 320;
  static const uint32_t kMaxPitch = kMaxWidth * 8;   // RGBA at 16 bits a sample

  // The 13 data bytes of the IHDR chunk. False for a picture this class does
  // not read: interlaced, wider than kMaxWidth, or not a valid combination of
  // colour type and bit depth.
  bool begin(const uint8_t* ihdr) {
    width_  = be32(ihdr);
    height_ = be32(ihdr + 4);
    depth_  = ihdr[8];
    type_   = ihdr[9];
    const bool plain = ihdr[10] == 0 && ihdr[11] == 0 && ihdr[12] == 0;   // compression, filter, interlace
    switch (type_) {
      case 0: channels_ = 1; break;   // grey
      case 2: channels_ = 3; break;   // RGB
      case 3: channels_ = 1; break;   // palette index
      case 4: channels_ = 2; break;   // grey + alpha
      case 6: channels_ = 4; break;   // RGB + alpha
      default: return false;
    }
    const bool small = depth_ == 1 || depth_ == 2 || depth_ == 4;
    const bool depthOk = depth_ == 8 || (depth_ == 16 && type_ != 3) || (small && (type_ == 0 || type_ == 3));
    if (!plain || !depthOk || !width_ || !height_ || width_ > kMaxWidth) return false;
    pitch_ = (width_ * channels_ * depth_ + 7) / 8;
    step_  = channels_ * depth_ / 8;
    if (!step_) step_ = 1;             // less than a byte a pixel: filters work on bytes
    fill_ = 0;
    y_ = 0;
    memset(prev_, 0, sizeof(prev_));
    memset(palette_, 0, sizeof(palette_));
    return true;
  }

  // The data of the PLTE chunk: three bytes a colour.
  void palette(const uint8_t* rgb, size_t bytes) {
    if (bytes > sizeof(palette_)) bytes = sizeof(palette_);
    memcpy(palette_, rgb, bytes);
  }

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  bool done() const { return y_ == height_; }

  // Inflated bytes, in any portions. Calls sink(y, const uint16_t* pixels) for
  // every row they complete. False when the data names a filter that does not
  // exist. Bytes past the last row are ignored.
  template <class Sink> bool feed(const uint8_t* data, size_t count, Sink&& sink) {
    while (count && y_ < height_) {
      size_t take = pitch_ + 1 - fill_;        // a row is a filter byte and the pixels
      if (take > count) take = count;
      memcpy(row_ + fill_, data, take);
      fill_ += uint32_t(take);
      data += take;
      count -= take;
      if (fill_ < pitch_ + 1) break;
      if (!unfilter()) return false;
      convert();
      sink(y_, line_);
      memcpy(prev_, row_ + 1, pitch_);
      fill_ = 0;
      ++y_;
    }
    return true;
  }

 private:
  static uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  static uint16_t rgb565(unsigned r, unsigned g, unsigned b) {
    return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  }

  bool unfilter() {
    uint8_t* cur = row_ + 1;
    switch (row_[0]) {
      case 0: break;
      case 1:   // Sub: the pixel to the left
        for (uint32_t i = step_; i < pitch_; ++i) cur[i] = uint8_t(cur[i] + cur[i - step_]);
        break;
      case 2:   // Up: the pixel above
        for (uint32_t i = 0; i < pitch_; ++i) cur[i] = uint8_t(cur[i] + prev_[i]);
        break;
      case 3:   // Average of the two
        for (uint32_t i = 0; i < pitch_; ++i) {
          const unsigned left = i >= step_ ? cur[i - step_] : 0;
          cur[i] = uint8_t(cur[i] + ((left + prev_[i]) >> 1));
        }
        break;
      case 4:   // Paeth: whichever of left, above, upper left is nearest to left + above - upper left
        for (uint32_t i = 0; i < pitch_; ++i) {
          const int a = i >= step_ ? cur[i - step_] : 0, b = prev_[i], c = i >= step_ ? prev_[i - step_] : 0;
          const int p = a + b - c;
          const int pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
          cur[i] = uint8_t(cur[i] + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c));
        }
        break;
      default: return false;
    }
    return true;
  }

  // Sample `index` of the row, as 8 bits: the high byte of a 16-bit sample,
  // a small one stretched to the full range.
  unsigned sample(const uint8_t* cur, uint32_t index) const {
    if (depth_ == 8) return cur[index];
    if (depth_ == 16) return cur[index * 2];
    const unsigned shift = 8 - depth_ - (index * depth_) % 8;
    const unsigned v = (cur[index * depth_ / 8] >> shift) & ((1u << depth_) - 1);
    return type_ == 3 ? v : v * 255 / ((1u << depth_) - 1);
  }

  void convert() {
    const uint8_t* cur = row_ + 1;
    for (uint32_t x = 0; x < width_; ++x) {
      unsigned r, g, b, a = 255;
      switch (type_) {
        case 0: r = g = b = sample(cur, x); break;
        case 2: r = sample(cur, x * 3); g = sample(cur, x * 3 + 1); b = sample(cur, x * 3 + 2); break;
        case 3: {
          const uint8_t* c = palette_ + (sample(cur, x) & 0xFF) * 3;
          r = c[0]; g = c[1]; b = c[2];
          break;
        }
        case 4: r = g = b = sample(cur, x * 2); a = sample(cur, x * 2 + 1); break;
        default: r = sample(cur, x * 4); g = sample(cur, x * 4 + 1); b = sample(cur, x * 4 + 2); a = sample(cur, x * 4 + 3); break;
      }
      if (a != 255) { r = r * a / 255; g = g * a / 255; b = b * a / 255; }   // over black
      line_[x] = rgb565(r, g, b);
    }
  }

  uint32_t width_ = 0, height_ = 0, pitch_ = 0, step_ = 1, fill_ = 0, y_ = 0;
  uint8_t  depth_ = 8, type_ = 2, channels_ = 3;
  uint8_t  row_[kMaxPitch + 1];
  uint8_t  prev_[kMaxPitch];
  uint8_t  palette_[256 * 3];
  uint16_t line_[kMaxWidth];
};

}  // namespace smalltv
