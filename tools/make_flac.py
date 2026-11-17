# -*- coding: utf-8 -*-
"""Create demo FLAC files (Vorbis comments + embedded cover) for the player."""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(ROOT), "pylibs"))

import av  # noqa: E402

MEDIA = os.path.join(ROOT, "media")
TMP = os.path.join(ROOT, "build", "flac_test")
COVER = r"C:\Windows\Web\Wallpaper\ThemeA\img20.jpg"


def encode(src_wav, dst, rate, sample_fmt):
    inp = av.open(src_wav)
    out = av.open(dst, "w", format="flac")
    stream = out.add_stream("flac", rate=rate, layout="stereo")
    res = av.AudioResampler(format=sample_fmt, layout="stereo", rate=rate)
    for frame in inp.decode(audio=0):
        for rf in res.resample(frame):
            rf.pts = None
            for p in stream.encode(rf):
                out.mux(p)
    for p in stream.encode(None):
        out.mux(p)
    out.close()
    inp.close()


def parse_blocks(d):
    assert d[:4] == b"fLaC"
    off = 4
    blocks = []
    last = False
    while not last:
        h = d[off]
        last = bool(h & 0x80)
        typ = h & 0x7F
        ln = (d[off + 1] << 16) | (d[off + 2] << 8) | d[off + 3]
        blocks.append((typ, d[off + 4:off + 4 + ln]))
        off += 4 + ln
    return blocks, off


def build_block(typ, body, last):
    return bytes([typ | (0x80 if last else 0),
                  (len(body) >> 16) & 0xFF, (len(body) >> 8) & 0xFF, len(body) & 0xFF]) + body


def vorbis_comment(tags):
    vendor = b"Shiyin demo"
    body = struct.pack("<I", len(vendor)) + vendor + struct.pack("<I", len(tags))
    for k, v in tags.items():
        s = (k + "=" + v).encode("utf-8")
        body += struct.pack("<I", len(s)) + s
    return body


def picture_block(jpeg):
    body = struct.pack(">I", 3)                       # front cover
    mime = b"image/jpeg"
    body += struct.pack(">I", len(mime)) + mime
    desc = "".encode("utf-8")
    body += struct.pack(">I", len(desc)) + desc
    body += struct.pack(">IIII", 0, 0, 0, 0)
    body += struct.pack(">I", len(jpeg)) + jpeg
    return body


def retag(path, tags, cover_path):
    d = open(path, "rb").read()
    blocks, audio_off = parse_blocks(d)
    streaminfo = next(b for t, b in blocks if t == 0)
    jpeg = open(cover_path, "rb").read() if cover_path and os.path.exists(cover_path) else None
    out = b"fLaC"
    out += build_block(0, streaminfo, False)
    out += build_block(4, vorbis_comment(tags), jpeg is None)
    if jpeg is not None:
        out += build_block(6, picture_block(jpeg), True)
    out += d[audio_off:]
    open(path, "wb").write(out)


def write_wav24(src_pcm, rate, path):
    pcm24 = bytearray()
    for i in range(0, len(src_pcm), 2):
        s = struct.unpack("<h", src_pcm[i:i + 2])[0]
        v = s << 8
        pcm24 += bytes((v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF))
    hdr = b"RIFF" + struct.pack("<I", 36 + len(pcm24)) + b"WAVEfmt " + \
        struct.pack("<IHHIIHH", 16, 1, 2, rate, rate * 6, 6, 24) + \
        b"data" + struct.pack("<I", len(pcm24))
    open(path, "wb").write(hdr + pcm24)


def read_wav(path):
    d = open(path, "rb").read()
    off, rate, pcm = 12, 0, b""
    while off + 8 <= len(d):
        cid = d[off:off + 4]
        ln = struct.unpack("<I", d[off + 4:off + 8])[0]
        body = d[off + 8:off + 8 + ln]
        if cid == b"fmt ":
            _, ch, rate = struct.unpack("<HHI", body[:8])
        elif cid == b"data":
            pcm = body
        off += 8 + ln + (ln & 1)
    return rate, pcm


def main():
    os.makedirs(TMP, exist_ok=True)
    src = os.path.join(MEDIA, "03-morning-light.wav")
    rate, pcm = read_wav(src)

    # 1) 16 bit / 44.1 kHz stereo, Chinese tags + cover
    f1 = os.path.join(MEDIA, "06-deep-blue.flac")
    encode(src, f1, 44100, "s16")
    retag(f1, {
        "TITLE": "\u6DF1\u84DD",
        "ARTIST": "\u591C\u822A\u5458",
        "ALBUM": "\u6F6E\u6C50\u4E4B\u4E0B",
        "DATE": "2023",
        "GENRE": "\u6C1B\u56F4",
        "TRACKNUMBER": "2",
    }, COVER)
    print("wrote", f1, os.path.getsize(f1))

    # 2) 24 bit / 48 kHz stereo
    wav24 = os.path.join(TMP, "src24.wav")
    write_wav24(pcm, rate, wav24)
    f2 = os.path.join(MEDIA, "07-tide-24bit.flac")
    encode(wav24, f2, 48000, "s32")
    retag(f2, {
        "TITLE": "\u6F6E\u5915 24bit",
        "ARTIST": "\u6DF1\u6D77\u5F55\u97F3",
        "ALBUM": "\u9AD8\u89E3\u6790\u6D4B\u8BD5",
        "DATE": "2024",
        "GENRE": "Ambient",
    }, None)
    print("wrote", f2, os.path.getsize(f2))


if __name__ == "__main__":
    main()
