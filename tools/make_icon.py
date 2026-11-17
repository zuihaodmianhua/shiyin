# -*- coding: utf-8 -*-
"""Generate res/shiyin.ico: green rounded tile with a dark music note."""
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GREEN = (0x3F, 0xC9, 0x96)
GREEN2 = (0x2E, 0xA8, 0x7C)
DARK = (0x0B, 0x18, 0x13)


def rounded_alpha(x, y, w, h, r):
    """1.0 inside the rounded rect, 0.0 outside, with a soft edge."""
    cx = min(max(x, r), w - 1 - r)
    cy = min(max(y, r), h - 1 - r)
    dx = x - cx
    dy = y - cy
    d = (dx * dx + dy * dy) ** 0.5
    if d <= r - 0.5:
        return 1.0
    if d >= r + 0.5:
        return 0.0
    return r + 0.5 - d


def note(x, y, w, h):
    """Return 1 where the note glyph is."""
    cx = w * 0.38
    cy = h * 0.68
    rx = w * 0.19
    ry = h * 0.14
    if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
        return 1
    stem_x = cx + rx * 0.78
    if stem_x - w * 0.045 <= x <= stem_x + w * 0.045 and h * 0.24 <= y <= cy:
        return 1
    # flag
    if stem_x <= x <= stem_x + w * 0.22 and h * 0.24 <= y <= h * 0.40:
        t = (y - h * 0.24) / (h * 0.16)
        if x <= stem_x + w * 0.22 * (1.0 - t * 0.55):
            return 1
    return 0


def render(size):
    px = []
    for y in range(size):
        row = []
        for x in range(size):
            a = rounded_alpha(x, y, size, size, size * 0.24)
            t = y / max(1, size - 1)
            c = tuple(int(GREEN[i] + (GREEN2[i] - GREEN[i]) * t) for i in range(3))
            if note(x, y, size, size) and a > 0.35:
                c = DARK
            row.append((c[0], c[1], c[2], int(a * 255)))
        px.append(row)
    return px


def ico_image(px, size):
    hdr = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, size * size * 4, 0, 0, 0, 0)
    data = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            r, g, b, a = px[y][x]
            data += bytes((b, g, r, a))
    mask_row = ((size + 31) // 32) * 4
    for y in range(size):
        data += b"\x00" * mask_row
    return hdr + bytes(data)


def main():
    sizes = [16, 24, 32, 48]
    images = []
    for s in sizes:
        images.append((s, ico_image(render(s), s)))
    out = bytearray()
    out += struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for s, img in images:
        out += struct.pack("<BBBBHHII", s, s, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    for s, img in images:
        out += img
    path = os.path.join(ROOT, "res", "shiyin.ico")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(bytes(out))
    print("wrote", path, len(out))


if __name__ == "__main__":
    main()
