#!/usr/bin/env python3
"""
Multiplayer loopback check: start a dedicated server, connect a client over UDP,
assert the server accepts the client and the client renders the networked map.

Exit code is 0 on pass, 1 on failure. Run from the repo root:
    python3 tools/mptest.py
"""

import os, pty, time, select, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build-native", "Quake3")
PORT = int(os.environ.get("NET_PORT", "27960"))
SHOTS = os.path.expanduser("~/Library/Application Support/Quake3/baseq3/screenshots")
if not os.path.isdir(SHOTS):
    SHOTS = os.path.join(ROOT, "baseq3", "screenshots")


def drain(fd, seconds):
    out = b""
    t0 = time.time()
    while time.time() - t0 < seconds:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                out += os.read(fd, 4096)
            except OSError:
                return out
    return out


def spawn(args):
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(ROOT)
        os.execv(args[0], args)
    return pid, fd


def main():
    if not os.path.isfile(EXE):
        print("FAIL: %s doesn't exist; run build-native.sh" % EXE)
        return 1

    for f in os.listdir(SHOTS) if os.path.isdir(SHOTS) else []:
        if f.endswith(".tga"):
            os.remove(os.path.join(SHOTS, f))

    server_args = [EXE, "+set", "dedicated", "1", "+set", "net_port", str(PORT),
                   "+set", "sv_maxclients", "4", "+devmap", "q3dm17",
                   "+addbot", "sarge", "1", "free", "0"]
    spid, sfd = spawn(server_args)
    sout = drain(sfd, 8)

    if "Opening IP socket" not in sout.decode(errors="replace"):
        print("FAIL: server did not open an IP socket")
        os.kill(spid, 15)
        return 1

    lsof = subprocess.run(["lsof", "-nP", "-iUDP"], capture_output=True, text=True)
    if ":%d" % PORT not in lsof.stdout:
        print("FAIL: nothing listening on UDP %d" % PORT)
        os.kill(spid, 15)
        return 1

    client_args = [EXE, "+set", "r_fullscreen", "0", "+set", "r_mode", "-1",
                   "+set", "r_customwidth", "1280", "+set", "r_customheight", "720",
                   "+connect", "localhost:%d" % PORT]
    cpid, cfd = spawn(client_args)
    cout = drain(cfd, 12)
    os.write(cfd, b"screenshot\n")
    cout += drain(cfd, 2)
    os.write(cfd, b"quit\n")
    t1 = time.time()
    cexit = None
    while time.time() - t1 < 8:
        r, _, _ = select.select([cfd], [], [], 0.2)
        if r:
            try:
                cout += os.read(cfd, 4096)
            except OSError:
                break
        p, st = os.waitpid(cpid, os.WNOHANG)
        if p:
            cexit = st
            break
    if cexit is None:
        print("FAIL: client did not exit")
        drain(cfd, 3)
        os.kill(cpid, 15)
        drain(cfd, 3)
        os.waitpid(cpid, 0)
        drain(sfd, 3)
        os.kill(spid, 15)
        drain(sfd, 3)
        os.waitpid(spid, 0)
        return 1

    os.kill(spid, 15)
    # The server blocks writing to the pty. If the master is not drained it never
    # reaches the signal, so drain before reaping.
    sout += drain(sfd, 3)
    os.waitpid(spid, 0)

    stext = sout.decode(errors="replace")
    ctext = cout.decode(errors="replace")

    fails = []
    if "ClientConnect: 1" not in stext:
        fails.append("server never accepted the client (no ClientConnect)")
    if "ClientBegin: 1" not in stext:
        fails.append("server never began the client (no ClientBegin)")
    if "Wrote screenshots" not in ctext:
        fails.append("client wrote no screenshot")
    if "Sys_Error" in ctext:
        fails.append("client hit Sys_Error")
    if cexit != 0:
        fails.append("client exit status %s" % cexit)

    shots = sorted([f for f in os.listdir(SHOTS) if f.endswith(".tga")])
    if not shots:
        fails.append("no screenshot on disk")
    else:
        stats = subprocess.run(["python3", os.path.join(ROOT, "tools", "tga-stats.py"),
                                os.path.join(SHOTS, shots[-1])],
                               capture_output=True, text=True)
        if "pure white" not in stats.stdout:
            fails.append("tga-stats failed on %s" % shots[-1])
        else:
            white = float(stats.stdout.split("pure white:")[1].split("%")[0])
            if white > 5.0:
                fails.append("screenshot is %s%% white - textures not rendering" % white)

    for f in fails:
        print("FAIL:", f)
    if fails:
        return 1

    print("ok: dedicated server on UDP %d accepted the client, client rendered the map" % PORT)
    for line in [l for l in stext.split("\n") if "ClientConnect" in l or "ClientBegin" in l]:
        print("   ", line.strip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
