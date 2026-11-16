// The PNG row decoder (src/features/ci/PngRows.h) against pictures written by
// tests/ci/make_png.py: every colour type, bit depth and row filter, with the
// pixels each must give. zlib inflates here; the device uses its ROM inflater.
#include "PngRows.h"
#include <zlib.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
using namespace smalltv;

static std::string dir;
static std::string file(const std::string& name) {
  std::ifstream in(dir + "/" + name, std::ios::binary);
  if (!in) { fprintf(stderr, "missing %s\n", name.c_str()); assert(false); }
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}
static uint32_t be32(const std::string& s, size_t at) {
  return (uint32_t(uint8_t(s[at])) << 24) | (uint32_t(uint8_t(s[at + 1])) << 16) | (uint32_t(uint8_t(s[at + 2])) << 8) | uint8_t(s[at + 3]);
}

// Decodes `png`. False when the header is refused; asserts on anything else.
static bool decode(const std::string& png, std::vector<uint16_t>& pixels, uint32_t& width, uint32_t& height) {
  assert(png.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0);
  std::unique_ptr<PngRows> rows(new PngRows());
  z_stream z{};
  assert(inflateInit(&z) == Z_OK);
  bool header = false, ended = false;
  uint32_t nextRow = 0;
  for (size_t at = 8; at + 12 <= png.size();) {
    const uint32_t length = be32(png, at);
    const std::string kind = png.substr(at + 4, 4);
    const uint8_t* data = reinterpret_cast<const uint8_t*>(png.data()) + at + 8;
    if (kind == "IHDR") {
      assert(length == 13);
      if (!rows->begin(data)) { inflateEnd(&z); return false; }
      header = true;
      width = rows->width();
      height = rows->height();
      pixels.assign(size_t(width) * height, 0xDEAD);
    } else if (kind == "PLTE") {
      rows->palette(data, length);
    } else if (kind == "IDAT") {
      assert(header);
      z.next_in = const_cast<Bytef*>(data);
      z.avail_in = length;
      while (z.avail_in && !ended) {
        uint8_t out[97];                 // odd and small: rows arrive in pieces
        z.next_out = out;
        z.avail_out = sizeof(out);
        const int rc = inflate(&z, Z_NO_FLUSH);
        assert(rc == Z_OK || rc == Z_STREAM_END);
        const bool ok = rows->feed(out, sizeof(out) - z.avail_out, [&](uint32_t y, const uint16_t* line) {
          assert(y == nextRow++);
          for (uint32_t x = 0; x < width; ++x) pixels[size_t(y) * width + x] = line[x];
        });
        assert(ok);
        if (rc == Z_STREAM_END) ended = true;
      }
    }
    at += 12 + length;
  }
  inflateEnd(&z);
  assert(header && ended && rows->done() && nextRow == height);
  return true;
}

int main(int argc, char** argv) {
  assert(argc == 2);
  dir = argv[1];
  const char* good[] = {"rgb8", "rgba8", "rgb16", "rgba16", "grey8", "grey16", "grey1", "grey2", "grey4", "greya8",
                        "greya16", "pal8", "pal4", "pal2", "pal1", "avatar16", "widest", "one"};
  for (const char* name : good) {
    std::vector<uint16_t> pixels;
    uint32_t w = 0, h = 0;
    assert(decode(file(std::string(name) + ".png"), pixels, w, h));
    const std::string expected = file(std::string(name) + ".565");
    assert(expected.size() == pixels.size() * 2);
    size_t wrong = 0;
    for (size_t i = 0; i < pixels.size(); ++i) {
      const uint16_t e = uint16_t(uint8_t(expected[i * 2]) | (uint8_t(expected[i * 2 + 1]) << 8));
      if (e != pixels[i] && !wrong++) fprintf(stderr, "%s: pixel %zu is %04x, expected %04x\n", name, i, pixels[i], e);
    }
    if (wrong) { fprintf(stderr, "%s: %zu of %zu pixels differ\n", name, wrong, pixels.size()); return 1; }
  }
  for (const char* name : {"bad-wide", "bad-interlaced"}) {
    std::vector<uint16_t> pixels;
    uint32_t w = 0, h = 0;
    assert(!decode(file(std::string(name) + ".png"), pixels, w, h));
  }
  {  // Headers that are refused, and a filter that does not exist.
    std::unique_ptr<PngRows> rows(new PngRows());
    const uint8_t rgb3[13] = {0, 0, 0, 4, 0, 0, 0, 4, 3, 2, 0, 0, 0};      // truecolour at 3 bits
    const uint8_t pal16[13] = {0, 0, 0, 4, 0, 0, 0, 4, 16, 3, 0, 0, 0};    // a 16-bit palette index
    const uint8_t empty[13] = {0, 0, 0, 0, 0, 0, 0, 4, 8, 2, 0, 0, 0};     // no width
    const uint8_t type5[13] = {0, 0, 0, 4, 0, 0, 0, 4, 8, 5, 0, 0, 0};     // no such colour type
    assert(!rows->begin(rgb3) && !rows->begin(pal16) && !rows->begin(empty) && !rows->begin(type5));
    const uint8_t ok[13] = {0, 0, 0, 2, 0, 0, 0, 1, 8, 0, 0, 0, 0};        // 2x1 grey
    assert(rows->begin(ok));
    const uint8_t badFilter[3] = {7, 1, 2};
    int calls = 0;
    assert(!rows->feed(badFilter, 3, [&](uint32_t, const uint16_t*) { ++calls; }) && calls == 0);
    assert(rows->begin(ok));
    const uint8_t row[5] = {0, 0xFF, 0x00, 9, 9};                          // bytes past the picture are ignored
    assert(rows->feed(row, 5, [&](uint32_t y, const uint16_t* line) { ++calls; assert(y == 0 && line[0] == 0xFFFF && line[1] == 0); }));
    assert(calls == 1 && rows->done());
  }
  puts("ci png tests passed");
}
