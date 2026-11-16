#!/usr/bin/env python3
"""Write test PNGs for tests/ci/test_png.cpp, with the pixels they must decode to.

    python3 tests/ci/make_png.py OUT_DIR

Each case is NAME.png and NAME.565: the rows as little-endian RGB565, computed
here from the source samples, not by decoding the file. The PNGs are written
by hand (struct + zlib) so that every colour type, bit depth and row filter is
covered, including 16-bit truecolour, the form that started this decoder.
Cases named bad-* have no .565: the decoder must refuse them.

Python 3 stdlib only.
"""
import random
import struct
import sys
import zlib
from pathlib import Path

CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def pack_row(samples, depth):
    """Samples of one row to bytes: big-endian for 16 bits, packed from the high bit below 8."""
    if depth == 16:
        return b''.join(struct.pack('>H', s) for s in samples)
    if depth == 8:
        return bytes(samples)
    out, acc, bits = bytearray(), 0, 0
    for s in samples:
        acc = (acc << depth) | s
        bits += depth
        if bits == 8:
            out.append(acc)
            acc = bits = 0
    if bits:
        out.append(acc << (8 - bits))
    return bytes(out)


def filtered(rows, step):
    out, prev = bytearray(), bytes(len(rows[0]))
    for y, cur in enumerate(rows):
        kind = y % 5                      # every filter in turn
        out.append(kind)
        for i, v in enumerate(cur):
            a = cur[i - step] if i >= step else 0
            b = prev[i]
            c = prev[i - step] if i >= step else 0
            pred = [0, a, b, (a + b) >> 1, paeth(a, b, c)][kind]
            out.append((v - pred) & 0xFF)
        prev = cur
    return bytes(out)


def to8(v, depth):
    return v >> 8 if depth == 16 else v if depth == 8 else v * 255 // ((1 << depth) - 1)


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def make(out, name, width, height, ctype, depth, interlace=0, seed=1):
    rnd = random.Random(seed)
    ch = CHANNELS[ctype]
    top = (1 << depth) - 1
    palette = [tuple(rnd.randrange(256) for _ in range(3)) for _ in range(1 << min(depth, 8))]
    rows, expected = [], bytearray()
    for _ in range(height):
        samples = [rnd.randrange(top + 1) for _ in range(width * ch)]
        rows.append(pack_row(samples, depth))
        for x in range(width):
            s = samples[x * ch:(x + 1) * ch]
            a = 255
            if ctype == 0:
                r = g = b = to8(s[0], depth)
            elif ctype == 2:
                r, g, b = (to8(v, depth) for v in s)
            elif ctype == 3:
                r, g, b = palette[s[0]]
            elif ctype == 4:
                r = g = b = to8(s[0], depth)
                a = to8(s[1], depth)
            else:
                r, g, b = (to8(v, depth) for v in s[:3])
                a = to8(s[3], depth)
            if a != 255:                  # over black
                r, g, b = r * a // 255, g * a // 255, b * a // 255
            expected += struct.pack('<H', rgb565(r, g, b))
    step = max(1, ch * depth // 8)
    data = zlib.compress(filtered(rows, step), 6)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, depth, ctype, 0, 0, interlace))
    png += chunk(b'tEXt', b'Comment\x00a chunk the decoder skips')
    if ctype == 3:
        png += chunk(b'PLTE', b''.join(bytes(c) for c in palette))
    for i in range(0, len(data), 211):    # several IDAT chunks, cut anywhere
        png += chunk(b'IDAT', data[i:i + 211])
    png += chunk(b'IEND', b'')
    (out / (name + '.png')).write_bytes(png)
    if not name.startswith('bad-'):
        (out / (name + '.565')).write_bytes(bytes(expected))


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    make(out, 'rgb8', 37, 23, 2, 8)
    make(out, 'rgba8', 37, 23, 6, 8)
    make(out, 'rgb16', 37, 23, 2, 16)
    make(out, 'rgba16', 37, 23, 6, 16)
    make(out, 'grey8', 37, 23, 0, 8)
    make(out, 'grey16', 37, 23, 0, 16)
    make(out, 'grey1', 37, 23, 0, 1)
    make(out, 'grey2', 37, 23, 0, 2)
    make(out, 'grey4', 37, 23, 0, 4)
    make(out, 'greya8', 37, 23, 4, 8)
    make(out, 'greya16', 37, 23, 4, 16)
    make(out, 'pal8', 37, 23, 3, 8)
    make(out, 'pal4', 37, 23, 3, 4)
    make(out, 'pal2', 37, 23, 3, 2)
    make(out, 'pal1', 37, 23, 3, 1)
    make(out, 'avatar16', 290, 290, 2, 16)     # the size and form of a real avatar
    make(out, 'widest', 320, 9, 6, 16)
    make(out, 'one', 1, 1, 2, 8)
    make(out, 'bad-wide', 321, 2, 2, 8)
    make(out, 'bad-interlaced', 16, 16, 2, 8, interlace=1)


if __name__ == '__main__':
    main()
