"""Observe the wasm page over time: buffered console length/tail plus canvas size.

Usage: python3 tools/page-watch.py [url] [seconds] [sample-every]
"""
import base64
import json
import socket
import subprocess
import sys
import time
import urllib.parse
import urllib.request

import cdp_util as cdp  # noqa: E402

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8081/Quake3.html"
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 120.0
EVERY = float(sys.argv[3]) if len(sys.argv) > 3 else 15.0

s = cdp.open_page(URL)
cdp.send(s, {"id": 1, "method": "Runtime.enable"})
t0 = time.time()
k = 0
expr = ("JSON.stringify([window.__q3log?window.__q3log.length:-1,"
        "window.__q3log?window.__q3log.slice(-3).join(' | ').slice(0,220):'', document.title])")
while time.time() - t0 < SECONDS:
    time.sleep(EVERY)
    k += 1
    cdp.send(s, {"id": 900 + k, "method": "Runtime.evaluate",
                 "params": {"expression": expr, "returnByValue": True}})
    for m in cdp.frames(s, 4):
        if m.get("id") == 900 + k:
            v = m.get("result", {}).get("result", {}).get("value")
            print("t=%3.0fs %s" % (time.time() - t0, v), flush=True)
