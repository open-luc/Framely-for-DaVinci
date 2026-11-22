// Framely test harness: renders FramelyCore outside Resolve.
//   harness render <outdir>            → PNGs for portrait/square/landscape × 4K/HD/vertical, several setting presets
//   harness geom <cases.txt>           → prints geometry for each case line (compare against the mockup's geom())
//   harness one <out.png> FW FH iw ih [key=val ...]   → render one case
// Case line format: FW FH imgW imgH key=value ...   (keys = Settings field names; basis keys bSize.. take 0=screen 1=image)
#include "FramelyCore.h"
#include <zlib.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <thread>
#include <map>

using namespace framely;

// ---------- PNG writer ----------
static void be32(std::vector<unsigned char>& v, uint32_t x) { for (int i = 3; i >= 0; i--) v.push_back((x >> (8 * i)) & 255); }
static void chunk(FILE* f, const char* type, const std::vector<unsigned char>& data) {
    std::vector<unsigned char> b; be32(b, (uint32_t)data.size()); b.insert(b.end(), type, type + 4); b.insert(b.end(), data.begin(), data.end());
    uint32_t crc = crc32(0, b.data() + 4, (uInt)(b.size() - 4)); be32(b, crc); fwrite(b.data(), 1, b.size(), f);
}
static bool writePNG(const std::string& path, int w, int h, const std::vector<unsigned char>& rgba) {
    FILE* f = fopen(path.c_str(), "wb"); if (!f) return false;
    const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10}; fwrite(sig, 1, 8, f);
    std::vector<unsigned char> ihdr; be32(ihdr, w); be32(ihdr, h); ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); chunk(f, "IHDR", ihdr);
    std::vector<unsigned char> raw; raw.reserve((size_t)(w * 4 + 1) * h);
    for (int y = 0; y < h; y++) { raw.push_back(0); raw.insert(raw.end(), rgba.begin() + (size_t)y * w * 4, rgba.begin() + (size_t)(y + 1) * w * 4); }
    uLongf n = compressBound((uLong)raw.size()); std::vector<unsigned char> z(n);
    compress2(z.data(), &n, raw.data(), (uLong)raw.size(), 6); z.resize(n); chunk(f, "IDAT", z); chunk(f, "IEND", {});
    fclose(f); return true;
}

static bool readPNG(const std::string& path, int& w, int& h, int& bpp, std::vector<unsigned char>& out) {
    std::ifstream in(path, std::ios::binary); std::vector<unsigned char> d((std::istreambuf_iterator<char>(in)), {});
    if (d.size() < 8) return false;
    size_t i = 8; std::vector<unsigned char> idat; int ct = 0, bd = 8;
    auto u32 = [&](size_t o) { return (uint32_t)d[o] << 24 | (uint32_t)d[o + 1] << 16 | (uint32_t)d[o + 2] << 8 | d[o + 3]; };
    while (i + 8 <= d.size()) {
        uint32_t n = u32(i); std::string t((char*)&d[i + 4], 4);
        if (t == "IHDR") { w = u32(i + 8); h = u32(i + 12); bd = d[i + 16]; ct = d[i + 17]; }
        if (t == "IDAT") idat.insert(idat.end(), d.begin() + i + 8, d.begin() + i + 8 + n);
        i += 12 + n;
    }
    if (bd != 8 || (ct != 2 && ct != 6)) { fprintf(stderr, "unsupported png %d %d\n", bd, ct); return false; }
    bpp = ct == 2 ? 3 : 4; size_t stride = (size_t)w * bpp;
    std::vector<unsigned char> raw((stride + 1) * h); uLongf rl = raw.size();
    if (uncompress(raw.data(), &rl, idat.data(), idat.size()) != Z_OK) return false;
    out.assign(stride * h, 0); std::vector<unsigned char> prev(stride, 0);
    for (int y = 0; y < h; y++) {
        unsigned char f = raw[y * (stride + 1)]; unsigned char* L = &raw[y * (stride + 1) + 1];
        for (size_t x = 0; x < stride; x++) {
            int a = x >= (size_t)bpp ? L[x - bpp] : 0, b = prev[x], c = x >= (size_t)bpp ? prev[x - bpp] : 0;
            if (f == 1) L[x] += a; else if (f == 2) L[x] += b; else if (f == 3) L[x] += (a + b) / 2;
            else if (f == 4) { int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c); L[x] += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); }
        }
        memcpy(&out[y * stride], L, stride); memcpy(prev.data(), L, stride);
    }
    return true;
}

// ---------- test images ----------
struct Img { int w, h; std::vector<float> px; float* at(int x, int y) { return &px[((size_t)y * w + x) * 4]; } };   // y up, premult

static void hsl(double h, double s, double l, double& r, double& g, double& b) {
    auto f = [&](double n) { double k = std::fmod(n + h / 30, 12), a = s * std::min(l, 1 - l); return l - a * std::max(-1.0, std::min({k - 3, 9 - k, 1.0})); };
    r = f(0); g = f(8); b = f(4);
}
// Like the mockup's makeImage(): gradient, 10×10 grid, centre cross + circle, yellow corner brackets. Pixels are y-up.
static Img makeTest(int w, int h, double hue) {
    Img im{w, h, std::vector<float>((size_t)w * h * 4)};
    double m = std::min(w, h);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        double yd = h - 1 - y;                    // y down for drawing
        double t = ((x / (double)w) + (yd / (double)h)) / 2;
        double r1, g1, b1, r2, g2, b2; hsl(hue, .55, .52, r1, g1, b1); hsl(hue + 50, .60, .24, r2, g2, b2);
        double r = r1 + (r2 - r1) * t, g = g1 + (g2 - g1) * t, b = b1 + (b2 - b1) * t;
        double lw = std::max(1.0, m * .002);
        bool grid = false;
        for (int i = 1; i < 10; i++) if (std::fabs(x - w * i / 10.0) < lw / 2 + .5 || std::fabs(yd - h * i / 10.0) < lw / 2 + .5) grid = true;
        if (grid) { r = r * .78 + .22; g = g * .78 + .22; b = b * .78 + .22; }
        double cl = m * .004;
        bool cross = std::fabs(x - w / 2.0) < cl / 2 + .5 || std::fabs(yd - h / 2.0) < cl / 2 + .5;
        bool ring = std::fabs(std::hypot(x - w / 2.0, yd - h / 2.0) - m * .3) < cl / 2 + .5;
        if (cross || ring) { r = r * .4 + .6; g = g * .4 + .6; b = b * .4 + .6; }
        double L = m * .12, T = m * .025;
        auto inCorner = [&](double X, double Y) { return (X < L && Y < T) || (X < T && Y < L); };
        if (inCorner(x, yd) || inCorner(w - 1 - x, yd) || inCorner(x, h - 1 - yd) || inCorner(w - 1 - x, h - 1 - yd)) { r = 1; g = .82; b = .25; }
        float* p = im.at(x, y); p[0] = (float)r; p[1] = (float)g; p[2] = (float)b; p[3] = 1;
    }
    return im;
}
// Build what Resolve hands the plugin (assumed: timeline-size frame, image fitted & centred, transparent outside).
static Img fitIntoFrame(const Img& src, int FW, int FH, int& x1, int& y1, int& x2, int& y2) {
    Img f{FW, FH, std::vector<float>((size_t)FW * FH * 4, 0.f)};
    double s = std::min(FW / (double)src.w, FH / (double)src.h);
    double w = src.w * s, h = src.h * s;
    x1 = (int)std::lround((FW - w) / 2); x2 = (int)std::lround((FW + w) / 2); y1 = (int)std::lround((FH - h) / 2); y2 = (int)std::lround((FH + h) / 2);
    for (int y = y1; y < y2; y++) for (int x = x1; x < x2; x++) {
        double sx = (x + .5 - x1) / (x2 - x1) * src.w - .5, sy = (y + .5 - y1) / (y2 - y1) * src.h - .5;
        int ix = (int)std::floor(sx), iy = (int)std::floor(sy); double tx = sx - ix, ty = sy - iy;
        auto P = [&](int a, int b) { a = std::clamp(a, 0, src.w - 1); b = std::clamp(b, 0, src.h - 1); return &src.px[((size_t)b * src.w + a) * 4]; };
        for (int c = 0; c < 4; c++) { double t0 = P(ix, iy)[c] + (P(ix + 1, iy)[c] - P(ix, iy)[c]) * tx, t1 = P(ix, iy + 1)[c] + (P(ix + 1, iy + 1)[c] - P(ix, iy + 1)[c]) * tx;
            f.at(x, y)[c] = (float)(t0 + (t1 - t0) * ty); }
    }
    return f;
}
// Alpha bounding box (what the plugin will do to find the image inside the frame).
static void alphaBBox(Img& f, int& x1, int& y1, int& x2, int& y2) {
    x1 = f.w; y1 = f.h; x2 = -1; y2 = -1;
    for (int y = 0; y < f.h; y++) for (int x = 0; x < f.w; x++) if (f.at(x, y)[3] > .5f) {
        x1 = std::min(x1, x); y1 = std::min(y1, y); x2 = std::max(x2, x); y2 = std::max(y2, y); }
    if (x2 < 0) { x1 = 0; y1 = 0; x2 = f.w; y2 = f.h; } else { x2++; y2++; }
}

static void setKey(Settings& s, const std::string& k, const std::string& v) {
    double d = atof(v.c_str());
#define D(n) if (k == #n) { s.n = d; return; }
#define I(n) if (k == #n) { s.n = (int)d; return; }
#define B(n) if (k == #n) { s.n = d != 0; return; }
    I(bSize) I(bCrop) I(bShape) I(bBorder) I(bShadow)
    D(boxW) D(boxH) D(size) D(posX) D(posY) B(safeOn) D(safeT) D(safeB) D(safeL) D(safeR)
    D(cropL) D(cropR) D(cropT) D(cropB) D(zoom) D(panX) D(panY) D(corner) D(circle)
    B(borderOn) B(borderOutside) D(borderW) B(shadowOn) D(shOp) D(shDist) D(shSoft) D(shSize) D(shAng)
    if (k == "basis") { s.bSize = s.bCrop = s.bShape = s.bBorder = s.bShadow = (int)d; return; }
    if (k == "borderColor") { unsigned c = strtoul(v.c_str(), 0, 16); s.borderColor = {((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0}; return; }
    if (k == "shadowColor") { unsigned c = strtoul(v.c_str(), 0, 16); s.shadowColor = {((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0}; return; }
    fprintf(stderr, "unknown key %s\n", k.c_str());
}

// Render settings onto a mid-grey checker background and save.
static bool gBlackBg = false;
static std::vector<unsigned char> gLast; static int gLastW = 0, gLastH = 0;
static void renderCase(const std::string& out, int FW, int FH, int iw, int ih, double hue, const Settings& st, double k = 1.0) {
    Img test = makeTest(iw, ih, hue);
    int fx1, fy1, fx2, fy2; Img frame = fitIntoFrame(test, (int)std::lround(FW * k), (int)std::lround(FH * k), fx1, fy1, fx2, fy2);
    int bx1, by1, bx2, by2; alphaBBox(frame, bx1, by1, bx2, by2);
    Geom G = computeGeom(st, FW, FH, bx2 - bx1, by2 - by1);
    RenderPlan P = makePlan(st, G, FW, FH, k, bx1, by1, bx2, by2);
    SrcView sv{frame.px.data(), (long)frame.w * 16, 0, 0, frame.w, frame.h};
    int W = frame.w, H = frame.h;
    std::vector<unsigned char> rgba((size_t)W * H * 4);
    auto work = [&](int y0, int y1) {
        for (int y = y0; y < y1; y++) for (int x = 0; x < W; x++) {
            float o[4]; shadePixel(P, sv, x, y, o);
            bool chk = ((x / 32) + (y / 32)) & 1; float bg = gBlackBg ? 0.f : (chk ? .62f : .38f);   // checker shows transparency
            int row = H - 1 - y; unsigned char* d = &rgba[((size_t)row * W + x) * 4];
            for (int c = 0; c < 3; c++) d[c] = (unsigned char)std::lround(255 * std::clamp(o[c] + bg * (1 - o[3]), 0.f, 1.f));
            d[3] = 255;
        }
    };
    int nt = std::max(1u, std::thread::hardware_concurrency()); std::vector<std::thread> th;
    for (int i = 0; i < nt; i++) th.emplace_back(work, H * i / nt, H * (i + 1) / nt);
    for (auto& t : th) t.join();
    gLast = rgba; gLastW = W; gLastH = H;
    if (!out.empty()) writePNG(out, W, H, rgba);
}

static void printGeom(const Geom& G) {
    printf("s=%.6f W=%.4f H=%.4f cx=%.4f cy=%.4f shape=[%.4f %.4f %.4f %.4f r=%.4f] content=[%.4f %.4f %.4f %.4f] "
           "win=[%.4f %.4f %.4f %.4f] border=%.4f shadow=[%.4f %.4f %.4f %.4f] panPx=[%.4f %.4f] safeAdj=%d panLim=%d\n",
           G.s, G.W, G.H, G.cx, G.cy, G.shape.x0, G.shape.y0, G.shape.x1, G.shape.y1, G.shape.r,
           G.content.x, G.content.y, G.content.w, G.content.h, G.win.x0, G.win.y0, G.win.x1, G.win.y1, G.borderPx,
           G.shDx, G.shDy, G.shSoft, G.shSpread, G.panPxX, G.panPxY, (int)G.safeAdjusted, (int)G.panLimited);
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: harness render <dir> | geom <cases> | one <png> FW FH iw ih [k=v..]\n"); return 1; }
    std::string mode = argv[1];
    if (mode == "geom") {
        std::ifstream in(argv[2]); std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line); double FW, FH, iw, ih; ss >> FW >> FH >> iw >> ih;
            Settings st; std::string kv;
            while (ss >> kv) { auto e = kv.find('='); setKey(st, kv.substr(0, e), kv.substr(e + 1)); }
            printGeom(computeGeom(st, FW, FH, iw, ih));
        }
        return 0;
    }
    if (mode == "compare") {   // compare <resolve.png> <diff.png> FW FH iw ih hue [k=v..]
        Settings st; for (int i = 9; i < argc; i++) { std::string kv = argv[i]; auto e = kv.find('='); setKey(st, kv.substr(0, e), kv.substr(e + 1)); }
        gBlackBg = true;
        renderCase("", atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7]), atof(argv[8]), st, 1.0);
        int w, h, bpp; std::vector<unsigned char> R;
        if (!readPNG(argv[2], w, h, bpp, R) || w != gLastW || h != gLastH) { printf("FAIL cannot read/size mismatch %dx%d vs %dx%d\n", w, h, gLastW, gLastH); return 2; }
        double sum = 0; long bad = 0, n = (long)w * h; std::vector<unsigned char> diff((size_t)n * 4, 255);
        int bx[2][4] = {{w, h, -1, -1}, {w, h, -1, -1}};
        for (long i = 0; i < n; i++) {
            int md = 0, le = 0, lr = 0;
            for (int c = 0; c < 3; c++) { int a = gLast[i * 4 + c], b = R[i * bpp + c]; int dd = abs(a - b); sum += dd; md = std::max(md, dd); le += a; lr += b; }
            if (md > 48) bad++;
            for (int k = 0; k < 2; k++) if ((k ? lr : le) > 60 * 3) { int x = i % w, y = i / w; bx[k][0] = std::min(bx[k][0], x); bx[k][1] = std::min(bx[k][1], y); bx[k][2] = std::max(bx[k][2], x + 1); bx[k][3] = std::max(bx[k][3], y + 1); }
            unsigned char v = (unsigned char)std::min(255, md * 4); diff[i * 4] = v; diff[i * 4 + 1] = v; diff[i * 4 + 2] = v;
        }
        int bbErr = 0; for (int j = 0; j < 4; j++) bbErr = std::max(bbErr, abs(bx[0][j] - bx[1][j]));
        double mean = sum / (n * 3.0), badPct = 100.0 * bad / n;
        bool ok = mean < 3 && badPct < 0.5 && bbErr <= 2;
        printf("%s mean=%.2f bad=%.3f%% bboxErr=%dpx expect=[%d %d %d %d] resolve=[%d %d %d %d]\n", ok ? "PASS" : "FAIL", mean, badPct, bbErr,
               bx[0][0], bx[0][1], bx[0][2], bx[0][3], bx[1][0], bx[1][1], bx[1][2], bx[1][3]);
        if (!ok) writePNG(argv[3], w, h, diff);
        return ok ? 0 : 1;
    }
    if (mode == "testimg") {   // raw test images (what you'd import into Resolve)
        struct IM { const char* n; int w, h; double hue; } ims[] = {{"portrait", 1080, 1920, 200}, {"square", 1080, 1080, 140}, {"landscape", 1920, 1080, 275}};
        for (auto& im : ims) {
            Img t = makeTest(im.w, im.h, im.hue); std::vector<unsigned char> rgba((size_t)im.w * im.h * 4);
            for (int y = 0; y < im.h; y++) for (int x = 0; x < im.w; x++) for (int c = 0; c < 4; c++)
                rgba[((size_t)(im.h - 1 - y) * im.w + x) * 4 + c] = (unsigned char)std::lround(255 * std::clamp(t.at(x, y)[c], 0.f, 1.f));
            writePNG(std::string(argv[2]) + "/Framely_" + im.n + ".png", im.w, im.h, rgba);
        }
        return 0;
    }
    if (mode == "one") {
        Settings st; for (int i = 7; i < argc; i++) { std::string kv = argv[i]; auto e = kv.find('='); setKey(st, kv.substr(0, e), kv.substr(e + 1)); }
        double k = 1; for (int i = 7; i < argc; i++) if (!strncmp(argv[i], "k=", 2)) k = atof(argv[i] + 2);
        renderCase(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), 200, st, k);
        return 0;
    }
    if (mode == "render") {
        std::string dir = argv[2];
        struct TL { const char* n; int w, h; } tls[] = {{"4k", 3840, 2160}, {"hd", 1920, 1080}, {"vert", 2160, 3840}};
        struct IM { const char* n; int w, h; double hue; } ims[] = {{"portrait", 1080, 1920, 200}, {"square", 1080, 1080, 140}, {"landscape", 1920, 1080, 275}};
        std::map<std::string, std::string> presets = {
            {"default", ""},
            {"styled", "corner=30 borderOn=1 borderW=1.5 shadowOn=1 shDist=2 shSoft=4"},
            {"outside", "corner=100 borderOn=1 borderOutside=1 borderW=3 shadowOn=1 shOp=80 shDist=3 shSoft=6 shSize=2"},
            {"circle", "circle=100 corner=50 borderOn=1 borderW=2 size=50"},
            {"crop", "cropL=20 cropT=10 zoom=150 panX=10 panY=-5 corner=20 size=60 posX=-20 posY=10"},
            {"fitbox", "bSize=0 boxW=30 boxH=40 posX=25 borderOn=1"},
            {"safe", "safeOn=1 safeT=10 safeB=20 safeL=5 safeR=30 size=120 posX=40 borderOn=1 borderOutside=1 borderW=2"},
        };
        for (auto& tl : tls) for (auto& im : ims) for (auto& [pn, args] : presets) {
            Settings st; std::istringstream ss(args); std::string kv;
            while (ss >> kv) { auto e = kv.find('='); setKey(st, kv.substr(0, e), kv.substr(e + 1)); }
            double k = tl.w > 2000 || tl.h > 2000 ? 0.5 : 1.0;       // render 4K/vertical at half scale to keep files small (also tests render scale)
            std::string out = dir + "/" + pn + "_" + tl.n + "_" + im.n + ".png";
            renderCase(out, tl.w, tl.h, im.w, im.h, im.hue, st, k);
        }
        printf("done\n");
        return 0;
    }
    return 1;
}
