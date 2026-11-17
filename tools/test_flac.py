# -*- coding: utf-8 -*-
"""Verify the FLAC decoder sample by sample.

Generates FLAC files with PyAV (16 bit stereo / mono / 24 bit stereo), decodes
them with tools/dec_test.exe (the player's own decoder) and compares against
the reference decoder (libFLAC through PyAV) at the file's native rate.
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(ROOT), "pylibs"))

import av  # noqa: E402

BUILD = os.path.join(ROOT, "build")
TMP = os.path.join(BUILD, "flac_test")
CC = os.path.join(ROOT, "tools", "mingw32", "bin", "gcc.exe")

CFLAGS = [
    "-std=gnu99", "-O2", "-march=i486", "-mno-sse", "-mfpmath=387",
    "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
    "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
    "-fno-tree-loop-distribute-patterns", "-fno-ident",
    "-DWINVER=0x0400", "-D_WIN32_WINNT=0x0400", "-D_WIN32_IE=0x0400",
    "-I" + os.path.join(ROOT, "src"), "-I" + os.path.join(ROOT, "third_party"),
]
LDFLAGS = [
    "-nostdlib", "-nostartfiles", "-Wl,--entry=sy_entry",
    "-Wl,--subsystem,windows:4.0",
    "-Wl,--major-subsystem-version,4", "-Wl,--minor-subsystem-version,0",
]


def build_harness():
    out = os.path.join(BUILD, "dec_test.exe")
    objs = []
    for name in ("dec_test", "decode", "flac", "tags", "util"):
        src = os.path.join(ROOT, "src", name + ".c") if name != "dec_test" \
            else os.path.join(ROOT, "tools", "dec_test.c")
        obj = os.path.join(BUILD, "dt_" + name + ".o")
        subprocess.check_call([CC] + CFLAGS + ["-c", "-o", obj, src])
        objs.append(obj)
    subprocess.check_call([CC] + LDFLAGS + ["-o", out] + objs +
                          ["-lwinmm", "-lgdi32", "-luser32", "-lkernel32", "-lgcc"])
    return out


def wav_pcm(path):
    d = open(path, "rb").read()
    assert d[:4] == b"RIFF", path
    off = 12
    while off + 8 <= len(d):
        cid = d[off:off + 4]
        ln = struct.unpack("<I", d[off + 4:off + 8])[0]
        if cid == b"data":
            return d[off + 8:off + 8 + ln]
        off += 8 + ln + (ln & 1)
    raise RuntimeError("no data chunk")


def read_wav(path):
    d = open(path, "rb").read()
    off = 12
    rate = ch = bits = 0
    pcm = b""
    while off + 8 <= len(d):
        cid = d[off:off + 4]
        ln = struct.unpack("<I", d[off + 4:off + 8])[0]
        body = d[off + 8:off + 8 + ln]
        if cid == b"fmt ":
            fmt, ch, rate, _, _, bits = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            pcm = body
        off += 8 + ln + (ln & 1)
    return rate, ch, bits, pcm


def write_wav24(path, rate, pcm24):
    hdr = b"RIFF" + struct.pack("<I", 36 + len(pcm24)) + b"WAVEfmt " + \
        struct.pack("<IHHIIHH", 16, 1, 2, rate, rate * 6, 6, 24) + \
        b"data" + struct.pack("<I", len(pcm24))
    open(path, "wb").write(hdr + pcm24)


def encode_flac(src_wav, dst_flac, fmt):
    inp = av.open(src_wav)
    out = av.open(dst_flac, "w", format="flac")
    stream = out.add_stream("flac")
    if fmt == "mono":
        layout, sample_fmt = "mono", "s16"
    elif fmt == "s32":
        layout, sample_fmt = "stereo", "s32"
    else:
        layout, sample_fmt = "stereo", "s16"
    stream.layout = layout
    res = av.AudioResampler(format=sample_fmt, layout=layout, rate=None)
    for frame in inp.decode(audio=0):
        for rf in res.resample(frame):
            rf.pts = None
            for p in stream.encode(rf):
                out.mux(p)
    for p in stream.encode(None):
        out.mux(p)
    out.close()
    inp.close()


def reference(flac):
    """Decode with libFLAC via PyAV, duplicated to stereo the same way we do."""
    inp = av.open(flac)
    st = inp.streams.audio[0]
    rate = st.rate
    layout = st.layout.name
    nch = 1 if "mono" in layout else 2
    res = av.AudioResampler(format="s16", layout=layout, rate=rate)
    mono = bytearray()
    for frame in inp.decode(audio=0):
        for rf in res.resample(frame):
            mono += bytes(rf.planes[0])[:rf.samples * 2 * nch]
    if nch == 1:
        stereo = bytearray()
        for i in range(0, len(mono), 2):
            stereo += mono[i:i + 2] * 2
        return rate, bytes(stereo)
    return rate, bytes(mono)


def compare(got, expect, label):
    if len(got) != len(expect):
        print("%-16s LENGTH %d vs %d bytes" % (label, len(got), len(expect)))
        return False
    if got == expect:
        print("%-16s OK  bit exact, %d frames" % (label, len(got) // 4))
        return True
    worst = 0
    bad = 0
    a = struct.unpack("<%dh" % (len(got) // 2), got)
    b = struct.unpack("<%dh" % (len(expect) // 2), expect)
    for x, y in zip(a, b):
        d = abs(x - y)
        if d:
            bad += 1
            worst = max(worst, d)
    print("%-16s DIFF %d samples, max %d LSB" % (label, bad, worst))
    return worst <= 1


def main():
    os.makedirs(TMP, exist_ok=True)
    exe = build_harness()
    src = os.path.join(ROOT, "media", "03-morning-light.wav")
    rate, ch, bits, src_pcm = read_wav(src)

    cases = []

    f1 = os.path.join(TMP, "stereo16.flac")
    encode_flac(src, f1, "s16")
    cases.append(("16bit stereo", f1))

    mono_pcm = bytearray()
    for i in range(0, len(src_pcm), 4):
        mono_pcm += src_pcm[i:i + 2]
    mono_wav = os.path.join(TMP, "mono16.wav")
    hdr = b"RIFF" + struct.pack("<I", 36 + len(mono_pcm)) + b"WAVEfmt " + \
        struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16) + \
        b"data" + struct.pack("<I", len(mono_pcm))
    open(mono_wav, "wb").write(hdr + mono_pcm)
    f2 = os.path.join(TMP, "mono16.flac")
    encode_flac(mono_wav, f2, "mono")
    cases.append(("16bit mono", f2))

    pcm24 = bytearray()
    for i in range(0, len(src_pcm), 2):
        s = struct.unpack("<h", src_pcm[i:i + 2])[0]
        v = s << 8
        pcm24 += bytes((v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF))
    wav24 = os.path.join(TMP, "stereo24.wav")
    write_wav24(wav24, rate, pcm24)
    f3 = os.path.join(TMP, "stereo24.flac")
    encode_flac(wav24, f3, "s32")
    cases.append(("24bit stereo", f3))

    ok = True
    for label, flac in cases:
        ref_rate, ref_pcm = reference(flac)
        out_wav = os.path.join(TMP, label.replace(" ", "_") + ".wav")
        rc = subprocess.call([exe, flac, out_wav])
        if rc != 0:
            print("%-16s decoder exited %d" % (label, rc))
            ok = False
            continue
        got = wav_pcm(out_wav)
        ok = compare(got, ref_pcm, label) and ok
        if label == "16bit stereo":
            seek_ms = 2000
            seek_wav = os.path.join(TMP, "seek.wav")
            rc = subprocess.call([exe, flac, seek_wav, str(seek_ms)])
            if rc != 0:
                print("%-16s seek exited %d" % ("seek 2000ms", rc))
                ok = False
            else:
                skip = (ref_rate * seek_ms // 1000) * 4
                ok = compare(wav_pcm(seek_wav), ref_pcm[skip:], "seek 2000ms") and ok
    print("\n%s" % ("ALL FLAC TESTS PASSED" if ok else "FLAC TESTS FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
