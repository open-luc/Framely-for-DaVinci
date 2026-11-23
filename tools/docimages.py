# Renders the README pictures (docs/images/*.png) from the real plugin maths (build/harness-bin, the same code
# Resolve runs; tools/compare.py checks the two match), lays them out like the approved mock-up and screenshots the
# pages with headless Chrome.   Usage: python3 -I tools/docimages.py
import os, re, subprocess, tempfile, html
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
H = os.path.join(root, 'build/harness-bin')
WORK = os.path.join(root, 'build/doc'); OUT = os.path.join(root, 'docs/images')
os.makedirs(WORK, exist_ok=True); os.makedirs(OUT, exist_ok=True)
CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
FW, FH, K, HUE = 1920, 1080, 0.5, 215          # HD timeline, rendered at half size
PH = {'landscape': (1920, 1080), 'square': (1080, 1080), 'portrait': (1080, 1920)}

def kv(d): return [f'{k}={v}' for k, v in d.items()]
def render(name, photo, st, k=K):
    iw, ih = PH[photo]; out = os.path.join(WORK, name + '.png')
    subprocess.run([H, 'one', out, str(FW), str(FH), str(iw), str(ih), f'k={k}', f'hue={HUE}'] + kv(st), check=True)
    return out
def geom(photo, st):
    iw, ih = PH[photo]
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f: f.write(f'{FW} {FH} {iw} {ih} ' + ' '.join(kv(st)) + '\n')
    line = subprocess.run([H, 'geom', f.name], capture_output=True, text=True, check=True).stdout
    nums = lambda key: [float(x) for x in re.search(key + r'=\[([^\]]+)\]', line).group(1).replace('r=', '').split()]
    return {'shape': nums('shape'), 'win': nums('win'), 'border': float(re.search(r'border=([\d.]+)', line).group(1))}
def img(path): return f'<img src="file://{path}">'
def zoomed(path, z):   # a true magnification of a frame about its centre (rendered at k*z, cropped by CSS)
    return f'<div class="zoom"><img src="file://{path}" style="width:{z*100:.3f}%;margin-left:{-(z-1)/2*100:.3f}%;margin-top:{-(z-1)/2*56.25:.3f}%"></div>'

CSS = open(os.path.join(root, 'tools/docimages.css')).read()
def page(name, body, width=1200):
    p = os.path.join(WORK, name + '.html')
    open(p, 'w').write(f'<!doctype html><html><head><meta charset="utf-8"><style>{CSS}</style></head><body style="width:{width - 40}px">{body}</body></html>')
    raw = os.path.join(WORK, name + '.raw.png')
    with tempfile.TemporaryDirectory() as prof:
        subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1.5',
                        f'--user-data-dir={prof}', '--no-first-run', '--allow-file-access-from-files',
                        f'--window-size={width},4000', f'--screenshot={raw}', 'file://' + p], check=True, capture_output=True)
    subprocess.run([H, 'trim', raw, os.path.join(OUT, name + '.png'), '30'], check=True)
    print('wrote docs/images/' + name + '.png')

def checkbox(label, on, ticked_help, unticked_help):
    hl = lambda on_line: ' class="hl"' if on_line else ''
    return (f'<div class="panel"><div class="cb"><span class="box{" on" if on else ""}">{"✓" if on else ""}</span><span class="nm">{label}</span></div>'
            f'<div class="help">When you make the picture smaller with Size:<br><span{hl(on)}>Ticked: {ticked_help}</span><br>'
            f'<span{hl(not on)}>Unticked: {unticked_help}</span></div></div>')
def row(before, after, blown, after_cap, blown_cap):
    return (f'<div class="row3"><figure>{img(before)}<figcaption><b>1 · Before:</b> Size 90</figcaption></figure>'
            f'<div class="arrow">drag Size<br>down to 35<br>→</div>'
            f'<figure>{img(after)}<figcaption><b>2 · After:</b> {after_cap}</figcaption></figure>'
            f'<figure>{blown}<figcaption><b>3 · After, blown up</b> to compare with Before: {blown_cap}</figcaption></figure></div>')

# ---------------------------------------------------------------- 1 · same settings on three photo shapes
S1 = {'size': 90, 'cornerSize': 8.25, 'rsCorner': 1, 'borderOn': 1, 'borderPos': 1, 'borderW': 2.8, 'rsBorder': 0}
short = lambda g: min(g['win'][2] - g['win'][0], g['win'][3] - g['win'][1])
base = short(geom('landscape', S1))
names = {'landscape': 'Wide landscape', 'square': 'Square', 'portrait': 'Narrow portrait'}
withF, without = [], []
for ph in PH:
    g = geom(ph, S1)
    withF.append((render('p1_with_' + ph, ph, S1), names[ph], g['shape'][4], g['border']))
    f = short(g) / base           # "without Framely": sizes as a % of each photo's shorter side
    s2 = dict(S1, cornerSize=S1['cornerSize'] * f, borderW=S1['borderW'] * f); g2 = geom(ph, s2)
    without.append((render('p1_without_' + ph, ph, s2), names[ph], g2['shape'][4], g2['border']))
fig = lambda rows: '<div class="row3s">' + ''.join(
    f'<figure>{img(p)}<figcaption>{n}<br><b class="px">{r:.0f} px corners · {b:.0f} px border</b></figcaption></figure>' for p, n, r, b in rows) + '</div>'
page('1-same-settings-every-shape', f'''
<h2>1 · Paste the same settings onto different photo shapes</h2>
<p>A wide landscape, a square and a narrow portrait photo, all with the exact same pasted settings at Size 90.</p>
<section><span class="tag">With Framely</span>
<p class="note" style="margin-top:0">Every photo gets the same corners and the same border, whatever its shape.</p>{fig(withF)}</section>
<section class="badrow"><span class="tag bad">Without Framely (sizes measured as a % of each photo)</span>
<p class="note" style="margin-top:0">The narrow portrait ends up with smaller corners and a thinner border than the others, so the set doesn't match.</p>{fig(without)}</section>
<p class="foot">Rendered by Framely's own code in an HD (1920×1080) timeline. Pixel sizes are at full HD.</p>''')

# ---------------------------------------------------------------- 2 · corners
Z = 90 / 35 * 0.85                # blow-up factor (a little under 90/35 so a thick outside border still fits the frame)
C = {'cornerSize': 22, 'borderOn': 0}
secs = ''
for on in (1, 0):
    st = dict(C, rsCorner=on)
    b = render(f'p2_before_{on}', 'landscape', dict(st, size=90)); a = render(f'p2_after_{on}', 'landscape', dict(st, size=35))
    z = render(f'p2_blown_{on}', 'landscape', dict(st, size=35), k=K * Z)
    secs += (f'<section><span class="tag{"" if on else " alt"}">{"Ticked (default)" if on else "Unticked"}</span>'
             + checkbox('Resize Corners With Picture', on, 'the corners shrink with it, like zooming out on the whole picture.',
                        'the corners keep their size, so the smaller picture looks rounder.')
             + row(b, a, zoomed(z, Z), 'corners got smaller too' if on else 'corners kept their size',
                   'same shape as Before, just further away' if on else 'much rounder than Before') + '</section>')
page('2-resize-corners', f'''<h2>2 · Shape section: what happens when you change Size</h2>
<p>Same photo, same settings. Drag Size from 90 down to 35. Only the checkbox differs.</p>{secs}''')

# ---------------------------------------------------------------- 3 · border
B = {'cornerSize': 0, 'borderOn': 1, 'borderPos': 1, 'borderW': 3}
secs = ''
for on in (0, 1):
    st = dict(B, rsBorder=on)
    b = render(f'p3_before_{on}', 'landscape', dict(st, size=90)); a = render(f'p3_after_{on}', 'landscape', dict(st, size=35))
    z = render(f'p3_blown_{on}', 'landscape', dict(st, size=35), k=K * Z)
    secs += (f'<section><span class="tag{" alt" if on else ""}">{"Ticked" if on else "Unticked (default)"}</span>'
             + checkbox('Resize Border With Picture', on, 'the border shrinks with it, like zooming out on the whole picture.',
                        "the border keeps its thickness, so the smaller picture's border looks thicker.")
             + row(b, a, zoomed(z, Z), 'border got thinner too' if on else 'border kept its thickness',
                   'same look as Before, just further away' if on else 'much thicker border than Before') + '</section>')
page('3-resize-border', f'''<h2>3 · Border section: what happens when you change Size</h2>
<p>Same photo, same settings, square corners so only the border changes. Drag Size from 90 down to 35.</p>{secs}''')

# ---------------------------------------------------------------- Guides (drawn from the same geometry the overlay uses)
G = dict(S1, size=70)
bg = render('guides_bg', 'square', G)
svg = []
for gi, (lab, (iw, ih), col) in enumerate([('9:16', (1080, 1920), '#4fd1ff'), ('1:1', (1080, 1080), '#7df29b'), ('16:9', (1920, 1080), '#ff6bd6')]):
    PH['_g'] = (iw, ih); x0, y0, x1, y1, r = geom('_g', G)['shape']
    svg.append(f'<rect x="{x0}" y="{y0}" width="{x1-x0}" height="{y1-y0}" rx="{r}" fill="none" stroke="#000" stroke-opacity=".55" stroke-width="7"/>'
               f'<rect x="{x0}" y="{y0}" width="{x1-x0}" height="{y1-y0}" rx="{r}" fill="none" stroke="{col}" stroke-width="3.5" stroke-dasharray="16 10"/>'
               f'<text x="{x0+12}" y="{y0+34+gi*40}" fill="{col}" font-size="30" font-weight="600" stroke="#000" stroke-width="5" paint-order="stroke">{lab}</text>')
cx, cy, arm = FW / 2, FH / 2, 56
svg.append(f'<path d="M{cx-arm} {cy}H{cx+arm}M{cx} {cy-arm}V{cy+arm}" stroke="#000" stroke-opacity=".75" stroke-width="9"/>'
           f'<path d="M{cx-arm} {cy}H{cx+arm}M{cx} {cy-arm}V{cy+arm}" stroke="#fff" stroke-width="4"/>'
           f'<circle cx="{cx}" cy="{cy}" r="9" fill="#e8a33d" stroke="#000" stroke-opacity=".75" stroke-width="3"/>')
page('guides', f'''<div class="viewer"><div class="vbar"><span>OpenFX Overlay</span><span>Framely Guides on</span></div>
<div class="vframe">{img(bg)}<svg viewBox="0 0 {FW} {FH}">{"".join(svg)}</svg></div></div>
<p class="foot">Illustration drawn from Framely's own geometry: a square clip at Size 70, with the 9:16, 1:1 and 16:9 guides showing where photos of those shapes land, corners included.</p>''', width=1000)

# ---------------------------------------------------------------- Inspector panel (illustration of the real parameters)
def slider(lab, val, frac): return f'<div class="prow"><span class="pl">{lab}</span><span class="sl"><i style="width:{frac*100:.0f}%"></i></span><span class="pv">{val}</span></div>'
def cbx(lab, on): return f'<div class="prow"><span class="pl"></span><span class="pcb"><span class="box{" on" if on else ""}">{"✓" if on else ""}</span>{html.escape(lab)}</span></div>'
def tip(lines): return '<div class="tip">' + '<br>'.join(lines) + '</div>'
page('inspector', f'''<div class="insp"><div class="ihead">Inspector · Effects · Framely – Picture in Picture</div>
<div class="grp">▾ Shape</div>{slider("Corner Size", "7.500", .15)}{slider("Circle", "0.000", 0)}{cbx("Resize Corners With Picture", True)}
{tip(["When you make the picture smaller with Size:", "Ticked: the corners shrink with it, like zooming out on the whole picture.", "Unticked: the corners keep their size, so the smaller picture looks rounder."])}
<div class="prow"><span class="pl"></span><span class="btn">Reset Shape</span></div>
<div class="grp">▾ Border</div>{cbx("Border", True)}{slider("Border Width", "1.200", .12)}
<div class="prow"><span class="pl">Placement</span><span class="dd">Outside the edge ▾</span></div>
<div class="prow"><span class="pl">Border Color</span><span class="sw"></span></div>{cbx("Resize Border With Picture", False)}
{tip(["When you make the picture smaller with Size:", "Ticked: the border shrinks with it, like zooming out on the whole picture.", "Unticked: the border keeps its thickness, so the smaller picture's border looks thicker."])}
<div class="prow"><span class="pl"></span><span class="btn">Reset Border</span></div></div>
<p class="foot">Illustration of the Shape and Border sections in Resolve's Inspector, with each checkbox's help text.</p>''', width=760)
