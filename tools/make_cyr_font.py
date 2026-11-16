#!/usr/bin/env python3
"""Generate src/features/theme/FontCyr866.h: Cyrillic glyphs for the built-in 5x7 font.

The firmware draws text with the classic 6x8 cell font of the GFX library
(font/glcdfont.h), which has no Cyrillic. This script holds the Russian
letters as ASCII art and writes them in the same format: five column bytes per
glyph, bit 0 the top row. The firmware addresses them by CP866 code, the code
page that puts Cyrillic where the font's accented Latin letters are.

    python3 tools/make_cyr_font.py            write the header
    python3 tools/make_cyr_font.py --png F    also write a preview image
    python3 tools/make_cyr_font.py --hex      print the table as hex (for
                                              skills/smalltv-theme-clock/scripts/stheme.py)

Letters that look the same as a Latin one are copied from glcdfont.h, read from
the PlatformIO library directory (build the firmware once so it exists), or
from the directory in SMALLTV_GFX_INCLUDE.

Python 3 stdlib only.
"""
import os
import pathlib
import re
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "src" / "features" / "theme" / "FontCyr866.h"

# Same shape as a Latin letter: taken from glcdfont.h.
LATIN = {
    "А": "A", "В": "B", "Е": "E", "К": "K", "М": "M", "Н": "H", "О": "O",
    "Р": "P", "С": "C", "Т": "T", "Х": "X",
    "а": "a", "е": "e", "о": "o", "р": "p", "с": "c", "у": "y", "х": "x",
}

# Up to eight rows of five pixels; missing rows are blank. Row 8 is the
# descender row, as for the Latin g and p.
ART = {
    "Б": ["#####", "#....", "#....", "####.", "#...#", "#...#", "####."],
    "Г": ["#####", "#....", "#....", "#....", "#....", "#....", "#...."],
    "Д": ["..##.", ".#.#.", ".#.#.", ".#.#.", ".#.#.", "#####", "#...#"],
    "Ж": ["#.#.#", "#.#.#", ".###.", "..#..", ".###.", "#.#.#", "#.#.#"],
    "З": [".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###."],
    "И": ["#...#", "#...#", "#..##", "#.#.#", "##..#", "#...#", "#...#"],
    "Й": ["..#..", "#...#", "#..##", "#.#.#", "##..#", "#...#", "#...#"],
    "Л": ["..###", ".#..#", ".#..#", ".#..#", ".#..#", ".#..#", "#...#"],
    "П": ["#####", "#...#", "#...#", "#...#", "#...#", "#...#", "#...#"],
    "У": ["#...#", "#...#", "#...#", ".####", "....#", "....#", ".###."],
    "Ф": ["..#..", ".###.", "#.#.#", "#.#.#", ".###.", "..#..", "..#.."],
    "Ц": ["#..#.", "#..#.", "#..#.", "#..#.", "#..#.", "#..#.", "#####", "....#"],
    "Ч": ["#...#", "#...#", "#...#", ".####", "....#", "....#", "....#"],
    "Ш": ["#.#.#", "#.#.#", "#.#.#", "#.#.#", "#.#.#", "#.#.#", "#####"],
    "Щ": ["#.#.#", "#.#.#", "#.#.#", "#.#.#", "#.#.#", "#.#.#", "#####", "....#"],
    "Ъ": ["##...", ".#...", ".#...", ".###.", ".#..#", ".#..#", ".###."],
    "Ы": ["#...#", "#...#", "#...#", "##..#", "#.#.#", "#.#.#", "##..#"],
    "Ь": ["#....", "#....", "#....", "####.", "#...#", "#...#", "####."],
    "Э": [".###.", "#...#", "....#", "..###", "....#", "#...#", ".###."],
    "Ю": ["#..#.", "#.#.#", "#.#.#", "###.#", "#.#.#", "#.#.#", "#..#."],
    "Я": [".####", "#...#", "#...#", ".####", "..#.#", ".#..#", "#...#"],
    "Ё": [".#.#.", "#####", "#....", "####.", "#....", "#....", "#####"],
    "б": ["...##", ".##..", "#....", "####.", "#...#", "#...#", ".###."],
    "в": [".....", ".....", "####.", "#...#", "####.", "#...#", "####."],
    "г": [".....", ".....", "####.", "#....", "#....", "#....", "#...."],
    "д": [".....", ".....", "..##.", ".#.#.", ".#.#.", "#####", "#...#"],
    "ж": [".....", ".....", "#.#.#", "#.#.#", ".###.", "#.#.#", "#.#.#"],
    "з": [".....", ".....", "####.", "....#", ".###.", "....#", "####."],
    "и": [".....", ".....", "#...#", "#..##", "#.#.#", "##..#", "#...#"],
    "й": [".#.#.", "..#..", "#...#", "#..##", "#.#.#", "##..#", "#...#"],
    "к": [".....", ".....", "#..#.", "#.#..", "##...", "#.#..", "#..#."],
    "л": [".....", ".....", "..###", ".#..#", ".#..#", ".#..#", "#...#"],
    "м": [".....", ".....", "#...#", "##.##", "#.#.#", "#...#", "#...#"],
    "н": [".....", ".....", "#...#", "#...#", "#####", "#...#", "#...#"],
    "п": [".....", ".....", "#####", "#...#", "#...#", "#...#", "#...#"],
    "т": [".....", ".....", "#####", "..#..", "..#..", "..#..", "..#.."],
    "ф": ["..#..", "..#..", ".###.", "#.#.#", "#.#.#", ".###.", "..#..", "..#.."],
    "ц": [".....", ".....", "#..#.", "#..#.", "#..#.", "#..#.", "#####", "....#"],
    "ч": [".....", ".....", "#...#", "#...#", ".####", "....#", "....#"],
    "ш": [".....", ".....", "#.#.#", "#.#.#", "#.#.#", "#.#.#", "#####"],
    "щ": [".....", ".....", "#.#.#", "#.#.#", "#.#.#", "#.#.#", "#####", "....#"],
    "ъ": [".....", ".....", "##...", ".#...", ".###.", ".#..#", ".###."],
    "ы": [".....", ".....", "#...#", "#...#", "##..#", "#.#.#", "##..#"],
    "ь": [".....", ".....", "#....", "#....", "####.", "#...#", "####."],
    "э": [".....", ".....", ".###.", "....#", "..###", "....#", ".###."],
    "ю": [".....", ".....", "#..#.", "#.#.#", "###.#", "#.#.#", "#..#."],
    "я": [".....", ".....", ".####", "#...#", ".####", ".#..#", "#...#"],
    "ё": [".#.#.", ".....", ".###.", "#...#", "#####", "#....", ".###."],
}

# The order of the table: CP866 codes 0x80-0xAF, 0xE0-0xEF, then 0xF0, 0xF1.
LETTERS = "АБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдежзийклмнопрстуфхцчшщъыьэюяЁё"


def glcdfont():
    base = os.environ.get("SMALLTV_GFX_INCLUDE")
    if base:
        path = pathlib.Path(base) / "font" / "glcdfont.h"
    else:
        found = sorted(ROOT.glob(".pio/libdeps/*/GFX Library for Arduino/src/font/glcdfont.h"))
        if not found:
            sys.exit("glcdfont.h not found: build the firmware once, or set SMALLTV_GFX_INCLUDE")
        path = found[0]
    text = path.read_text()
    body = text[text.index("font[]"):]
    values = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
    if len(values) < 256 * 5:
        sys.exit(f"{path}: expected 1280 font bytes, found {len(values)}")
    return values[:256 * 5]


def columns(rows):
    for r in rows:
        if len(r) != 5 or set(r) - set("#."):
            sys.exit(f"bad row {r!r}")
    if len(rows) > 8:
        sys.exit("a glyph has at most eight rows")
    return [sum(1 << y for y, r in enumerate(rows) if r[x] == "#") for x in range(5)]


def table():
    base = glcdfont()
    out = []
    for ch in LETTERS:
        assert ch.encode("cp866")  # every letter has a CP866 code
        if ch in LATIN:
            code = ord(LATIN[ch])
            out += base[code * 5:code * 5 + 5]
        else:
            out += columns(ART[ch])
    return out


def header(data):
    lines = [
        "// GENERATED FILE -- do not edit by hand.",
        "// Produced by tools/make_cyr_font.py, which holds the glyphs as ASCII art.",
        "//",
        "// Russian letters for the built-in 5x7 font, in the format of the GFX",
        "// library's glcdfont.h: five column bytes per glyph, bit 0 the top row.",
        "// Text is held one byte per character in CP866, see Text866.h.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "namespace smalltv {",
        "// Index of a CP866 Cyrillic code in kFontCyr866, -1 for any other byte.",
        "inline int cyr866Index(uint8_t c) {",
        "  if (c >= 0x80 && c <= 0xAF) return c - 0x80;        // А-Я, а-п",
        "  if (c >= 0xE0 && c <= 0xEF) return c - 0xE0 + 48;   // р-я",
        "  if (c == 0xF0 || c == 0xF1) return c - 0xF0 + 64;   // Ё, ё",
        "  return -1;",
        "}",
        "",
        "static const uint8_t kFontCyr866[66 * 5] = {",
    ]
    for i, ch in enumerate(LETTERS):
        cols = ", ".join(f"0x{v:02X}" for v in data[i * 5:i * 5 + 5])
        lines.append(f"  {cols},  // {ch}")
    lines += [
        "};",
        "",
        "// One column of a glyph: the Cyrillic table for a CP866 letter, else `latin`,",
        "// the 256-glyph glcdfont table.",
        "inline uint8_t fontColumn866(const uint8_t* latin, uint8_t c, uint8_t col) {",
        "  int k = cyr866Index(c);",
        "  return k >= 0 ? kFontCyr866[k * 5 + col] : latin[unsigned(c) * 5 + col];",
        "}",
        "}  // namespace smalltv",
        "",
    ]
    return "\n".join(lines)


def png(path, data):
    # Sample lines at 4x, white on black, one PNG without any library.
    base = glcdfont()
    lines = ["АБВГДЕЁЖЗИЙКЛМНОП", "РСТУФХЦЧШЩЪЫЬЭЮЯ", "абвгдеёжзийклмноп", "рстуфхцчшщъыьэюя",
             "Понедельник, 16 ноября", "Съешь ещё этих мягких", "французских булок, да", "выпей чаю. 12:34 +7°"]
    scale, cols = 4, max(len(s) for s in lines)
    w, h = cols * 6 * scale + 8, len(lines) * 10 * scale + 8
    img = [bytearray(w) for _ in range(h)]
    for row, s in enumerate(lines):
        for i, ch in enumerate(s):
            if ch in LETTERS:
                k = LETTERS.index(ch)
                g = data[k * 5:k * 5 + 5]
            elif ch == "°":
                g = base[0xF8 * 5:0xF8 * 5 + 5]
            else:
                g = base[ord(ch) * 5:ord(ch) * 5 + 5]
            for x in range(5):
                for y in range(8):
                    if g[x] >> y & 1:
                        for dy in range(scale):
                            for dx in range(scale):
                                img[4 + (row * 10 + y) * scale + dy][4 + (i * 6 + x) * scale + dx] = 255
    raw = b"".join(b"\0" + bytes(r) for r in img)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    pathlib.Path(path).write_bytes(
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    data = table()
    if "--hex" in sys.argv:
        print(bytes(data).hex())
        return
    OUT.write_text(header(data), encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}")
    if "--png" in sys.argv:
        path = sys.argv[sys.argv.index("--png") + 1]
        png(path, data)
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
