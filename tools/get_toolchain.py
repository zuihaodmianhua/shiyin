# -*- coding: utf-8 -*-
"""Download the i686 MinGW-w64 toolchain (only used to produce the Win95 PE)."""
import os
import ssl
import sys
import time
import urllib.request

ctx = ssl._create_unverified_context()
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GH = ("https://github.com/brechtsanders/winlibs_mingw/releases/download/"
      "15.2.0posix-13.0.0-msvcrt-r1/"
      "winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r1.zip")
URLS = [p + GH for p in ("https://gh-proxy.com/", "https://ghproxy.net/", "")]
DEST = os.path.join(ROOT, "tools", "winlibs-i686.zip")
UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}
WANT = 280 * 1000 * 1000


def size(p):
    try:
        return os.path.getsize(p)
    except OSError:
        return 0


def download(url, dest):
    have = size(dest)
    req = urllib.request.Request(url, headers=UA)
    if have:
        req.add_header("Range", "bytes=%d-" % have)
    with urllib.request.urlopen(req, timeout=120, context=ctx) as r, open(dest, "ab") as f:
        total = int(r.headers.get("Content-Length") or 0) + have
        done = have
        last = 0
        while True:
            chunk = r.read(1 << 18)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            if done - last > 15 * 1000 * 1000:
                last = done
                print("  %0.1f / %0.1f MB" % (done / 1e6, total / 1e6), flush=True)
    return size(dest)


if size(DEST) > 200 * 1000 * 1000:
    print("already downloaded", size(DEST))
    raise SystemExit(0)

part = DEST + ".part"
for url in URLS:
    for attempt in range(3):
        try:
            got = download(url, part)
            print("got", got)
            if got >= WANT:
                os.replace(part, DEST)
                print("ok", size(DEST))
                raise SystemExit(0)
        except SystemExit:
            raise
        except Exception as e:
            print("fail", type(e).__name__, e, "-> retry")
            time.sleep(2)
print("download incomplete")
sys.exit(1)
