# -*- coding: utf-8 -*-
"""Tiny FTP server that only accepts uploads (for pulling files out of a VM).

Windows 95 ships C:\\WINDOWS\\ftp.exe, which speaks plain FTP, so the guest can
push a file to this machine without any shared folders or guest tools:

    ftp <host-ip> 2121
    anonymous
    anything
    put C:\\WINDOWS\\Desktop\\shiyin.log
    bye

Files land in build/incoming/.  Run:  python tools/ftp_recv.py
"""
import os
import socket
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, "build", "incoming")
SHARE = os.path.join(ROOT, "build")          # files the guest may download
PORT = 2121


def log(msg):
    print(time.strftime("[%H:%M:%S] ") + msg, flush=True)


def send(ctrl, text):
    ctrl.sendall((text + "\r\n").encode("latin1"))


def handle(ctrl, addr, local_ip):
    # the data connection must advertise the address the client reached us on,
    # otherwise NAT guests can open the control channel but not the data one
    local_ip = ctrl.getsockname()[0]
    log("connection from %s:%d (local %s)" % (addr[0], addr[1], local_ip))
    ctrl.sendall(b"220 shiyin receive ready\r\n")
    buf = b""
    pasv = None
    data_sock = None
    while True:
        chunk = ctrl.recv(1024)
        if not chunk:
            break
        buf += chunk
        while b"\r\n" in buf:
            line, buf = buf.split(b"\r\n", 1)
            try:
                text = line.decode("latin1")
            except Exception:
                continue
            cmd, _, arg = text.partition(" ")
            cmd = cmd.upper()
            log("<- " + text)
            if cmd == "USER":
                send(ctrl, "331 need password")
            elif cmd == "PASS":
                send(ctrl, "230 logged in")
            elif cmd == "SYST":
                send(ctrl, "215 UNIX Type: L8")
            elif cmd == "PWD" or cmd == "XPWD":
                send(ctrl, '257 "/" is current directory')
            elif cmd == "CWD" or cmd == "XCWD":
                send(ctrl, "250 ok")
            elif cmd == "TYPE":
                send(ctrl, "200 type set")
            elif cmd == "MODE":
                send(ctrl, "200 mode set")
            elif cmd == "STRU":
                send(ctrl, "200 structure set")
            elif cmd == "NOOP":
                send(ctrl, "200 ok")
            elif cmd == "FEAT":
                send(ctrl, "211-Features:")
                send(ctrl, "211 End")
            elif cmd == "PASV":
                if pasv:
                    try:
                        pasv.close()
                    except OSError:
                        pass
                pasv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                pasv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                pasv.bind(("0.0.0.0", 0))
                pasv.listen(1)
                p = pasv.getsockname()[1]
                h = local_ip.split(".")
                send(ctrl, "227 Entering Passive Mode (%s,%s,%s,%s,%d,%d)" %
                     (h[0], h[1], h[2], h[3], p >> 8, p & 0xFF))
            elif cmd == "EPSV":
                if pasv:
                    try:
                        pasv.close()
                    except OSError:
                        pass
                pasv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                pasv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                pasv.bind(("0.0.0.0", 0))
                pasv.listen(1)
                send(ctrl, "229 Entering Extended Passive Mode (|||%d|)" %
                     pasv.getsockname()[1])
            elif cmd in ("STOR", "APPE"):
                name = os.path.basename(arg.replace("\\", "/")) or "upload.bin"
                if not pasv:
                    send(ctrl, "425 use PASV first")
                    continue
                send(ctrl, "150 opening data connection")
                try:
                    pasv.settimeout(60)
                    data_sock, _ = pasv.accept()
                except OSError:
                    send(ctrl, "425 cannot open data connection")
                    continue
                os.makedirs(DEST, exist_ok=True)
                out = os.path.join(DEST, name)
                total = 0
                mode = "ab" if cmd == "APPE" else "wb"
                with open(out, mode) as f:
                    while True:
                        d = data_sock.recv(65536)
                        if not d:
                            break
                        f.write(d)
                        total += len(d)
                data_sock.close()
                data_sock = None
                pasv.close()
                pasv = None
                send(ctrl, "226 transfer complete")
                log("saved %s (%d bytes) -> %s" % (name, total, out))
            elif cmd == "RETR":
                name = os.path.basename(arg.replace("\\", "/"))
                src = os.path.join(SHARE, name)
                if not os.path.isfile(src):
                    send(ctrl, "550 no such file")
                    continue
                if not pasv:
                    send(ctrl, "425 use PASV first")
                    continue
                send(ctrl, "150 sending %s" % name)
                try:
                    pasv.settimeout(120)
                    data_sock, _ = pasv.accept()
                    with open(src, "rb") as f:
                        while True:
                            d = f.read(65536)
                            if not d:
                                break
                            data_sock.sendall(d)
                    data_sock.close()
                except OSError as e:
                    log("retr failed: %s" % e)
                data_sock = None
                pasv.close()
                pasv = None
                send(ctrl, "226 transfer complete")
                log("sent %s" % name)
            elif cmd == "SIZE":
                name = os.path.basename(arg.replace("\\", "/"))
                src = os.path.join(SHARE, name)
                if os.path.isfile(src):
                    send(ctrl, "213 %d" % os.path.getsize(src))
                else:
                    send(ctrl, "550 no such file")
            elif cmd == "MDTM":
                send(ctrl, "213 20260101000000")
            elif cmd == "REST":
                send(ctrl, "350 restart not supported")
            elif cmd == "LIST" or cmd == "NLST":
                send(ctrl, "150 listing")
                if pasv:
                    pasv.settimeout(10)
                    try:
                        data_sock, _ = pasv.accept()
                        for n in sorted(os.listdir(SHARE))[:50]:
                            p2 = os.path.join(SHARE, n)
                            if os.path.isfile(p2):
                                data_sock.sendall(("-rw-r--r-- 1 user group %d Jan 01 00:00 %s\r\n"
                                                   % (os.path.getsize(p2), n)).encode("latin1"))
                        data_sock.close()
                    except OSError:
                        pass
                    pasv.close()
                    pasv = None
                    data_sock = None
                send(ctrl, "226 done")
            elif cmd == "QUIT":
                send(ctrl, "221 bye")
                ctrl.close()
                log("session closed")
                return
            else:
                send(ctrl, "502 command not implemented")
    ctrl.close()


def main():
    os.makedirs(DEST, exist_ok=True)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", PORT))
    srv.listen(4)
    # discover the address the guest should use
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("8.8.8.8", 80))
        local_ip = probe.getsockname()[0]
    except OSError:
        local_ip = "127.0.0.1"
    probe.close()
    log("listening on port %d, uploads -> %s" % (PORT, DEST))
    log("tell the guest to run:  ftp %s %d" % (local_ip, PORT))
    while True:
        ctrl, addr = srv.accept()
        threading.Thread(target=handle, args=(ctrl, addr, local_ip), daemon=True).start()


if __name__ == "__main__":
    main()
