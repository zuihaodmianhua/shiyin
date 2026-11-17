# -*- coding: utf-8 -*-
"""Generate test media for Shiyin: WAV, MP3 (with ID3v2 cover), AIFF and AU."""
import array
import math
import os
import struct
import sys

sys.path.insert(0, r"C:\Users\Administrator\Documents\deepseek-harness\default-workspace\pylibs")
import av  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "media")
WALL = r"C:\Windows\Web\Wallpaper"

COVERS = [
    os.path.join(WALL, "ThemeA", "img20.jpg"),
    os.path.join(WALL, "Spotlight", "img50.jpg"),
    os.path.join(WALL, "ThemeA", "img24.jpg"),
    os.path.join(WALL, "Windows", "img0.jpg"),
]


def cover(i):
    c = COVERS[i % len(COVERS)]
    if os.path.exists(c):
        return open(c, "rb").read()
    return None


def synth(path_wav, seconds, base, tempo=0.36):
    rate = 44100
    n = int(rate * seconds)
    left = array.array("h")
    right = array.array("h")
    chords = [
        (0, 4, 7, 11), (2, 5, 9, 12), (-3, 2, 5, 9), (0, 4, 7, 12),
    ]
    for i in range(n):
        t = i / rate
        beat = t / tempo
        bar = int(beat) % len(chords)
        step = int(beat * 4) % 4
        semi = chords[bar][step]
        f = base * (2 ** (semi / 12.0))
        env = math.exp(-((beat * 4) % 1.0) * 3.0)
        v = 0.0
        v += math.sin(2 * math.pi * f * t) * 0.5
        v += math.sin(2 * math.pi * f * 2 * t) * 0.18
        v += math.sin(2 * math.pi * f * 3 * t) * 0.07
        v += math.sin(2 * math.pi * (base / 2) * t) * 0.16
        fade = min(1.0, i / (rate * 0.05), (n - i) / (rate * 0.3))
        v *= env * 0.55 * fade
        l = int(max(-1.0, min(1.0, v * 0.95)) * 32000)
        r = int(max(-1.0, min(1.0, v * 0.80 + v * 0.2 * math.sin(t * 0.8))) * 32000)
        left.append(l)
        right.append(r)

    data = bytearray()
    for i in range(n):
        data += struct.pack("<hh", left[i], right[i])
    with open(path_wav, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", 36 + len(data)))
        f.write(b"WAVEfmt ")
        f.write(struct.pack("<IHHIIHH", 16, 1, 2, rate, rate * 4, 4, 16))
        f.write(b"data")
        f.write(struct.pack("<I", len(data)))
        f.write(data)
    return rate


def transcode_mp3(src, dst, bitrate=192000):
    inp = av.open(src)
    out = av.open(dst, "w", format="mp3")
    stream = out.add_stream("libmp3lame", rate=44100)
    stream.layout = "stereo"
    stream.bit_rate = bitrate
    res = av.AudioResampler(format="s16", layout="stereo", rate=44100)
    for frame in inp.decode(audio=0):
        for rf in res.resample(frame):
            rf.pts = None
            for p in stream.encode(rf):
                out.mux(p)
    for p in stream.encode(None):
        out.mux(p)
    out.close()
    inp.close()


def syncsafe(n):
    return bytes([(n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F])


def frame(fid, payload):
    return fid.encode() + struct.pack(">I", len(payload)) + b"\x00\x00" + payload


def text(fid, value):
    return frame(fid, b"\x01" + value.encode("utf-16"))


def apic(jpeg):
    payload = b"\x01" + b"image/jpeg\x00" + b"\x03" + "\u5c01\u9762".encode("utf-16") + b"\x00\x00" + jpeg
    return frame("APIC", payload)


def tag(path_mp3, meta, jpeg, version=3):
    body = b""
    for fid, key in (("TIT2", "title"), ("TPE1", "artist"), ("TALB", "album"),
                     ("TPE2", "album_artist"), ("TCON", "genre"),
                     ("TYER", "year"), ("TRCK", "track")):
        if meta.get(key):
            body += text(fid, meta[key])
    if jpeg:
        body += apic(jpeg)
    body += b"\x00" * 64
    header = b"ID3" + bytes([version, 0]) + b"\x00" + syncsafe(len(body))
    data = open(path_mp3, "rb").read()
    open(path_mp3, "wb").write(header + body + data)


def riff_chunk(cid, payload):
    c = cid + struct.pack("<I", len(payload)) + payload
    if len(payload) & 1:
        c += b"\x00"
    return c


def read_riff(path):
    d = open(path, "rb").read()
    assert d[:4] == b"RIFF"
    fmt = None
    data = None
    off = 12
    while off + 8 <= len(d):
        cid = d[off:off + 4]
        ln = struct.unpack("<I", d[off + 4:off + 8])[0]
        body = d[off + 8:off + 8 + ln]
        if cid == b"fmt ":
            fmt = body
        elif cid == b"data":
            data = body
        off += 8 + ln + (ln & 1)
    return fmt, data


def add_wav_info(path, info, jpeg=None):
    fmt, data = read_riff(path)
    lst = b"INFO"
    for cid, val in (("INAM", info.get("title")), ("IART", info.get("artist")),
                     ("IPRD", info.get("album")), ("ICRD", info.get("year")),
                     ("IGNR", info.get("genre"))):
        if val:
            lst += riff_chunk(cid.encode(), val.encode("gbk", "replace") + b"\x00")
    body = riff_chunk(b"fmt ", fmt) + riff_chunk(b"data", data) + riff_chunk(b"LIST", lst)
    if jpeg:
        id3body = b""
        for fid, key in (("TIT2", "title"), ("TPE1", "artist"), ("TALB", "album")):
            if info.get(key):
                id3body += text(fid, info[key])
        id3body += apic(jpeg) + b"\x00" * 32
        tagbytes = b"ID3\x03\x00\x00" + syncsafe(len(id3body)) + id3body
        body += riff_chunk(b"id3 ", tagbytes)
    out = b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body
    open(path, "wb").write(out)


def be_chunk(cid, payload):
    c = cid + struct.pack(">I", len(payload)) + payload
    if len(payload) & 1:
        c += b"\x00"
    return c


def write_aiff(path, seconds, base):
    rate = 44100
    n = int(rate * seconds)
    frames = bytearray()
    for i in range(n):
        t = i / rate
        v = math.sin(2 * math.pi * base * t) * 0.4 + math.sin(2 * math.pi * base * 1.5 * t) * 0.2
        v *= min(1.0, i / 2000.0, (n - i) / 20000.0)
        s = int(max(-1.0, min(1.0, v)) * 30000)
        frames += struct.pack(">hh", s, s)
    ext = struct.pack(">HQ", 16398, 44100 << 48)
    comm = struct.pack(">hIh", 2, n, 16) + ext
    ssnd = struct.pack(">II", 0, 0) + bytes(frames)
    body = be_chunk(b"COMM", comm) + be_chunk(b"NAME", b"Tide") + \
        be_chunk(b"AUTH", b"Kafka by the Sea") + be_chunk(b"SSND", ssnd)
    open(path, "wb").write(b"FORM" + struct.pack(">I", len(body) + 4) + b"AIFF" + body)


def write_au(path, seconds, base):
    rate = 44100
    n = int(rate * seconds)
    pcm = bytearray()
    for i in range(n):
        t = i / rate
        v = math.sin(2 * math.pi * base * t) * 0.35
        v *= min(1.0, i / 2000.0, (n - i) / 20000.0)
        s = int(max(-1.0, min(1.0, v)) * 30000)
        pcm += struct.pack(">hh", s, s)
    hdr = b".snd" + struct.pack(">IIIII", 28, len(pcm), 3, rate, 2)
    open(path, "wb").write(hdr + bytes(pcm))


def main():
    os.makedirs(OUT, exist_ok=True)
    tmp = os.path.join(OUT, "_tmp.wav")

    tracks = [
        dict(file="01-night-flight.mp3", title="\u591c\u822a\u661f", artist="\u62fe\u5149\u4e50\u961f",
             album="\u57ce\u5e02\u591c\u884c", year="2021", genre="\u7535\u5b50",
             track="3", seconds=9, base=220.0, cover=0),
        dict(file="02-two-moons.mp3", title="\u53cc\u6708", artist="\u6797\u6df1",
             album="\u9759\u6c34", year="2019", genre="\u6c11\u8c23",
             track="5", seconds=7, base=196.0, cover=None),
        dict(file="03-morning-light.wav", title="Morning Light", artist="Aya Sato",
             album="Daybreak", year="2022", genre="Ambient", seconds=6, base=261.6, cover=1),
        dict(file="04-tide.aiff", title="Tide", artist="Kafka by the Sea",
             album="Shoreline", year="2018", seconds=5, base=174.6, cover=None),
        dict(file="05-silence.au", title="Silence", artist="Studio AU",
             album="Formats", year="2017", seconds=4, base=329.6, cover=None),
    ]

    for t in tracks:
        path = os.path.join(OUT, t["file"])
        if t["file"].endswith(".mp3"):
            synth(tmp, t["seconds"], t["base"])
            transcode_mp3(tmp, path)
            jpg = None
            if t["cover"] is not None:
                jpg = cover(t["cover"])
            tag(path, t, jpg)
        elif t["file"].endswith(".wav"):
            synth(path, t["seconds"], t["base"])
            jpg = cover(t["cover"]) if t["cover"] is not None else None
            add_wav_info(path, t, jpg)
        elif t["file"].endswith(".aiff"):
            write_aiff(path, t["seconds"], t["base"])
        elif t["file"].endswith(".au"):
            write_au(path, t["seconds"], t["base"])
        print("wrote", path, os.path.getsize(path))
    if os.path.exists(tmp):
        os.remove(tmp)


if __name__ == "__main__":
    main()

