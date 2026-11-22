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
        double sx = (x + .5 - x1) / (x2 - x1) * src.w, sy = (y + .5 - y1) / (y2 - y1) * src.h;
        int ix = std::min(src.w - 1, (int)sx), iy = std::min(src.h - 1, (int)sy);
        memcpy(f.at(x, y), &src.px[((size_t)iy * src.w + ix) * 4], 16);
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
            bool chk = ((x / 32) + (y / 32)) & 1; float bg = chk ? .62f : .38f;   // checker shows transparency
            int row = H - 1 - y; unsigned char* d = &rgba[((size_t)row * W + x) * 4];
            for (int c = 0; c < 3; c++) d[c] = (unsigned char)std::lround(255 * std::clamp(o[c] + bg * (1 - o[3]), 0.f, 1.f));
            d[3] = 255;
        }
    };
    int nt = std::max(1u, std::thread::hardware_concurrency()); std::vector<std::thread> th;
    for (int i = 0; i < nt; i++) th.emplace_back(work, H * i / nt, H * (i + 1) / nt);
    for (auto& t : th) t.join();
    writePNG(out, W, H, rgba);
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
