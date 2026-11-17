# -*- coding: utf-8 -*-
"""Inspect the produced PE for Windows 95 compatibility."""
import struct
import sys


def main(path):
    d = open(path, "rb").read()
    if d[:2] != b"MZ":
        print("not a PE")
        return 1
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    assert d[pe:pe + 4] == b"PE\0\0"
    machine, nsec = struct.unpack_from("<HH", d, pe + 4)
    tds = struct.unpack_from("<I", d, pe + 8)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", d, opt)[0]
    (entry, basecode, basedata) = struct.unpack_from("<III", d, opt + 16)
    imgbase, salign, falign = struct.unpack_from("<III", d, opt + 28)
    osmaj, osmin = struct.unpack_from("<HH", d, opt + 40)
    imgmaj, imgmin = struct.unpack_from("<HH", d, opt + 44)
    submaj, submin = struct.unpack_from("<HH", d, opt + 48)
    dllchar = struct.unpack_from("<H", d, opt + 70)[0]
    nsub = opt + 92
    ndirs = struct.unpack_from("<I", d, opt + 92)[0]
    imp_rva, imp_sz = struct.unpack_from("<II", d, opt + 96 + 8)

    print("machine           0x%04x %s" % (machine, "i386" if machine == 0x14C else "?"))
    print("magic             0x%04x %s" % (magic, "PE32" if magic == 0x10B else "?"))
    print("entry RVA         0x%08x" % entry)
    print("image base        0x%08x" % imgbase)
    print("section align     %d / file align %d" % (salign, falign))
    print("OS version        %d.%d   (needs <= 4.0 for Win95)" % (osmaj, osmin))
    print("subsystem version %d.%d   (needs <= 4.0 for Win95)" % (submaj, submin))
    print("image version     %d.%d" % (imgmaj, imgmin))
    print("DllCharacteristics 0x%04x" % dllchar)

    # sections for RVA->offset
    sects = []
    so = opt + struct.unpack_from("<H", d, pe + 20)[0]
    for i in range(nsec):
        name = d[so + i * 40:so + i * 40 + 8].rstrip(b"\0").decode("latin1")
        vsz, va, rsz, ro = struct.unpack_from("<IIII", d, so + i * 40 + 8)
        sects.append((name, va, vsz, ro, rsz))

    def rva2off(rva):
        for name, va, vsz, ro, rsz in sects:
            if va <= rva < va + max(vsz, rsz):
                return ro + (rva - va)
        return None

    print("\nsections:")
    for name, va, vsz, ro, rsz in sects:
        print("  %-8s va=0x%08x vsz=%7d raw=%7d" % (name.encode("ascii", "replace").decode(), va, vsz, rsz))

    # imports
    off = rva2off(imp_rva)
    print("\nimports:")
    dlls = []
    i = 0
    while off:
        desc = d[off + i * 20: off + i * 20 + 20]
        if len(desc) < 20:
            break
        oft, ts, fc, name_rva, ft = struct.unpack("<IIIII", desc)
        if name_rva == 0:
            break
        no = rva2off(name_rva)
        dll = d[no:d.index(b"\0", no)].decode("latin1")
        # walk thunks
        funcs = []
        t = rva2off(oft or ft)
        while t:
            v = struct.unpack_from("<I", d, t)[0]
            if v == 0:
                break
            if v & 0x80000000:
                funcs.append("#%d" % (v & 0xFFFF))
            else:
                fo = rva2off(v & 0x7FFFFFFF)
                funcs.append(d[fo + 2:d.index(b"\0", fo + 2)].decode("latin1"))
            t += 4
        dlls.append(dll)
        print("  %-16s %d functions" % (dll, len(funcs)))
        print("    " + ", ".join(funcs))
        i += 1

    print("\nresource dir present:", ndirs > 0)
    bad = [x for x in dlls if x.lower() not in
           ("kernel32.dll", "user32.dll", "gdi32.dll", "winmm.dll",
            "comdlg32.dll", "shell32.dll", "advapi32.dll", "ole32.dll")]
    if bad:
        print("!! non-Win95 DLLs:", bad)
        return 2
    if submaj > 4 or osmaj > 4:
        print("!! subsystem/OS version too new for Windows 95")
        return 3
    print("\nRESULT: import set and headers are Windows 95 compatible")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/Shiyin.exe"))


