let FW = 3840, FH = 2160;
const clamp01 = x => Math.max(0, Math.min(1, x));
function fitted(img) { const s = Math.min(FW / img.w, FH / img.h); return { w: img.w * s, h: img.h * s }; }
function geom(st, img) {
  const b = st.basis, v = st.v, Ss = Math.min(FW, FH), f = fitted(img), refW = f.w, refH = f.h;
  // 1. crop, measured on the picture at "fits the screen" size
  let cl, cr, ct, cb;
  if (b.crop === "screen") { cl = v.cropL / 100 * FW; cr = v.cropR / 100 * FW; ct = v.cropT / 100 * FH; cb = v.cropB / 100 * FH; }
  else { cl = v.cropL / 100 * refW; cr = v.cropR / 100 * refW; ct = v.cropT / 100 * refH; cb = v.cropB / 100 * refH; }
  const minRef = Math.min(refW, refH) * .01;
  if (refW - cl - cr < minRef) { const k = (refW - minRef) / Math.max(1e-9, cl + cr); cl *= k; cr *= k; }
  if (refH - ct - cb < minRef) { const k = (refH - minRef) / Math.max(1e-9, ct + cb); ct *= k; cb *= k; }
  const wW = refW - cl - cr, wH = refH - ct - cb, offX = (cl - cr) / 2, offY = (ct - cb) / 2;
  // 2. size & position
  // Origin: the point every picture is centred on. The screen centre, or the safe area's centre when that's on.
  let safe = null, safeAdjusted = false;
  if (st.safeOn) {
    safe = { x0: v.safeL / 100 * FW, x1: FW - v.safeR / 100 * FW, y0: v.safeT / 100 * FH, y1: FH - v.safeB / 100 * FH };
    if (safe.x1 - safe.x0 < FW * .02) { const c = (safe.x0 + safe.x1) / 2; safe.x0 = c - FW * .01; safe.x1 = c + FW * .01; }   // margins overlap: keep a sliver
    if (safe.y1 - safe.y0 < FH * .02) { const c = (safe.y0 + safe.y1) / 2; safe.y0 = c - FH * .01; safe.y1 = c + FH * .01; }
  }
  const origin = safe ? { x: (safe.x0 + safe.x1) / 2, y: (safe.y0 + safe.y1) / 2 } : { x: FW / 2, y: FH / 2 };
  const ax = origin.x + v.posX / 100 * FW, ay = origin.y - v.posY / 100 * FH;
  let s = b.size === "screen" ? Math.min(v.boxW / 100 * FW / wW, v.boxH / 100 * FH / wH) : v.size / 100;
  const place = () => b.size === "screen" ? [ax, ay] : [ax + offX * s, ay + offY * s];   // window centre
  let [wcx, wcy] = place();
  if (safe) {
    // Keep inside safe area: shrink only if the picture (plus an outside border) is too big, then push it back inside.
    const sW = safe.x1 - safe.x0, sH = safe.y1 - safe.y0;
    const obOf = sc => (st.borderOn && st.borderPos === "outside") ? (b.border === "screen" ? v.borderW / 100 * Ss : v.borderW / 100 * Math.min(wW, wH) * sc) : 0;
    for (let i = 0; i < 3; i++) {
      const tw = wW * s + 2 * obOf(s), th = wH * s + 2 * obOf(s);
      if (tw > sW + .01 || th > sH + .01) { s *= Math.min(sW / tw, sH / th); safeAdjusted = true; }
    }
    [wcx, wcy] = place();
    const ob = obOf(s), hw = wW * s / 2 + ob, hh = wH * s / 2 + ob;
    const nx = Math.min(Math.max(wcx, safe.x0 + hw), safe.x1 - hw), ny = Math.min(Math.max(wcy, safe.y0 + hh), safe.y1 - hh);
    if (Math.abs(nx - wcx) > .5 || Math.abs(ny - wcy) > .5) safeAdjusted = true;
    wcx = nx; wcy = ny;
  }
  const cx = wcx - offX * s, cy = wcy - offY * s;
  const W = refW * s, H = refH * s, ww = wW * s, wh = wH * s, winShort = Math.min(ww, wh), half0 = winShort / 2;
  // 3. shape: corners, then circle pull-in
  const cornerPx = Math.min(b.shape === "screen" ? v.corner / 100 * Ss : v.corner / 100 * half0, half0);
  const t = clamp01(v.circle / 100);
  let sw = ww, sh = wh; if (ww > wh) sw = ww - (ww - wh) * t; else sh = wh - (wh - ww) * t;
  const shortS = Math.min(sw, sh) / 2, r = Math.min(cornerPx + (shortS - cornerPx) * t, shortS);
  const shape = { x0: wcx - sw / 2, y0: wcy - sh / 2, x1: wcx + sw / 2, y1: wcy + sh / 2, r };
  // 4. reframe: image zoomed about its own centre, then panned; kept covering the shape
  const z = v.zoom / 100, cw = W * z, ch = H * z;
  const unitX = b.crop === "screen" ? FW * s : W, unitY = b.crop === "screen" ? FH * s : H;
  let ccx = cx + v.panX / 100 * unitX, ccy = cy - v.panY / 100 * unitY;
  const nx = Math.min(Math.max(ccx, shape.x1 - cw / 2), shape.x0 + cw / 2), ny = Math.min(Math.max(ccy, shape.y1 - ch / 2), shape.y0 + ch / 2);
  const panLimited = Math.abs(nx - ccx) > .5 || Math.abs(ny - ccy) > .5; ccx = nx; ccy = ny;
  const len = (val, grp) => b[grp] === "screen" ? val / 100 * Ss : val / 100 * winShort;
  const ang = v.shAng * Math.PI / 180, dist = len(v.shDist, "shadow");
  return { s, W, H, cx, cy, Ss, winShort, half0, cornerPx, refW, refH, crop: { cl, cr, ct, cb },
    win: { x0: wcx - ww / 2, x1: wcx + ww / 2, y0: wcy - wh / 2, y1: wcy + wh / 2, w: ww, h: wh, cx: wcx, cy: wcy },
    shape, content: { x: ccx - cw / 2, y: ccy - ch / 2, w: cw, h: ch }, panLimited, safe, safeAdjusted, origin, anchor: { x: ax, y: ay },
    panPx: { x: ccx - cx, y: -(ccy - cy) }, unitX, unitY,
    borderPx: len(v.borderW, "border"),
    shadow: { dist, dx: dist * Math.cos(ang), dy: -dist * Math.sin(ang), soft: len(v.shSoft, "shadow"), spread: len(v.shSize, "shadow") } };
}

// ---- random cases: prints settings line (for harness) and expected geometry ----
let seed = 12345; const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
const pick = a => a[Math.floor(rnd() * a.length)];
const r = (a, b) => +(a + rnd() * (b - a)).toFixed(2);
const cases = [], expect = [];
const TL = [[3840,2160],[1920,1080],[2160,3840]], IM = [[1080,1920],[1080,1080],[1920,1080],[4000,3000],[1000,2500]];
for (let i = 0; i < 400; i++) {
  [FW, FH] = pick(TL); const [iw, ih] = pick(IM);
  const b = { size: pick(["screen","image"]), crop: pick(["screen","image"]), shape: pick(["screen","image"]), border: pick(["screen","image"]), shadow: pick(["screen","image"]) };
  const v = { boxW: r(5,100), boxH: r(5,100), size: r(5,200), posX: r(-60,60), posY: r(-60,60),
    cropL: r(0,40)*(rnd()<.5), cropR: r(0,40)*(rnd()<.5), cropT: r(0,40)*(rnd()<.5), cropB: r(0,40)*(rnd()<.5),
    zoom: rnd()<.5?100:r(100,400), panX: r(-50,50)*(rnd()<.5), panY: r(-50,50)*(rnd()<.5),
    corner: r(0,100), circle: rnd()<.5?0:r(0,100), borderW: r(0,10), shOp: 60, shDist: r(0,20), shSoft: r(0,20), shSize: r(-15,15), shAng: r(0,360),
    safeT: r(0,40), safeB: r(0,40), safeL: r(0,40), safeR: r(0,40) };
  const st = { basis: b, v, safeOn: rnd()<.4, borderOn: rnd()<.6, borderPos: pick(["inside","outside"]) };
  const B = x => x === "screen" ? 0 : 1;
  let line = `${FW} ${FH} ${iw} ${ih} bSize=${B(b.size)} bCrop=${B(b.crop)} bShape=${B(b.shape)} bBorder=${B(b.border)} bShadow=${B(b.shadow)} safeOn=${+st.safeOn} borderOn=${+st.borderOn} borderOutside=${+(st.borderPos==="outside")}`;
  for (const [k, val] of Object.entries(v)) line += ` ${k}=${val}`;
  cases.push(line);
  const G = geom(st, { w: iw, h: ih });
  expect.push([G.s, G.W, G.H, G.cx, G.cy, G.shape.x0, G.shape.y0, G.shape.x1, G.shape.y1, G.shape.r, G.content.x, G.content.y, G.content.w, G.content.h,
    G.win.x0, G.win.y0, G.win.x1, G.win.y1, G.borderPx, G.shadow.dx, G.shadow.dy, G.shadow.soft, G.shadow.spread, G.panPx.x, G.panPx.y, +G.safeAdjusted, +G.panLimited].join(" "));
}
Deno.writeTextFileSync(Deno.args[0], cases.join("\n") + "\n");
Deno.writeTextFileSync(Deno.args[1], expect.join("\n") + "\n");
