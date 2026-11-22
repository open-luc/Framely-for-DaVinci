# Compare every Resolve render in build/resolve against the harness's expected frame.
import os, sys, subprocess, glob, re
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
cfgs = {}
for line in open(os.path.join(root, 'tools/configs.txt')):
    if line.startswith('#') or '|' not in line: continue
    n, a = [x.strip() for x in line.split('|', 1)]; cfgs[n] = a.split()
def harness_keys(args):
    out = []
    for kv in args:
        k, v = kv.split('=')
        if k == 'measureFrom': continue
        if k == 'borderPos': out.append('borderOutside=' + v); continue
        if k in ('borderColor', 'shadowColor'):
            r, g, b = [float(x) for x in v.split(',')]; out.append(f"{k}={int(round(r*255)):02x}{int(round(g*255)):02x}{int(round(b*255)):02x}"); continue
        out.append(kv)
    return out
TL = {'4k': (3840, 2160), 'hd': (1920, 1080), 'vert': (2160, 3840)}
IM = {'portrait': (1080, 1920, 200), 'square': (1080, 1080, 140), 'landscape': (1920, 1080, 275)}
only = sys.argv[1] if len(sys.argv) > 1 else None
fails = 0; total = 0
for f in sorted(glob.glob(os.path.join(root, 'build/resolve*/*.png'))):
    m = re.match(r'(\w+?)_(4k|hd|vert)_(portrait|square|landscape)_\d+\.png', os.path.basename(f))
    if not m: continue
    c, t, i = m.groups()
    if only and c != only: continue
    fw, fh = TL[t]; iw, ih, hue = IM[i]
    diff = f.replace('.png', '.diff.png').replace('/resolve2/', '/diff/').replace('/resolve/', '/diff/')
    os.makedirs(os.path.dirname(diff), exist_ok=True)
    r = subprocess.run([os.path.join(root, 'build/harness-bin'), 'compare', f, diff, str(fw), str(fh), str(iw), str(ih), str(hue)] + harness_keys(cfgs[c]), capture_output=True, text=True)
    total += 1; fails += r.returncode != 0
    print(f"{c:8} {t:4} {i:9} {r.stdout.strip()}")
print(f"{total - fails}/{total} passed")
