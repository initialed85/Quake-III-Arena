import sys, struct, collections

path = sys.argv[1]
raw = open(path, 'rb').read()
typ = raw[2]
w, h = struct.unpack('<HH', raw[12:16])
bpp = raw[16]
step = bpp // 8
n = w * h
px = raw[18:18 + n * step]

if typ == 10:
    out = bytearray()
    i = 0
    while len(out) < n * step:
        b = px[i]; i += 1
        if b & 0x80:
            c = px[i:i + step]; i += step
            out += c * (b & 0x7F)
        else:
            out += px[i:i + b * step]; i += b * step
    px = bytes(out)

white = black = 0
rs = gs = bs = 0
cnt = collections.Counter()
for i in range(0, len(px) - step + 1, step):
    b, g, r = px[i], px[i + 1], px[i + 2]
    cnt[(r, g, b)] += 1
    rs += r; gs += g; bs += b
    if r > 250 and g > 250 and b > 250:
        white += 1
    if r < 5 and g < 5 and b < 5:
        black += 1

print(f"{w}x{h} bpp={bpp} typ={typ} pixels={n}")
print(f"mean RGB: {rs/n:.1f} {gs/n:.1f} {bs/n:.1f}")
print(f"pure white: {white/n*100:.1f}%   pure black: {black/n*100:.1f}%")
print(f"distinct colours: {len(cnt)}")
print("top 8:", cnt.most_common(8))
