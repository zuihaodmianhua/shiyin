# -*- coding: utf-8 -*-
"""Behavioural test for the four playback modes.

Plays the last track of the demo list and checks which row is playing after it
ends (detected by the mint equalizer drawn in the index column).
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import crop  # noqa: E402
import shot  # noqa: E402

u = ctypes.windll.user32
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACCENT = (74, 217, 165)


def seed_ini(mode, volume=72):
    media = os.path.join(ROOT, "media")
    names = ["01-night-flight.mp3", "02-two-moons.mp3", "03-morning-light.wav",
             "04-tide.aiff", "05-silence.au"]
    ini = "[ui]\r\nvolume=%d\r\nmode=%d\r\n\r\n[list]\r\ncount=%d\r\n" % (volume, mode, len(names))
    for i, n in enumerate(names):
        ini += "%d=%s\r\n" % (i, os.path.join(media, n))
    open(os.path.join(ROOT, "build", "shiyin.ini"), "w", encoding="ascii").write(ini)


def playing_rows(png):
    w, h, rgb = crop.read_png(png)
    rows = set()
    # index column of the playlist: x ~ 395..430, y 190..560
    for y in range(150, min(h, 620)):
        for x in range(392, min(w, 440)):
            i = (y * w + x) * 3
            if abs(rgb[i] - ACCENT[0]) < 12 and abs(rgb[i + 1] - ACCENT[1]) < 12 \
                    and abs(rgb[i + 2] - ACCENT[2]) < 12:
                rows.add((y - 200) // 65)
    return sorted(rows)


def run(mode, label):
    seed_ini(mode)
    p = subprocess.Popen([os.path.join(ROOT, "build", "Shiyin.exe")],
                         cwd=os.path.join(ROOT, "build"))
    h = 0
    for _ in range(60):
        time.sleep(0.2)
        h = u.FindWindowA(b"ShiyinMain", None)
        if h:
            break
    time.sleep(0.9)
    shot.click(h, 420, 485, dbl=True)   # play track 5 (3 s)
    time.sleep(6.0)              # let it finish
    out = os.path.join(ROOT, "shots", "behave-%s.png" % label)
    shot.capture(h, out)
    rows = playing_rows(out)
    p.terminate()
    return rows


if __name__ == "__main__":
    for mode, label, expect in (
        (0, "order", "stopped"),
        (1, "all", "track 1"),
        (2, "one", "track 5"),
        (3, "shuffle", "track != 5"),
    ):
        rows = run(mode, label)
        print("%-8s mode=%d -> playing rows %s   (expect %s)" % (label, mode, rows, expect))
