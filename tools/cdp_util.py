"""Minimal Chrome DevTools Protocol client over a raw websocket (no deps).

Chrome must be running with --remote-debugging-port=9222. open_page() creates a page
target for a URL (PUT /json/new is required by current Chrome) and returns the socket
already upgraded to the page's devtools websocket.
"""
import base64
import json
import socket
import time
import urllib.parse
import urllib.request

HOST = "127.0.0.1"
PORT = 9222


def _ws_connect(path):
    s = socket.create_connection((HOST, PORT))
    key = base64.b64encode(b"0123456789abcdef").decode()
    req = ("GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
           "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (path, HOST, PORT, key))
    s.sendall(req.encode())
    reply = s.recv(4096)
    assert b"101" in reply, reply[:120]
    return s


def open_page(url):
    req = urllib.request.Request("http://%s:%d/json/new?%s" % (HOST, PORT, urllib.parse.quote(url)),
                                method="PUT")
    info = json.loads(urllib.request.urlopen(req, timeout=5).read())
    path = "/" + info["webSocketDebuggerUrl"].split("://", 1)[1].split("/", 1)[1]
    return _ws_connect(path)


def send(s, payload):
    data = json.dumps(payload).encode()
    n = len(data)
    head = bytes([0x81])
    if n < 126:
        head += bytes([0x80 | n])
    elif n < 65536:
        head += bytes([0x80 | 126]) + n.to_bytes(2, "big")
    else:
        head += bytes([0x80 | 127]) + n.to_bytes(8, "big")
    mask = bytes([0x01, 0x02, 0x03, 0x04])
    s.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))
    return


def frames(s, seconds):
    """Yield decoded CDP messages for the next `seconds` seconds."""
    s.settimeout(0.5)
    buf = b""
    out = []
    t0 = time.time()
    while time.time() - t0 < seconds:
        try:
            buf += s.recv(65536)
        except socket.timeout:
            continue
        except (ConnectionResetError, OSError):
            break
        while len(buf) >= 2:
            length = buf[1] & 0x7F
            offset = 2
            if length == 126:
                if len(buf) < 4:
                    break
                length = int.from_bytes(buf[2:4], "big")
                offset = 4
            elif length == 127:
                if len(buf) < 10:
                    break
                length = int.from_bytes(buf[2:10], "big")
                offset = 10
            if len(buf) < offset + length:
                break
            try:
                out.append(json.loads(buf[offset:offset + length].decode("utf-8", "replace")))
            except json.JSONDecodeError:
                pass
            buf = buf[offset + length:]
    return out
