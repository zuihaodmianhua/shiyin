# -*- coding: utf-8 -*-
"""Launch Shiyin, optionally drive it, capture its window to a PNG."""
import ctypes
import ctypes.wintypes as wt
import os
import struct
import subprocess
import sys
import time
import zlib

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    try:
        user32.SetProcessDPIAware()
    except Exception:
        pass

SRCCOPY = 0x00CC0020
PW_RENDERFULLCONTENT = 0x00000002
DIB_RGB_COLORS = 0
WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK = 0x0201, 0x0202, 0x0203


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                ("biPlanes", wt.WORD), ("biBitCount", wt.WORD),
                ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG),
                ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wt.DWORD * 3)]


def capture(hwnd, path):
    rect = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    w = rect.right - rect.left
    h = rect.bottom - rect.top
    hdc = user32.GetWindowDC(hwnd)
    mdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mdc, bmp)
    if not user32.PrintWindow(hwnd, mdc, PW_RENDERFULLCONTENT):
        user32.PrintWindow(hwnd, mdc, 0)

    bi = BITMAPINFO()
    bi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.bmiHeader.biWidth = w
    bi.bmiHeader.biHeight = -h
    bi.bmiHeader.biPlanes = 1
    bi.bmiHeader.biBitCount = 32
    bi.bmiHeader.biCompression = 0
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), DIB_RGB_COLORS)

    raw = bytearray()
    for y in range(h):
        raw.append(0)
        row = buf.raw[y * w * 4:(y + 1) * w * 4]
        for x in range(w):
            raw += bytes((row[x * 4 + 2], row[x * 4 + 1], row[x * 4]))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)

    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(hwnd, hdc)
    return w, h


def click(hwnd, x, y, dbl=False):
    lp = (y << 16) | (x & 0xFFFF)
    user32.PostMessageA(hwnd, WM_LBUTTONDOWN, 1, lp)
    user32.PostMessageA(hwnd, WM_LBUTTONUP, 0, lp)
    if dbl:
        user32.PostMessageA(hwnd, WM_LBUTTONDBLCLK, 1, lp)
        user32.PostMessageA(hwnd, WM_LBUTTONUP, 0, lp)
    time.sleep(0.15)


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print("usage: shot.py <exe> <out.png> [--wait s] [--click x,y[,dbl]] ...")
        return 2
    exe, out = os.path.abspath(args[0]), os.path.abspath(args[1])
    wait = 0.8
    clicks = []
    i = 2
    while i < len(args):
        if args[i] == "--wait":
            wait = float(args[i + 1]); i += 2
        elif args[i] == "--click":
            spec = args[i + 1]; i += 2
            parts = spec.split(",")
            clicks.append((int(parts[0]), int(parts[1]), len(parts) > 2 and parts[2] == "dbl"))
        else:
            i += 1

    os.makedirs(os.path.dirname(out), exist_ok=True)
    proc = subprocess.Popen([exe], cwd=os.path.dirname(exe))
    hwnd = 0
    for _ in range(60):
        time.sleep(0.2)
        hwnd = user32.FindWindowA(b"ShiyinMain", None)
        if hwnd:
            break
    if not hwnd:
        print("window not found, exit", proc.poll())
        return 1
    for (x, y, dbl) in clicks:
        click(hwnd, x, y, dbl)
    time.sleep(wait)
    w, h = capture(hwnd, out)
    print("captured %dx%d -> %s" % (w, h, out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
