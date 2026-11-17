# -*- coding: utf-8 -*-
"""Crop a PNG region and scale it up (nearest neighbour) for close inspection."""
import struct
import sys
import zlib


def read_png(path):
    d = open(path, "rb").read()
    pos, idat, w, h, bd, ct = 8, b"", 0, 0, 0, 0
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        tag = d[pos + 4:pos + 8]
        data = d[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if tag == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", data[:10])
        elif tag == b"IDAT":
            idat += data
    raw = zlib.decompress(idat)
    ch = 3 if ct == 2 else 4
    stride = w * ch + 1
    out = bytearray(w * h * 3)
    for y in range(h):
        o = y * stride + 1
        for x in range(w):
            out[(y * w + x) * 3:(y * w + x) * 3 + 3] = raw[o + x * ch:o + x * ch + 3]
    return w, h, out


def write_png(path, w, h, rgb):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * w * 3:(y + 1) * w * 3]

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    src, dst, x0, y0, x1, y1, scale = sys.argv[1:8]
    x0, y0, x1, y1, scale = int(x0), int(y0), int(x1), int(y1), int(scale)
    w, h, rgb = read_png(src)
    cw, chh = x1 - x0, y1 - y0
    out = bytearray(cw * scale * chh * scale * 3)
    ow = cw * scale
    for y in range(chh):
        for x in range(cw):
            si = ((y0 + y) * w + (x0 + x)) * 3
            for dy in range(scale):
                for dx in range(scale):
                    di = ((y * scale + dy) * ow + (x * scale + dx)) * 3
                    out[di:di + 3] = rgb[si:si + 3]
    write_png(dst, ow, chh * scale, out)
    print("wrote", dst, ow, chh * scale)


if __name__ == "__main__":
    main()
