// FramelyCore.h — geometry + pixel math for Framely – Picture in Picture.
// Pure C++17, no OFX dependency, so it can be tested by tools/harness.cpp outside Resolve.
//
// Geometry is a line-by-line port of geom() in the spec mockup
// (https://claude.ai/artifact/G39ChnbFmpoKvM3vWW9GEY). Units: "screen" pixels of the full-size
// timeline frame, y pointing DOWN (like the mockup's canvas). Every user value is a % (never pixels).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace framely {

enum Basis { kScreen = 0, kImage = 1 };

struct RGB { double r = 1, g = 1, b = 1; };

struct Settings {
    // bSize: kScreen = "Fit in box", kImage = "Scale". Crop is always a share of each picture (kImage).
    int bSize = kImage, bCrop = kImage;
    // Size & Position
    double boxW = 40, boxH = 45;          // % of screen width / height   (Fit in box)
    double size = 90;                     // % · 100 = fits the screen    (Scale)
    double posX = 0, posY = 0;            // % of screen width / height from the centre, up is +
    bool   safeOn = false;
    double safeT = 5, safeB = 5, safeL = 5, safeR = 5;   // % of screen height / width
    // Crop & Reframe
    double cropL = 0, cropR = 0, cropT = 0, cropB = 0;   // % of screen or of image
    double zoom = 100;                    // %
    double panX = 0, panY = 0;            // % of screen*scale or of picture; up is +
    // Shape
    // Corners and border are a % of the SCREEN's shorter side, never of the photo, so pasted settings give every photo
    // shape the exact same corners and border. "Resize ... With Picture" multiplies them by Size/100 (Fit in box: the
    // box's shorter side / the screen's shorter side), never by anything that depends on the photo's shape.
    double corner = 7.5;                  // Corner Size, % of screen short side
    bool   rsCorner = true;               // Resize Corners With Picture
    double circle = 0;                    // %
    // Border
    bool   borderOn = false, borderOutside = true;   // outside: matches the plugin's factory settings
    double borderW = 1.2;                 // % of screen short side
    bool   rsBorder = false;              // Resize Border With Picture
    RGB    borderColor{1, 1, 1};
    // Shadow: Distance, Softness and Size are % of the screen's shorter side × Size/100 (like ticked corners), so every
    // photo shape gets the identical shadow and it always grows and shrinks with the picture.
    bool   shadowOn = false;
    double shOp = 60, shDist = 1.5, shSoft = 2.5, shSize = 0, shAng = 315;
    RGB    shadowColor{0, 0, 0};
};

struct RRect { double x0 = 0, y0 = 0, x1 = 0, y1 = 0, r = 0; bool valid = true; };
struct Rect  { double x = 0, y = 0, w = 0, h = 0; };

struct Geom {
    double s = 1, W = 0, H = 0, cx = 0, cy = 0, Ss = 0, winShort = 0, half0 = 0, cornerPx = 0, refW = 0, refH = 0;
    double cl = 0, cr = 0, ct = 0, cb = 0;
    struct { double x0, x1, y0, y1, w, h, cx, cy; } win{};
    RRect shape;
    Rect  content;                 // where the (zoomed, panned) image is drawn
    bool  panLimited = false;
    bool  hasSafe = false; struct { double x0, x1, y0, y1; } safe{};
    bool  safeAdjusted = false;
    double originX = 0, originY = 0, anchorX = 0, anchorY = 0;
    double panPxX = 0, panPxY = 0, unitX = 0, unitY = 0;
    double borderPx = 0;
    double shDist = 0, shDx = 0, shDy = 0, shSoft = 0, shSpread = 0;
};

inline double clamp01(double x) { return std::max(0.0, std::min(1.0, x)); }
inline double clampd(double v, double lo, double hi) { return std::min(std::max(v, lo), hi); }

// FW,FH: timeline frame size. imgW,imgH: the image's own size (only its aspect matters).
inline Geom computeGeom(const Settings& st, double FW, double FH, double imgW, double imgH) {
    Geom G;
    const double Ss = std::min(FW, FH);
    const double fs = std::min(FW / imgW, FH / imgH);
    const double refW = imgW * fs, refH = imgH * fs;
    // 1. crop, measured on the picture at "fits the screen" size
    double cl, cr, ct, cb;
    if (st.bCrop == kScreen) { cl = st.cropL / 100 * FW; cr = st.cropR / 100 * FW; ct = st.cropT / 100 * FH; cb = st.cropB / 100 * FH; }
    else { cl = st.cropL / 100 * refW; cr = st.cropR / 100 * refW; ct = st.cropT / 100 * refH; cb = st.cropB / 100 * refH; }
    const double minRef = std::min(refW, refH) * .01;
    if (refW - cl - cr < minRef) { double k = (refW - minRef) / std::max(1e-9, cl + cr); cl *= k; cr *= k; }
    if (refH - ct - cb < minRef) { double k = (refH - minRef) / std::max(1e-9, ct + cb); ct *= k; cb *= k; }
    const double wW = refW - cl - cr, wH = refH - ct - cb, offX = (cl - cr) / 2, offY = (ct - cb) / 2;
    // 2. size & position. Origin = screen centre, or the safe area's centre when that's on.
    bool hasSafe = st.safeOn; double sx0 = 0, sx1 = 0, sy0 = 0, sy1 = 0;
    if (hasSafe) {
        sx0 = st.safeL / 100 * FW; sx1 = FW - st.safeR / 100 * FW; sy0 = st.safeT / 100 * FH; sy1 = FH - st.safeB / 100 * FH;
        if (sx1 - sx0 < FW * .02) { double c = (sx0 + sx1) / 2; sx0 = c - FW * .01; sx1 = c + FW * .01; }
        if (sy1 - sy0 < FH * .02) { double c = (sy0 + sy1) / 2; sy0 = c - FH * .01; sy1 = c + FH * .01; }
    }
    const double ox = hasSafe ? (sx0 + sx1) / 2 : FW / 2, oy = hasSafe ? (sy0 + sy1) / 2 : FH / 2;
    const double ax = ox + st.posX / 100 * FW, ay = oy - st.posY / 100 * FH;
    // Corners & border: one factor for every photo shape (see Settings)
    const double sizeFactor = st.bSize == kScreen ? std::min(st.boxW / 100 * FW, st.boxH / 100 * FH) / Ss : st.size / 100;
    const double borderPx = st.borderW / 100 * Ss * (st.rsBorder ? sizeFactor : 1);
    const double cornerWant = st.corner / 100 * Ss * (st.rsCorner ? sizeFactor : 1);
    double s = st.bSize == kScreen ? std::min(st.boxW / 100 * FW / wW, st.boxH / 100 * FH / wH) : st.size / 100;
    auto placeX = [&]() { return st.bSize == kScreen ? ax : ax + offX * s; };
    auto placeY = [&]() { return st.bSize == kScreen ? ay : ay + offY * s; };
    double wcx = placeX(), wcy = placeY();
    bool safeAdjusted = false;
    if (hasSafe) {
        const double sW = sx1 - sx0, sH = sy1 - sy0;
        auto obOf = [&](double) { return st.borderOn && st.borderOutside ? borderPx : 0.0; };
        for (int i = 0; i < 3; i++) {
            double tw = wW * s + 2 * obOf(s), th = wH * s + 2 * obOf(s);
            if (tw > sW + .01 || th > sH + .01) { s *= std::min(sW / tw, sH / th); safeAdjusted = true; }
        }
        wcx = placeX(); wcy = placeY();
        double ob = obOf(s), hw = wW * s / 2 + ob, hh = wH * s / 2 + ob;
        double nx = std::min(std::max(wcx, sx0 + hw), sx1 - hw), ny = std::min(std::max(wcy, sy0 + hh), sy1 - hh);
        if (std::fabs(nx - wcx) > .5 || std::fabs(ny - wcy) > .5) safeAdjusted = true;
        wcx = nx; wcy = ny;
    }
    const double cx = wcx - offX * s, cy = wcy - offY * s;
    const double W = refW * s, H = refH * s, ww = wW * s, wh = wH * s, winShort = std::min(ww, wh), half0 = winShort / 2;
    // 3. shape: corners, then circle pull-in
    const double cornerPx = std::min(cornerWant, half0);   // can't be rounder than fully round
    const double t = clamp01(st.circle / 100);
    double sw = ww, sh = wh; if (ww > wh) sw = ww - (ww - wh) * t; else sh = wh - (wh - ww) * t;
    const double shortS = std::min(sw, sh) / 2, r = std::min(cornerPx + (shortS - cornerPx) * t, shortS);
    G.shape = { wcx - sw / 2, wcy - sh / 2, wcx + sw / 2, wcy + sh / 2, r, true };
    // 4. reframe: image zoomed about its own centre, then panned; kept covering the shape
    const double z = st.zoom / 100, cw = W * z, ch = H * z;
    const double unitX = st.bCrop == kScreen ? FW * s : W, unitY = st.bCrop == kScreen ? FH * s : H;
    double ccx = cx + st.panX / 100 * unitX, ccy = cy - st.panY / 100 * unitY;
    double nx = std::min(std::max(ccx, G.shape.x1 - cw / 2), G.shape.x0 + cw / 2);
    double ny = std::min(std::max(ccy, G.shape.y1 - ch / 2), G.shape.y0 + ch / 2);
    G.panLimited = std::fabs(nx - ccx) > .5 || std::fabs(ny - ccy) > .5; ccx = nx; ccy = ny;
    auto len = [&](double val) { return val / 100 * Ss * sizeFactor; };
    const double ang = st.shAng * M_PI / 180, dist = len(st.shDist);

    G.s = s; G.W = W; G.H = H; G.cx = cx; G.cy = cy; G.Ss = Ss; G.winShort = winShort; G.half0 = half0; G.cornerPx = cornerPx;
    G.refW = refW; G.refH = refH; G.cl = cl; G.cr = cr; G.ct = ct; G.cb = cb;
    G.win = { wcx - ww / 2, wcx + ww / 2, wcy - wh / 2, wcy + wh / 2, ww, wh, wcx, wcy };
    G.content = { ccx - cw / 2, ccy - ch / 2, cw, ch };
    G.hasSafe = hasSafe; G.safe = { sx0, sx1, sy0, sy1 }; G.safeAdjusted = safeAdjusted;
    G.originX = ox; G.originY = oy; G.anchorX = ax; G.anchorY = ay;
    G.panPxX = ccx - cx; G.panPxY = -(ccy - cy); G.unitX = unitX; G.unitY = unitY;
    G.borderPx = borderPx;
    G.shDist = dist; G.shDx = dist * std::cos(ang); G.shDy = -dist * std::sin(ang);
    G.shSoft = len(st.shSoft); G.shSpread = len(st.shSize);
    return G;
}

// Pan values that hold exactly what is visible (the mockup's normalizePan): store these back after edits
// so Pan never holds hidden offset (a crop would otherwise make the image slide).
inline void normalizedPan(const Geom& G, double& panX, double& panY) {
    panX = G.panPxX / G.unitX * 100; panY = G.panPxY / G.unitY * 100;
}

inline RRect grow(const RRect& sh, double d) {
    RRect g{ sh.x0 - d, sh.y0 - d, sh.x1 + d, sh.y1 + d, std::max(0.0, sh.r + d), sh.valid };
    if (g.x1 - g.x0 < 1 || g.y1 - g.y0 < 1) g.valid = false;
    return g;
}
inline RRect innerOf(const RRect& sh, double bw) {   // inside border: inner edge stays rounded
    double w = sh.x1 - sh.x0 - 2 * bw, h = sh.y1 - sh.y0 - 2 * bw;
    RRect r; if (w <= 0 || h <= 0) { r.valid = false; return r; }
    double rr = sh.r > 0 ? std::max(sh.r - bw, sh.r * .4) : 0;
    return { sh.x0 + bw, sh.y0 + bw, sh.x1 - bw, sh.y1 - bw, std::min({rr, w / 2, h / 2}), true };
}

// Signed distance from (px,py) to a rounded rectangle (negative inside).
inline double sdRRect(const RRect& R, double px, double py) {
    double hx = (R.x1 - R.x0) / 2, hy = (R.y1 - R.y0) / 2;
    double r = std::min(R.r, std::min(hx, hy));
    double qx = std::fabs(px - (R.x0 + R.x1) / 2) - hx + r, qy = std::fabs(py - (R.y0 + R.y1) / 2) - hy + r;
    double ox = std::max(qx, 0.0), oy = std::max(qy, 0.0);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0) - r;
}
// Anti-aliased coverage: d in output pixels. 1-pixel-wide ramp = sharp edge, never feathered.
inline double cover(double dPx) { return clamp01(0.5 - dPx); }
// Gaussian-blurred edge (exact for a straight edge), sigma in output pixels.
inline double softCover(double dPx, double sigma) {
    if (sigma < 0.35) return cover(dPx);
    return 0.5 * std::erfc(dPx / (sigma * M_SQRT2));
}

// -------- pixel rendering --------
// Source view: RGBA float, premultiplied, rows addressed by y in [y1,y2) with y UP (OFX convention).
struct SrcView {
    const float* data = nullptr; long rowBytes = 0; int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    const float* px(int x, int y) const { return (const float*)((const char*)data + (long)(y - y1) * rowBytes) + (long)(x - x1) * 4; }
};

// Bilinear sample at continuous source position (sx, sy) in OFX pixel coords (y up), premultiplied, clamped to `clampR`.
inline void sampleBilinear(const SrcView& s, double fx, double fy, int cx1, int cy1, int cx2, int cy2, float out[4]) {
    fx -= 0.5; fy -= 0.5;
    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    double tx = fx - x0, ty = fy - y0;
    int xa = std::clamp(x0, cx1, cx2 - 1), xb = std::clamp(x0 + 1, cx1, cx2 - 1);
    int ya = std::clamp(y0, cy1, cy2 - 1), yb = std::clamp(y0 + 1, cy1, cy2 - 1);
    const float *p00 = s.px(xa, ya), *p10 = s.px(xb, ya), *p01 = s.px(xa, yb), *p11 = s.px(xb, yb);
    for (int c = 0; c < 4; c++) {
        double a = p00[c] + (p10[c] - p00[c]) * tx, b = p01[c] + (p11[c] - p01[c]) * tx;
        out[c] = float(a + (b - a) * ty);
    }
}

// Everything the per-pixel loop needs, already in OUTPUT pixel units (render scale applied), y DOWN.
struct RenderPlan {
    double k = 1;                 // render scale (output px per screen px)
    double FHpx = 0;              // output frame height in output px (to flip y)
    double frameX0 = 0, frameY0 = 0;   // output-pixel position of the frame's bottom-left (OFX coords)
    RRect shape, inner, outer, shadow;
    bool  borderOn = false, outside = false, shadowOn = false;
    float bc[3]{1, 1, 1}, sc[3]{0, 0, 0}; double shOp = 0, sigma = 0, shDx = 0, shDy = 0;
    Rect content;                 // output px, y down
    int srcX1 = 0, srcY1 = 0, srcX2 = 0, srcY2 = 0;   // the image's own area inside the source frame (OFX px, y up)
    int bbX1 = 0, bbY1 = 0, bbX2 = 0, bbY2 = 0;       // output px (OFX, y up) that can be non-transparent
};

inline RRect scaleR(const RRect& r, double k) { return { r.x0 * k, r.y0 * k, r.x1 * k, r.y1 * k, r.r * k, r.valid }; }

// frameW/H: full-res timeline frame; k: render scale; src area: the image's pixels inside the source (render-scale px, y up)
inline RenderPlan makePlan(const Settings& st, const Geom& G, double frameW, double frameH, double k,
                           int srcX1, int srcY1, int srcX2, int srcY2) {
    (void)frameW;
    RenderPlan P; P.k = k; P.FHpx = frameH * k;
    P.shape = scaleR(G.shape, k);
    P.borderOn = st.borderOn && G.borderPx > 0;
    P.outside = P.borderOn && st.borderOutside;
    double bw = G.borderPx * k;
    P.outer = P.outside ? grow(P.shape, bw) : P.shape;
    P.inner = (P.borderOn && !P.outside) ? innerOf(P.shape, bw) : P.shape;
    P.shadowOn = st.shadowOn && st.shOp > 0;
    if (P.shadowOn) {
        RRect sil = grow(P.outer, G.shSpread * k);
        if (!sil.valid) P.shadowOn = false;
        sil.x0 += G.shDx * k; sil.x1 += G.shDx * k; sil.y0 += G.shDy * k; sil.y1 += G.shDy * k;
        P.shadow = sil;
    }
    P.bc[0] = (float)st.borderColor.r; P.bc[1] = (float)st.borderColor.g; P.bc[2] = (float)st.borderColor.b;
    P.sc[0] = (float)st.shadowColor.r; P.sc[1] = (float)st.shadowColor.g; P.sc[2] = (float)st.shadowColor.b;
    P.shOp = st.shOp / 100; P.sigma = G.shSoft * k / 2;     // canvas shadowBlur → sigma = blur / 2
    P.content = { G.content.x * k, G.content.y * k, G.content.w * k, G.content.h * k };
    P.srcX1 = srcX1; P.srcY1 = srcY1; P.srcX2 = srcX2; P.srcY2 = srcY2;
    // bounding box (y down) of everything we draw, converted to y-up output pixels
    double x0 = P.outer.x0, y0 = P.outer.y0, x1 = P.outer.x1, y1 = P.outer.y1;
    if (P.shadowOn) {
        double m = 4 * P.sigma + 2;
        x0 = std::min(x0, P.shadow.x0 - m); y0 = std::min(y0, P.shadow.y0 - m);
        x1 = std::max(x1, P.shadow.x1 + m); y1 = std::max(y1, P.shadow.y1 + m);
    }
    P.bbX1 = (int)std::floor(x0) - 2; P.bbX2 = (int)std::ceil(x1) + 2;
    P.bbY1 = (int)std::floor(P.FHpx - y1) - 2; P.bbY2 = (int)std::ceil(P.FHpx - y0) + 2;
    return P;
}

// Render one output pixel at OFX pixel coords (x, y), y up, origin = frame bottom-left. Output premultiplied RGBA.
inline void shadePixel(const RenderPlan& P, const SrcView& src, int x, int y, float out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    if (x < P.bbX1 || x >= P.bbX2 || y < P.bbY1 || y >= P.bbY2) return;
    const double px = x + 0.5, py = P.FHpx - (y + 0.5);    // y-down screen position (output px)
    // Fast path: well inside the picture (and inside any inside-border ring) with an opaque image pixel,
    // nothing underneath can show, so skip shadow/border maths. Same result as the full path.
    if (sdRRect(P.shape, px, py) < -1 && (!(P.borderOn && !P.outside) || (P.inner.valid && sdRRect(P.inner, px, py) < -1))
        && P.content.w > 0 && P.content.h > 0) {
        double u = (px - P.content.x) / P.content.w, v = (py - P.content.y) / P.content.h;
        if (u >= 0 && u <= 1 && v >= 0 && v <= 1) {
            float img[4];
            sampleBilinear(src, P.srcX1 + u * (P.srcX2 - P.srcX1), P.srcY2 - v * (P.srcY2 - P.srcY1), P.srcX1, P.srcY1, P.srcX2, P.srcY2, img);
            if (img[3] >= 0.9999f) { out[0] = img[0]; out[1] = img[1]; out[2] = img[2]; out[3] = img[3]; return; }
        }
    }
    // shadow (bottom layer)
    if (P.shadowOn) {
        double a = P.shOp * softCover(sdRRect(P.shadow, px, py), P.sigma);
        out[0] = float(P.sc[0] * a); out[1] = float(P.sc[1] * a); out[2] = float(P.sc[2] * a); out[3] = float(a);
    }
    // outside border: filled outer shape under the picture
    if (P.outside) {
        double a = cover(sdRRect(P.outer, px, py));
        if (a > 0) for (int c = 0; c < 4; c++) out[c] = float((c < 3 ? P.bc[c] : 1.f) * a + out[c] * (1 - a));
    }
    // picture
    double ds = sdRRect(P.shape, px, py);
    double aS = cover(ds);
    if (aS <= 0) return;
    float img[4] = {0, 0, 0, 0};
    if (P.content.w > 0 && P.content.h > 0) {
        double u = (px - P.content.x) / P.content.w, v = (py - P.content.y) / P.content.h;   // 0..1, v down
        if (u >= -0.0001 && u <= 1.0001 && v >= -0.0001 && v <= 1.0001) {
            double sx = P.srcX1 + u * (P.srcX2 - P.srcX1);
            double sy = P.srcY2 - v * (P.srcY2 - P.srcY1);   // flip: v down → y up
            sampleBilinear(src, sx, sy, P.srcX1, P.srcY1, P.srcX2, P.srcY2, img);
        }
    }
    // inside border ring replaces the picture's outer band
    double ring = 0;
    if (P.borderOn && !P.outside) ring = P.inner.valid ? clamp01(aS - cover(sdRRect(P.inner, px, py))) : aS;
    double aImg = aS - ring;    // picture coverage outside the ring
    float layer[4];
    for (int c = 0; c < 4; c++) layer[c] = float(img[c] * aImg + (c < 3 ? P.bc[c] : 1.f) * ring);
    double la = layer[3];
    for (int c = 0; c < 4; c++) out[c] = float(layer[c] + out[c] * (1 - la));
}

} // namespace framely
