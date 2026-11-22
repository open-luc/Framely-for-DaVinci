# Measure the bounding box of non-black pixels in an image (PNG or anything sips can read).
# usage: python3 -I measure.py image [threshold]
import sys, zlib, struct, subprocess, os, tempfile
def readpng(p):
    d = open(p, 'rb').read(); assert d[:8] == b'\x89PNG\r\n\x1a\n'
    i = 8; idat = b''; w = h = 0
    while i < len(d):
        n = struct.unpack('>I', d[i:i+4])[0]; t = d[i+4:i+8]; c = d[i+8:i+8+n]
        if t == b'IHDR': w, h, bd, ct = struct.unpack('>IIBB', c[:10])
        if t == b'IDAT': idat += c
        i += 12 + n
    bpp = {2: 3, 6: 4}[ct]; raw = zlib.decompress(idat); stride = w * bpp
    out = bytearray(h * stride); prev = bytearray(stride); pos = 0
    for y in range(h):
        f = raw[pos]; line = bytearray(raw[pos+1:pos+1+stride]); pos += 1 + stride
        for x in range(stride):
            a = line[x-bpp] if x >= bpp else 0; b = prev[x]; c = prev[x-bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out[y*stride:(y+1)*stride] = line; prev = line
    return w, h, bpp, out
src = sys.argv[1]; th = int(sys.argv[2]) if len(sys.argv) > 2 else 40
if not src.endswith('.png'):
    tmp = tempfile.mktemp(suffix='.png'); subprocess.run(['sips', '-s', 'format', 'png', src, '--out', tmp], capture_output=True); src = tmp
w, h, bpp, px = readpng(src)
x0, y0, x1, y1 = w, h, -1, -1
for y in range(h):
    row = px[y*w*bpp:(y+1)*w*bpp]
    for x in range(w):
        r, g, b = row[x*bpp], row[x*bpp+1], row[x*bpp+2]
        if r + g + b > th * 3:
            if x < x0: x0 = x
            if x > x1: x1 = x
            if y < y0: y0 = y
            if y > y1: y1 = y
print(f"size {w}x{h} bbox x0={x0} y0={y0} x1={x1+1} y1={y1+1}  (fractions {x0/w:.4f} {y0/h:.4f} {(x1+1)/w:.4f} {(y1+1)/h:.4f})")
