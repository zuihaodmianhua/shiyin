# -*- coding: utf-8 -*-
"""Reproduce: add a single file through the Open dialog, close, check the INI."""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

u = ctypes.windll.user32
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
INI = os.path.join(BUILD, "shiyin.ini")
MEDIA = os.path.join(ROOT, "media", "01-night-flight.mp3")

WM_SETTEXT = 0x000C
WM_COMMAND = 0x0111
WM_LBUTTONDOWN, WM_LBUTTONUP = 0x0201, 0x0202


def ini_entries():
    try:
        txt = open(INI, "r", encoding="mbcs", errors="replace").read()
    except OSError:
        return None
    return txt


def find_child(hwnd, cls):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, lp):
        buf = ctypes.create_unicode_buffer(64)
        u.GetClassNameW(h, buf, 64)
        if buf.value == cls:
            found.append(h)
        return True

    u.EnumChildWindows(hwnd, cb, 0)
    return found


def main():
    if os.path.exists(INI):
        os.remove(INI)
    p = subprocess.Popen([os.path.join(BUILD, "Shiyin.exe")], cwd=BUILD)
    h = 0
    for _ in range(60):
        time.sleep(0.2)
        h = u.FindWindowA(b"ShiyinMain", None)
        if h:
            break
    time.sleep(1.0)

    # click "添加文件" (top right)
    rect = wt.RECT()
    u.GetWindowRect(h, ctypes.byref(rect))
    print("window", rect.left, rect.top)
    lp = (42 << 16) | 1148
    u.PostMessageA(h, WM_LBUTTONDOWN, 1, lp)
    u.PostMessageA(h, WM_LBUTTONUP, 0, lp)
    time.sleep(1.2)

    dlg = 0
    for _ in range(30):
        dlg = u.FindWindowA(b"#32770", None)
        if dlg:
            break
        time.sleep(0.2)
    print("dialog", dlg)
    if not dlg:
        print("no dialog")
        p.terminate()
        return 1

    edits = find_child(dlg, "Edit")
    print("edits", edits)
    if not edits:
        p.terminate()
        return 1
    u.SendMessageW(edits[0], WM_SETTEXT, 0, ctypes.c_wchar_p(MEDIA))
    time.sleep(0.3)
    u.PostMessageA(dlg, WM_COMMAND, 1, 0)      # IDOK
    time.sleep(1.5)
    print("dialog still open?", bool(u.IsWindow(dlg)))

    # close the player gracefully
    u.PostMessageA(h, 0x0010, 0, 0)
    for _ in range(40):
        time.sleep(0.1)
        if p.poll() is not None:
            break
    print("exit", p.poll())
    print("--- ini ---")
    print(ini_entries())
    return 0


if __name__ == "__main__":
    sys.exit(main())
