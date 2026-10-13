#!/usr/bin/env python3
"""Pixel statistics for a PNG, no PIL. Prints size, mean RGB, pure-white/pure-black
percentages and distinct colour count. A frame with real geometry scores thousands of
distinct colours; a framebuffer that only clears scores one.

Usage: png-stats.py file.png [region x,y,w,h]
"""
import sys, zlib, struct

def chunks(data):
    i = 8
    out = {}
    while i < len(data):
        n = struct.unpack(">I", data[i:i+4])[0]
        typ = data[i+4:i+8]
        body = data[i+8:i+8+n]
        if typ == b"IDAT":
            out[typ] = out.get(typ, b"") + body
        else:
            out[typ] = body
        i += 12 + n
    return out

def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc: return a
    if pb <= pc: return b
    return c

def decode(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("%s is not a PNG" % path)
    c = chunks(data)
    w, h, depth, color = struct.unpack(">IIBB", c[b"IHDR"][:10])
    if depth != 8:
        raise SystemExit("bit depth %d unsupported" % depth)
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    bpp = channels
    stride = w * bpp
    raw = zlib.decompress(c[b"IDAT"])
    rows = []
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        f = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos+stride]); pos += stride
        if f == 0:
            pass
        elif f == 1:
            for x in range(bpp, stride): line[x] = (line[x] + line[x-bpp]) & 255
        elif f == 2:
            for x in range(stride): line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                line[x] = (line[x] + (a + prev[x]) // 2) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                c2 = prev[x-bpp] if x >= bpp else 0
                line[x] = (line[x] + paeth(a, prev[x], c2)) & 255
        else:
            raise SystemExit("bad filter %d on row %d" % (f, y))
        rows.append(line)
        prev = line
    return w, h, bpp, rows

def stats(path, region=None):
    w, h, bpp, rows = decode(path)
    if region:
        rx, ry, rw, rh = region
        rx, ry = max(0, rx), max(0, ry)
        rw, rh = min(rw, w - rx), min(rh, h - ry)
    else:
        rx, ry, rw, rh = 0, 0, w, h
    total = rw * rh
    sr = sg = sb = 0
    white = black = 0
    seen = set()
    for y in range(ry, ry + rh):
        row = rows[y]
        for x in range(rx, rx + rw):
            i = x * bpp
            r, g, b = row[i], row[i+1], row[i+2] if bpp >= 3 else (row[i], row[i], row[i])
            sr += r; sg += g; sb += b
            seen.add((r, g, b))
            if r == 255 and g == 255 and b == 255: white += 1
            if r == 0 and g == 0 and b == 0: black += 1
    print("size %dx%d region %d,%d %dx%d" % (w, h, rx, ry, rw, rh))
    print("mean %d,%d,%d" % (sr//total, sg//total, sb//total))
    print("distinct %d" % len(seen))
    print("white %.1f%% black %.1f%%" % (100.0*white/total, 100.0*black/total))
    return len(seen)

def encode(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    body = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xffffffff)
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", body) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    open(path, "wb").write(out)

def crop(path, out, region):
    w, h, bpp, rows = decode(path)
    rx, ry, rw, rh = region
    rx, ry = max(0, rx), max(0, ry)
    rw, rh = min(rw, w - rx), min(rh, h - ry)
    new = [bytes(row[rx*bpp:(rx+rw)*bpp]) for row in rows[ry:ry+rh]]
    encode(out, rw, rh, new)
    print("cropped %s -> %s (%d x %d)" % (path, out, rw, rh))

if __name__ == "__main__":
    reg = None
    if len(sys.argv) > 2:
        reg = [int(v) for v in sys.argv[2].split(",")]
    if len(sys.argv) > 3 and sys.argv[3] == "--crop":
        crop(sys.argv[1], sys.argv[4], reg)
        sys.exit(0)
    n = stats(sys.argv[1], reg)
    if n < 100:
        print("FAIL: %d distinct colours (no geometry)" % n)
        sys.exit(1)
    print("ok: %d distinct colours" % n)
