#!/usr/bin/env python3
"""
Load the WASM build in headless Chrome over CDP, capture the page console, and
screenshot the canvas. WebGL runs on SwiftShader (--enable-unsafe-swiftshader),
so this is a real renderer check with no visible window.

  python3 tools/browser-test.py [url] [seconds] [out.png] [width,height]

The page console is what the engine prints through Emscripten's console.log, so
it carries the same Q3 log lines the native pty harness produces.

The page is created through /json/new?<url> rather than Page.navigate: navigating
an existing headless target resets the CDP websocket, and the console stream is
lost. Screenshots are sampled repeatedly so a frame rendered after load is
captured.
"""

import base64
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time
import urllib.parse
import urllib.request

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
PORT = 9333
DEFAULT_URL = "http://127.0.0.1:8081/Quake3.html"
# The in-engine pixel assertion is PIXELPROBE in the console (r_pixelprobe N).
# Emscripten creates the WebGL context inside the wasm module, so the page has no
# handle on it; the probe here only confirms the canvas is sized as the engine set.
EVAL = ("(() => { const c = document.getElementById('canvas'); const r = c.getBoundingClientRect();"
        "return JSON.stringify([c.width, c.height, Math.round(r.left), Math.round(r.top), Math.round(r.width), Math.round(r.height)]); })()")
# The page buffers its own console output (see shell.html): headless Chrome delivers no
# consoleAPICalled / Log.entryAdded to a page target, so drain that buffer here.
LOG_DRAIN_JS = "(window.__q3log ? window.__q3log.splice(0) : [])"

# The wasm filesystem holds the engine's own TGA screenshots. Emscripten exposes the FS on
# Module when built with -sEXPORTED_RUNTIME_METHODS=FS. Read the newest TGA back out as base64.
FS_SHOT_JS = ("(function(){ var fs = (typeof Module!=='undefined'&&Module.FS)?Module.FS:"
              "(typeof FS!=='undefined'?FS:null); if(!fs){ return 'NO FS'; }"
              "var dirs=['/baseq3/screenshots','/home/web_user/.quake3/baseq3/screenshots',"
              "'./baseq3/screenshots','baseq3/screenshots'];"
              "for(var i=0;i<dirs.length;i++){ var f; try{ f=fs.readdir(dirs[i]); }catch(e){ continue; }"
              "f=f.filter(function(n){ return n.slice(-4)==='.tga'; }); if(!f.length) continue; f.sort();"
              "var b=fs.readFile(dirs[i]+'/'+f[f.length-1]); var s='',CH=8192;"
              "for(var j=0;j<b.length;j+=CH){ s+=String.fromCharCode.apply(null,b.subarray(j,j+CH)); }"
              "return f[f.length-1]+'|'+btoa(s); } return 'NONE'; })()")
DEBUG = os.environ.get("BROWSER_DEBUG") == "1"


def ws_connect(path):
    """Open a raw WebSocket to a CDP target."""
    key = base64.b64encode(os.urandom(16)).decode()
    s = socket.create_connection(("127.0.0.1", PORT), timeout=30)
    s.sendall(("GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\n"
               "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
               "Sec-WebSocket-Version: 13\r\nOrigin: http://127.0.0.1:%d\r\n\r\n"
               % (path, PORT, key, PORT)).encode())
    resp = b""
    while b"\r\n\r\n" not in resp:
        chunk = s.recv(4096)
        if not chunk:
            raise RuntimeError("handshake closed: %r" % resp)
        resp += chunk
    status = resp.split(b"\r\n")[0]
    if b"101" not in status:
        raise RuntimeError("websocket upgrade failed: %r" % status)
    if DEBUG:
        print("handshake", status, "path", path)
    return s


def ws_send(s, text):
    payload = text.encode()
    mask = b"\xa7\x23\x4b\x2f"
    n = len(payload)
    if n < 126:
        header = bytes([0x80 | n])
    elif n < 65536:
        # the mask bit must stay set: 0xFE is "masked, extended length 126",
        # 0x7E is unmasked and Chrome resets the connection on it
        header = b"\xfe" + struct.pack(">H", n)
    else:
        header = b"\xff" + struct.pack(">Q", n)
    s.sendall(b"\x81" + header + mask
              + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))


def ws_read(s, deadline=None):
    """Read one frame. Returns the decoded payload, or None when the socket closes."""
    def recv(n):
        buf = b""
        while len(buf) < n:
            try:
                chunk = s.recv(n - len(buf))
            except socket.timeout:
                # a large frame (a screenshot is several KB) can time out part
                # way through; keep reading rather than declaring the socket closed
                if deadline is not None and time.time() > deadline:
                    return b"partial"
                continue
            if not chunk:
                return None
            buf += chunk
        return buf

    hdr = recv(2)
    if hdr is None:
        return None
    length = hdr[1] & 0x7F
    if DEBUG:
        print("frame header", hdr.hex(), "len", length)
    if length == 126:
        length = struct.unpack(">H", recv(2))[0]
    elif length == 127:
        length = struct.unpack(">Q", recv(8))[0]
    if hdr[1] & 0x80:
        mask = recv(4)
        data = recv(length)
        if data is None:
            return None
        data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    else:
        data = recv(length)
        if data is None:
            return None
    return data.decode("utf-8", "replace")


def open_page(url):
    """Create the page target directly on the game URL. Returns the WS path."""
    # DevTools must be listening before a target is created, otherwise the
    # websocket to the new page is closed straight away.
    for _ in range(40):
        try:
            json.loads(urllib.request.urlopen(
                "http://127.0.0.1:%d/json/version" % PORT, timeout=2).read())
            break
        except Exception:
            time.sleep(0.25)
    for _ in range(40):
        try:
            req = urllib.request.Request(
                "http://127.0.0.1:%d/json/new?" % PORT + urllib.parse.quote(url, safe=""),
                method="PUT")
            target = json.loads(urllib.request.urlopen(req, timeout=2).read())
            return "/" + target["webSocketDebuggerUrl"].split("://", 1)[1].split("/", 1)[1]
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("could not open a page on port %d" % PORT)


def ensure_server(url):
    """Serve build-wasm if the URL's port is not answering. Returns the Popen or None.

    The engine only runs over http:// (file:// has no XHR for .wasm/.data), and a page
    created before the server answers lands on chrome-error://chromewebdata/, where every
    evaluate returns nothing and every screenshot is a blank sheet.
    """
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme not in ("http", "https"):
        print("not an http url: %s" % url)
        sys.exit(1)
    port = parsed.port or 80
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    probe = "%s://%s:%d/" % (parsed.scheme, parsed.hostname, port)

    def alive():
        try:
            return 200 <= urllib.request.urlopen(probe, timeout=1).status < 500
        except Exception:
            return False

    if alive():
        return None
    proc = subprocess.Popen([sys.executable, "-m", "http.server", str(port),
                             "--directory", os.path.join(root, "build-wasm")],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(40):
        if alive():
            print("serving %s on %d" % (os.path.join(root, "build-wasm"), port))
            return proc
        time.sleep(0.25)
    print("no server answering on port %d" % port)
    sys.exit(1)


def main():
    url = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_URL
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 12.0
    out = sys.argv[3] if len(sys.argv) > 3 else "/tmp/wasm-shot.png"
    size = sys.argv[4] if len(sys.argv) > 4 else "1024,768"

    server = ensure_server(url)
    chrome_log = open("/tmp/chrome-cdp.log", "wb")
    # GPU=1 runs on the real GPU path (ANGLE/Metal on macOS) instead of SwiftShader,
    # so a frame that renders here and not there is a GL-emulation bug, not game logic.
    if os.environ.get("GPU"):
        chrome_flags = ["--headless=new", "--use-gl=angle", "--use-angle=metal",
                        "--enable-gpu-rasterization", "--ignore-gpu-blocklist"]
    else:
        chrome_flags = ["--headless=new", "--disable-gpu", "--enable-unsafe-swiftshader"]
    chrome = subprocess.Popen(
        [CHROME] + chrome_flags +
        ["--no-sandbox", "--remote-debugging-port=%d" % PORT,
         # Chrome 111+ rejects CDP websocket upgrades from a client with no
         # registered origin, so allow them explicitly.
         "--remote-allow-origins=*",
         "--window-size=%s" % size, "about:blank"],
        stdout=chrome_log, stderr=subprocess.STDOUT)

    # DevTools needs a moment to be listening, and the page target is created
    # against a live browser endpoint.
    time.sleep(2.5)

    console = []
    t_start = time.time()
    shots = []
    probes = []
    seq = 0
    shots_sent = 0
    shot_answers = 0
    loops = 0
    pending = {}

    def cmd(method, params=None):
        nonlocal seq
        seq += 1
        ws_send(ws, json.dumps({"id": seq, "method": method, "params": params or {}}))
        if DEBUG:
            print("sent", seq, method)
        pending[seq] = method
        return seq

    try:
        ws = ws_connect(open_page(url))
        print("connected:", ws.getsockname(), ws.gettimeout())
        cmd("Runtime.enable")
        cmd("Page.enable")
        cmd("Log.enable")
        if DEBUG:
            cmd("Runtime.evaluate", {"expression": "JSON.stringify({url:location.href,"
                "hook:typeof window.__q3log, canvas:!!document.getElementById('canvas'),"
                "scripts:document.scripts.length, ready:document.readyState})",
                "returnByValue": True})

        # Sample at a fixed rate with one in-flight request per kind. Re-issuing
        # captureScreenshot / Runtime.evaluate from inside their own response
        # handler floods CDP and starves the page: a 45s run produced 267k
        # messages and the engine never got past window creation.
        deadline = time.time() + seconds
        # The wasm main loop saturates the renderer thread at ~1 fps under SwiftShader, and
        # CDP replies are only serviced when that loop yields. A 2.5 s cadence starves the
        # screenshot replies completely; widen it when the page is the bottleneck.
        interval = float(os.environ.get("SHOT_INTERVAL", "2.5"))
        next_shot = time.time() + 1.0
        next_probe = time.time() + 0.5
        shot_inflight = None
        probe_inflight = None
        log_inflight = None
        next_log = time.time() + 1.0
        ws.settimeout(1)
        while time.time() < deadline:
            now = time.time()
            loops += 1
            if shot_inflight is None and now >= next_shot:
                shot_inflight = cmd("Page.captureScreenshot", {"format": "png"})
                shots_sent += 1
                next_shot = now + interval
            if probe_inflight is None and now >= next_probe:
                probe_inflight = cmd("Runtime.evaluate", {"expression": EVAL, "returnByValue": True})
                next_probe = now + interval
            if log_inflight is None and now >= next_log:
                log_inflight = cmd("Runtime.evaluate",
                                   {"expression": LOG_DRAIN_JS, "returnByValue": True})
                next_log = now + 2.0

            try:
                raw = ws_read(ws, min(deadline, next_shot, next_probe))
            except (socket.timeout, ConnectionResetError, OSError):
                continue
            if raw is None:
                print("CDP connection closed")
                break
            if raw == "partial":
                continue
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                continue

            if msg.get("id") in pending:
                method = pending.pop(msg["id"])
                result = msg.get("result", {})
                if method == "Page.captureScreenshot":
                    shot_inflight = None
                    shot_answers += 1
                    if DEBUG:
                        print("shot result keys", sorted(result.keys()), "len", len(result.get("data", "")))
                    if "data" in result:
                        shots.append(base64.b64decode(result["data"]))
                elif method == "Runtime.evaluate":
                    if log_inflight is not None and msg["id"] == log_inflight:
                        log_inflight = None
                        val = result.get("result", {}).get("value")
                        if DEBUG:
                            print("log drain ->", repr(val)[:120])
                        if isinstance(val, list):
                            console.extend(val)
                    else:
                        probe_inflight = None
                        v = result.get("result", {}).get("value", "?")
                        if v not in probes:
                            probes.append(v)
                if "error" in msg:
                    print("%s failed: %s" % (method, msg["error"].get("message")))

            m = msg.get("method")
            stamp = "[%5.1fs] " % (time.time() - t_start)
            if m == "Runtime.consoleAPICalled":
                parts = [a.get("value", a.get("description", ""))
                         for a in msg["params"].get("args", [])]
                console.append(stamp + " ".join(str(p) for p in parts))
            elif m == "Log.entryAdded":
                console.append(stamp + msg["params"]["entry"].get("text", ""))

        # The engine's own screenshot path (cl_avidemo, "screenshot") writes TGA files into
        # the wasm filesystem. CDP captureScreenshot is starved by a ~1 fps main loop, so pull
        # the newest TGA out of the module filesystem and assert on those pixels instead.
        if os.environ.get("SHOTFILE") == "1":
            shot_id = cmd("Runtime.evaluate", {"expression": FS_SHOT_JS, "returnByValue": True})
            fs_shot = None
            fs_deadline = time.time() + 30
            while fs_shot is None and time.time() < fs_deadline:
                try:
                    raw = ws_read(ws, fs_deadline)
                except (socket.timeout, ConnectionResetError, OSError):
                    continue
                if raw is None or raw == "partial":
                    continue
                try:
                    msg = json.loads(raw)
                except json.JSONDecodeError:
                    continue
                if msg.get("id") == shot_id:
                    fs_shot = msg.get("result", {}).get("result", {}).get("value")
                elif msg.get("method") == "Runtime.consoleAPICalled":
                    parts = [a.get("value", a.get("description", ""))
                             for a in msg["params"].get("args", [])]
                    console.append(" ".join(str(p) for p in parts))
                elif msg.get("method") == "Log.entryAdded":
                    console.append(msg["params"]["entry"].get("text", ""))
            tgapath = out[:-4] + ".tga" if out.endswith(".png") else out + ".tga"
            if fs_shot and "|" in fs_shot:
                name, b64 = fs_shot.split("|", 1)
                with open(tgapath, "wb") as f:
                    f.write(base64.b64decode(b64))
                print("fs screenshot: %s -> %s (%d bytes)"
                      % (name, tgapath, os.path.getsize(tgapath)))
            else:
                print("fs screenshot: none (%s)" % fs_shot)

        ws.close()
    finally:
        chrome.terminate()
        if server:
            server.terminate()

    print("=== capture: %d screenshot requests, %d answered, %d frames kept, %d loop iterations ===" % (shots_sent, shot_answers, len(shots), loops))
    print("=== console (%d lines) ===" % len(console))
    engine = [l for l in console if "PIXELPROBE" not in l]
    pixel = [l for l in console if "PIXELPROBE" in l]
    for line in engine:
        print(line)
    print("=== PIXELPROBE samples (%d, showing first 3 and last 3) ===" % len(pixel))
    for l in pixel[:3]:
        print(l)
    for l in pixel[-3:]:
        print(l)

    print("=== canvas probe values (%d unique) ===" % len(probes))
    for v in probes:
        print(v)

    # The pixel verdict. A frame with real geometry has thousands of distinct colours;
    # the RB_Clear gray and a black framebuffer both report distinct == 1, so this is
    # the check that says the game is drawing, not that a window opened.
    best_probe = None
    for l in pixel:
        m = re.search(r"frame (\d+).*distinct (\d+)", l)
        if m and (best_probe is None or int(m.group(1)) > best_probe[0]):
            best_probe = (int(m.group(1)), int(m.group(2)), l)
    if best_probe is None:
        print("FAIL: no PIXELPROBE lines; run with +set r_pixelprobe N")
    elif best_probe[1] > 200:
        print("ok: geometry rendered (frame %d, %d distinct colours)" % (best_probe[0], best_probe[1]))
    else:
        print("FAIL: frame %d has %d distinct colours (clear colour only, no geometry)" % (best_probe[0], best_probe[1]))
        print("    " + best_probe[2])

    # The first screenshot is the blank page, the last is the game. Pick the
    # largest PNG, which is the frame with the most detail.
    if shots:
        best = max(shots, key=len)
        with open(out, "wb") as f:
            f.write(best)
        w, h = struct.unpack(">II", best[16:24])
        # A ~1 fps engine composites slowly, so the most detailed frame and the final
        # frame differ. Keep the last one too: it is the most advanced game state.
        last = shots[-1]
        lastpath = out[:-4] + "-last.png" if out.endswith(".png") else out + ".last.png"
        with open(lastpath, "wb") as f:
            f.write(last)
        print("screenshots: %d samples, best %d bytes -> %s (%d x %d), last %d bytes -> %s"
              % (len(shots), len(best), out, w, h, len(last), lastpath))
        distinct = len(set(shots))
        print("distinct frames: %d" % distinct)
    else:
        print("no screenshot captured")

    joined = "\n".join(console)
    for marker in ("----- Client Initialization -----", "GL_VENDOR", "GL_EXTENSIONS",
                   "----- Server Start -----", "InitGame:", "loaded maps/",
                   "AAS initialized", "ClientBegin", "Executing command line"):
        print("%-36s %s" % (marker, "found" if marker in joined else "MISSING"))

    print("=== chrome log tail ===")
    os.system("tail -4 /tmp/chrome-cdp.log")

if __name__ == "__main__":
    main()
