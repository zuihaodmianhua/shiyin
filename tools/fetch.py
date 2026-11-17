# -*- coding: utf-8 -*-
"""Fetch build dependencies (toolchain + third-party single-header libs).
TLS verification is disabled on purpose: this machine sits behind a filtering
proxy whose CA is not installed in the OpenSSL trust store.
"""
import os
import ssl
import sys
import urllib.request

ctx = ssl._create_unverified_context()
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ITEMS = [
    ("third_party/stb_image.h",
     "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"),
    ("third_party/minimp3.h",
     "https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h"),
    ("third_party/minimp3_ex.h",
     "https://raw.githubusercontent.com/lieff/minimp3/master/minimp3_ex.h"),
]


def fetch(url, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 1024:
        print("skip (exists):", dest)
        return
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=120, context=ctx) as r, open(dest, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        done = 0
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            if total:
                pct = done * 100 // total
                print("\r  %s %d%% (%0.1f/%0.1f MB)" % (
                    os.path.basename(dest), pct, done / 1e6, total / 1e6), end="", flush=True)
        print()
    print("done:", dest, os.path.getsize(dest))


if __name__ == "__main__":
    for rel, url in ITEMS:
        fetch(url, os.path.join(ROOT, rel))
