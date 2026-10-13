#!/usr/bin/env python3
"""
Substantiate where the single browser thread spends its time.

Launches headless Chrome on a URL, finds the renderer process that owns the page,
and samples cumulative CPU seconds for every Chrome process once a second. The delta
of CPU seconds over wall seconds is the fraction of one core the process burns:
delta 1.0 means one core is fully busy. A page that is blocked in a sync call shows
delta 0.

Usage: tools/cpu-probe.py [url] [seconds]
"""

import subprocess
import sys
import time
import urllib.parse

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
DEFAULT_URL = "http://127.0.0.1:8081/Quake3.html"


def ps_rows():
    """pid, cumulative cpu seconds, %cpu, command tail for every chrome process."""
    out = subprocess.run(
        ["ps", "-o", "pid=,time=,pcpu=,comm=", "-ax"],
        capture_output=True, text=True, check=True).stdout
    rows = {}
    for line in out.splitlines():
        parts = line.split(None, 3)
        if len(parts) < 4:
            continue
        pid, cputime, pcpu, comm = parts
        if "Chrome" not in comm and "chrome" not in comm:
            continue
        # `time` is mm:ss.hh on macOS
        mm, ss = cputime.split(":")
        rows[int(pid)] = (float(mm) * 60 + float(ss), float(pcpu), comm)
    return rows


def main():
    url = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_URL
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0

    # --no-sandbox + swiftshader so WebGL works headless; the page is the initial
    # target, so no CDP is needed and the page is not disturbed by sampling.
    proc = subprocess.Popen([CHROME, "--headless=new", "--disable-gpu",
                             "--enable-unsafe-swiftshader", "--no-sandbox",
                             "--window-size=1024,768", url],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    time.sleep(4)
    prev = ps_rows()
    samples = []
    for i in range(int(seconds)):
        time.sleep(1)
        cur = ps_rows()
        lines = []
        for pid, (cpu, pcpu, comm) in cur.items():
            if pid not in prev:
                continue
            delta = cpu - prev[pid][0]
            if delta > 0.02 or pcpu > 1:
                kind = comm.split("(", 1)[-1].rstrip(")") if "(" in comm else comm
                lines.append("pid %-7s %-22s cpu=%6.2fs delta=%5.2f pcpu=%5.1f%%" %
                             (pid, kind, cpu, delta, pcpu))
        samples.append((i, lines))
        prev = cur

    proc.terminate()

    for i, lines in samples:
        print("t=%2i  %s" % (i, " | ".join(lines) if lines else "idle"))

    # The renderer is the process whose CPU time grows steadily. Report the
    # mean delta over the run for each pid seen at least half the samples.
    tot = {}
    seen = {}
    for i, lines in samples:
        for l in lines:
            pid = int(l.split()[1])
            d = float(l.split("delta=")[1].split()[0])
            tot[pid] = tot.get(pid, 0) + d
            seen[pid] = seen.get(pid, 0) + 1
    print("=== mean core fraction over %d samples ===" % len(samples))
    for pid, total in sorted(tot.items(), key=lambda kv: -kv[1]):
        print("pid %s: total cpu %.2fs over %d samples, mean %.2f cores" %
              (pid, total, seen[pid], total / len(samples)))


if __name__ == "__main__":
    main()
