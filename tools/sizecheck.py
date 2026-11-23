# Brand-consistency check: the same settings must give every photo shape the exact same corner radius and border
# thickness and drop shadow (in pixels), with "Resize ... With Picture" ticked or unticked, in Scale and Fit in box modes.
# Usage: python3 -I tools/sizecheck.py   (needs build/harness-bin)
import itertools, os, re, subprocess, sys, tempfile
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TL = {'hd': (1920, 1080), '4k': (3840, 2160), 'vert': (2160, 3840)}
IM = {'landscape': (1920, 1080), 'square': (1080, 1080), 'portrait': (1080, 1920), 'narrow 9:21': (900, 2100)}
cases, keys = [], []
for (tn, (fw, fh)), mode, size, rc, rb in itertools.product(TL.items(), ('scale', 'box'), (90, 35), (0, 1), (0, 1)):
    sz = f'bSize=1 size={size}' if mode == 'scale' else f'bSize=0 boxW={size * .5} boxH={size}'
    for iname, (iw, ih) in IM.items():
        cases.append(f'{fw} {fh} {iw} {ih} {sz} cornerSize=4 rsCorner={rc} borderOn=1 borderW=2.5 rsBorder={rb} shadowOn=1 shDist=2 shSoft=3 shSize=1')
        keys.append((tn, mode, size, rc, rb, iname))
with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f: f.write('\n'.join(cases) + '\n')
out = subprocess.run([os.path.join(root, 'build/harness-bin'), 'geom', f.name], capture_output=True, text=True).stdout.splitlines()
groups, bad = {}, 0
for k, line in zip(keys, out):
    r = float(re.search(r'r=([\d.]+)', line).group(1)); b = float(re.search(r'border=([\d.]+)', line).group(1))
    sh = tuple(round(float(x), 6) for x in re.search(r'shadow=\[([^\]]+)\]', line).group(1).split())
    groups.setdefault(k[:5], []).append((k[5], r, b, sh))
for g, rows in groups.items():
    same = len({(round(r, 6), round(b, 6), sh) for _, r, b, sh in rows}) == 1
    bad += not same
    print(f"{'OK ' if same else 'BAD'} {g[0]:4} {g[1]:5} size/box {g[2]:3} rsCorner={g[3]} rsBorder={g[4]}: " +
          ', '.join(f'{n} r={r:.2f} b={b:.2f} shadow={sh[2]:.1f}' for n, r, b, sh in rows))
print(f'{len(groups) - bad}/{len(groups)} setting combinations identical across {len(IM)} photo shapes')
sys.exit(1 if bad else 0)
