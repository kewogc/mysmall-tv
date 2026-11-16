// Text866.h — Russian text for the built-in font.
//
// On-screen text is one byte per character, because every layout in the
// firmware counts bytes: ASCII as is, Russian letters in CP866 (the glyphs are
// in FontCyr866.h). Text arrives as UTF-8 (the notify API, theme manifests,
// fetched JSON) and is converted once, where it enters.
//
// Plain C++, no Arduino types: the theme engine and its desktop tools use it too.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace smalltv {

// CP866 code of a Unicode code point the font can draw, 0 for any other.
inline uint8_t cp866FromCodepoint(uint32_t cp) {
  if (cp >= 0x20 && cp < 0x7F) return uint8_t(cp);
  if (cp >= 0x410 && cp <= 0x43F) return uint8_t(0x80 + (cp - 0x410));   // А-Я, а-п
  if (cp >= 0x440 && cp <= 0x44F) return uint8_t(0xE0 + (cp - 0x440));   // р-я
  if (cp == 0x401) return 0xF0;   // Ё
  if (cp == 0x451) return 0xF1;   // ё
  if (cp == 0xB0) return 0xF8;    // degree sign
  if (cp == 0xB7) return 0xFA;    // middle dot
  return 0;
}

// True for a byte that is text: printable ASCII, a CP866 Russian letter, the
// degree sign or the middle dot.
inline bool isText866(uint8_t c) {
  return (c >= 0x20 && c < 0x7F) || (c >= 0x80 && c <= 0xAF) || (c >= 0xE0 && c <= 0xF1) || c == 0xF8 || c == 0xFA;
}

// Converts `length` bytes of UTF-8 at `in` to one byte per character at `out`
// and returns the number of bytes written. The result is never longer than the
// input, so `out` may be `in`. A character the font cannot draw becomes
// `unknown`, or is dropped when `unknown` is 0. A byte that is not part of a
// well-formed UTF-8 sequence is copied unchanged, so text that is ASCII comes
// out as it went in. No terminator is written.
inline size_t utf8ToCp866(const char* in, size_t length, char* out, char unknown = '?') {
  size_t n = 0;
  for (size_t i = 0; i < length;) {
    const uint8_t c = uint8_t(in[i]);
    int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (extra > 0 && i + size_t(extra) < length) {
      uint32_t cp = c & (0x3F >> extra);
      bool ok = true;
      for (int k = 1; k <= extra; ++k) {
        const uint8_t t = uint8_t(in[i + size_t(k)]);
        if ((t & 0xC0) != 0x80) { ok = false; break; }
        cp = (cp << 6) | (t & 0x3F);
      }
      if (ok) {
        const uint8_t b = cp866FromCodepoint(cp);
        if (b) out[n++] = char(b);
        else if (unknown) out[n++] = unknown;
        i += size_t(extra) + 1;
        continue;
      }
    }
    out[n++] = char(c);
    ++i;
  }
  return n;
}

// The same for a NUL-terminated buffer, in place.
inline void utf8ToCp866(char* text, char unknown = '?') {
  size_t length = 0;
  while (text[length]) ++length;
  text[utf8ToCp866(text, length, text, unknown)] = 0;
}

// Number of character cells a NUL-terminated string takes when it is printed
// through the display (Gfx.cpp), which accepts both forms: one-byte text, and
// UTF-8 Russian letters as they stand in string literals (lead byte 0xD0 or
// 0xD1 and one continuation byte).
inline size_t textCells866(const char* text) {
  size_t cells = 0;
  for (size_t i = 0; text[i]; ++i, ++cells) {
    const uint8_t c = uint8_t(text[i]);
    if ((c == 0xD0 || c == 0xD1) && (uint8_t(text[i + 1]) & 0xC0) == 0x80) ++i;
  }
  return cells;
}

}  // namespace smalltv
