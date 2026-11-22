// Framely – Picture in Picture · OFX plugin for DaVinci Resolve.
// Geometry/pixels: FramelyCore.h (port of the spec mockup). This file: parameters, defaults, render, viewer overlay.
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <thread>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#include "ofxsImageEffect.h"
#include "ofxsInteract.h"
#include "ofxsMultiThread.h"
#include "ofxsProcessing.H"
#include "ofxDrawSuite.h"
#include "FramelyCore.h"

using namespace OFX;
using framely::Settings;
using framely::Geom;

#define FRAMELY_VERSION "0.1.0"

// ============================================================ logging
static std::string homeDir() { const char* h = getenv("HOME"); return h ? h : "/tmp"; }
static void flog(const char* fmt, ...) {
    FILE* f = fopen((homeDir() + "/Library/Logs/Framely.log").c_str(), "a");
    if (!f) return;
    time_t t = time(nullptr); char ts[32]; strftime(ts, sizeof ts, "%m-%d %H:%M:%S", localtime(&t));
    fprintf(f, "[%s] ", ts);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

// ============================================================ parameter table
enum Kind { kDouble, kBool, kChoice, kRGB, kButton, kGroup };
struct PDef {
    const char* name; const char* label; Kind kind; const char* group;
    double def; double min, max;                  // min/max for doubles (Each image / fixed range)
    double smin, smax;                            // range when the group measures from the Screen (NaN = same)
    const char* hint;
    std::vector<const char*> opts;                // choice options
    double r = 0, g = 0, b = 0;                   // rgb default
    const char* basisGroup = nullptr;             // which basis choice changes this param's range
};
static const double NA = NAN;
static const char* SH = "% of the screen's shorter side";
static const char* PH = "% of the picture's shorter side";

// Groups (OFX group params), in panel order after the top rows.
struct GDef { const char* name; const char* label; bool open; };
static const std::vector<GDef> GROUPS = {
    {"grpGuides", "Guides", false},
    {"grpSize",   "Size & Position", true},
    {"grpCrop",   "Crop & Reframe", true},
    {"grpShape",  "Shape", true},
    {"grpBorder", "Border", true},
    {"grpShadow", "Drop Shadow", true},
};

static std::vector<PDef> makeParams() {
    std::vector<PDef> P;
    auto D = [&](const char* n, const char* l, const char* g, double def, double mn, double mx, const char* hint,
                 double smn = NA, double smx = NA, const char* bg = nullptr) {
        PDef d{n, l, kDouble, g, def, mn, mx, smn, smx, hint}; d.basisGroup = bg; P.push_back(d); };
    auto B = [&](const char* n, const char* l, const char* g, bool def, const char* hint) { P.push_back({n, l, kBool, g, def ? 1.0 : 0.0, 0, 1, NA, NA, hint}); };
    auto C = [&](const char* n, const char* l, const char* g, int def, std::vector<const char*> o, const char* hint) {
        PDef d{n, l, kChoice, g, (double)def, 0, (double)o.size() - 1, NA, NA, hint}; d.opts = o; P.push_back(d); };
    auto RGB = [&](const char* n, const char* l, const char* g, double r, double gg, double b, const char* hint) {
        PDef d{n, l, kRGB, g, 0, 0, 1, NA, NA, hint}; d.r = r; d.g = gg; d.b = b; P.push_back(d); };
    auto BTN = [&](const char* n, const char* l, const char* g, const char* hint) { P.push_back({n, l, kButton, g, 0, 0, 0, NA, NA, hint}); };
    const std::vector<const char*> SI = {"Screen", "Each image"};

    // ---- top of the panel
    BTN("saveDefault", "Save as Default", nullptr, "Saves every setting below. New copies of the effect start with them, and every reset goes back to them.");
    BTN("resetDefault", "Reset to Default", nullptr, "Puts every setting back to your saved default (or factory settings if you never saved one).");
    BTN("factory", "Factory Settings", nullptr, "Loads the built-in settings. Your saved default is not changed.");
    C("measureFrom", "Measure from", nullptr, 1, {"Screen", "Each image", "Custom mix"},
      "Screen: values are a share of the screen, so they come out the same size on screen for every clip. "
      "Each image: values are a share of each image, so every clip keeps the same proportions. "
      "Custom mix: each section's own setting decides.");

    // ---- Guides (viewer only, never rendered)
    B("guides", "Show Layout Guides", "grpGuides", true, "Viewer-only outlines showing where pictures of other shapes would land with these exact settings. Never appear in renders. Set the viewer's overlay to OpenFX Overlay to see them.");
    B("g916", "9:16", "grpGuides", true, ""); B("g45", "4:5", "grpGuides", true, ""); B("g11", "1:1", "grpGuides", true, "");
    B("g43", "4:3", "grpGuides", true, ""); B("g169", "16:9", "grpGuides", true, ""); B("gCustom", "Custom Ratio", "grpGuides", false, "");
    D("customW", "Custom Width", "grpGuides", 21, 1, 40, "Ratio width (e.g. 21 for 21:9)");
    D("customH", "Custom Height", "grpGuides", 9, 1, 40, "Ratio height (e.g. 9 for 21:9)");
    B("gCross", "Centre Crosshair", "grpGuides", true, ""); B("gDot", "Centre Dot", "grpGuides", true, "");
    B("gBox", "Box Outline", "grpGuides", true, "Shows the box in Fit in box mode.");
    B("gSafe", "Safe Area Outline", "grpGuides", true, "");
    B("gWarn", "\"No Room\" Warning", "grpGuides", true, "Red marks when the safe area holds the picture back from where Position asks.");
    B("gLabels", "Labels", "grpGuides", true, "");
    D("guideOp", "Guide Opacity", "grpGuides", 100, 10, 100, "% · how strongly the guides show");
    D("guideW", "Line Weight", "grpGuides", 2, 1, 6, "Thickness of the guide lines");
    BTN("resetGuides", "Reset Guides", "grpGuides", "Puts every Guides setting back to your saved default.");

    // ---- Size & Position
    C("bSize", "Size by", "grpSize", 1, {"Fit in box", "Scale"},
      "Fit in box: every image goes in the same box on screen, as large as it fits. Scale: every image gets the same scale, as a % of fitting the screen.");
    D("boxW", "Box Width", "grpSize", 40, 1, 100, "% of screen width");
    D("boxH", "Box Height", "grpSize", 45, 1, 100, "% of screen height");
    D("size", "Size", "grpSize", 90, 5, 200, "% · 100 = fits the screen");
    D("posX", "Position X", "grpSize", 0, -100, 100, "% of screen width from the centre point (the safe area's centre when that's on)");
    D("posY", "Position Y", "grpSize", 0, -100, 100, "% of screen height from the centre point · up is +");
    C("dragMode", "Viewer Drag Moves", "grpSize", 0, {"The picture", "The image inside it"}, "What dragging in the viewer (OpenFX Overlay) moves. Hold Option to do the other one.");
    B("safeOn", "Keep Inside Safe Area", "grpSize", false, "Shrinks or nudges the picture so it never leaves the safe area. Position is then measured from the safe area's centre.");
    D("safeT", "Safe Top", "grpSize", 5, 0, 95, "% of screen height kept clear at the top");
    D("safeB", "Safe Bottom", "grpSize", 5, 0, 95, "% of screen height kept clear at the bottom");
    D("safeL", "Safe Left", "grpSize", 5, 0, 95, "% of screen width kept clear on the left");
    D("safeR", "Safe Right", "grpSize", 5, 0, 95, "% of screen width kept clear on the right");
    BTN("resetSize", "Reset Size & Position", "grpSize", "Puts every Size & Position setting back to your saved default.");

    // ---- Crop & Reframe
    C("bCrop", "Crop & Pan Measured From", "grpCrop", 1, SI, "Screen: a % of the screen, like Resolve's Picture in Picture. Each image: a % of each image, so the same values frame every image the same way.");
    D("cropL", "Crop Left", "grpCrop", 0, 0, 49, "% of image width (or screen width)", 0, 50, "bCrop");
    D("cropR", "Crop Right", "grpCrop", 0, 0, 49, "% of image width (or screen width)", 0, 50, "bCrop");
    D("cropT", "Crop Top", "grpCrop", 0, 0, 49, "% of image height (or screen height)", 0, 50, "bCrop");
    D("cropB", "Crop Bottom", "grpCrop", 0, 0, 49, "% of image height (or screen height)", 0, 50, "bCrop");
    D("zoom", "Zoom", "grpCrop", 100, 100, 400, "% · enlarges the image inside the window without moving the window");
    D("panX", "Pan X", "grpCrop", 0, -50, 50, "% of image width (or screen width) · moves the image inside the window", -50, 50, "bCrop");
    D("panY", "Pan Y", "grpCrop", 0, -50, 50, "% of image height (or screen height) · up is +", -50, 50, "bCrop");
    BTN("resetCrop", "Reset Crop & Reframe", "grpCrop", "Puts every Crop & Reframe setting back to your saved default.");

    // ---- Shape
    C("bShape", "Corners Measured From", "grpShape", 1, SI, "Screen: corner size is the same on screen for every clip. Each image: Roundness is a % of each window (100 = fully rounded ends).");
    D("corner", "Roundness", "grpShape", 15, 0, 100, "Each image: % · 100 = fully rounded ends. Screen: % of the screen's shorter side, stops at fully round.", 0, 50, "bShape");
    D("circle", "Circle", "grpShape", 0, 0, 100, "% · pulls the long sides in · 100 = perfect circle");
    BTN("resetShape", "Reset Shape", "grpShape", "Puts every Shape setting back to your saved default.");

    // ---- Border
    B("borderOn", "Border", "grpBorder", false, "Turning the border on or off never moves or resizes the picture.");
    C("bBorder", "Border Measured From", "grpBorder", 1, SI, "Screen: same thickness on screen for every clip. Each image: thickness is a % of the picture.");
    D("borderW", "Border Width", "grpBorder", 1.2, 0, 25, PH, 0, 10, "bBorder");
    C("borderPos", "Placement", "grpBorder", 0, {"Inside the edge", "Outside the edge"}, "");
    RGB("borderColor", "Border Color", "grpBorder", 1, 1, 1, "");
    BTN("resetBorder", "Reset Border", "grpBorder", "Puts every Border setting back to your saved default.");

    // ---- Shadow
    B("shadowOn", "Drop Shadow", "grpShadow", false, "Turning the shadow on or off never moves or resizes the picture.");
    C("bShadow", "Shadow Measured From", "grpShadow", 1, SI, "Screen: same distance and softness on screen for every clip. Each image: a % of the picture, scales with it.");
    D("shOp", "Opacity", "grpShadow", 60, 0, 100, "%");
    D("shDist", "Distance", "grpShadow", 1.5, 0, 40, PH, 0, 20, "bShadow");
    D("shSoft", "Softness", "grpShadow", 2.5, 0, 40, PH, 0, 20, "bShadow");
    D("shSize", "Size", "grpShadow", 0, -30, 30, "+ grows, − shrinks", -15, 15, "bShadow");
    D("shAng", "Angle", "grpShadow", 315, 0, 360, "° · 0 = right, 90 = up");
    RGB("shadowColor", "Shadow Color", "grpShadow", 0, 0, 0, "");
    BTN("resetShadow", "Reset Drop Shadow", "grpShadow", "Puts every Drop Shadow setting back to your saved default.");
    return P;
}
static const std::vector<PDef>& PARAMS() { static std::vector<PDef> p = makeParams(); return p; }
static const PDef* findDef(const std::string& n) { for (auto& d : PARAMS()) if (n == d.name) return &d; return nullptr; }
static const char* BASIS_NAMES[] = {"bSize", "bCrop", "bShape", "bBorder", "bShadow"};
static const char* groupOfBasis(const std::string& b) {
    if (b == "bSize") return "grpSize"; if (b == "bCrop") return "grpCrop"; if (b == "bShape") return "grpShape";
    if (b == "bBorder") return "grpBorder"; return "grpShadow";
}

// ============================================================ saved defaults (a small text file: "name value [g b]")
using Values = std::map<std::string, std::vector<double>>;
static std::string defaultsPath() { return homeDir() + "/Library/Application Support/Framely/defaults.txt"; }
static Values factoryValues() {
    Values v;
    for (auto& d : PARAMS()) {
        if (d.kind == kButton) continue;
        if (d.kind == kRGB) v[d.name] = {d.r, d.g, d.b}; else v[d.name] = {d.def};
    }
    return v;
}
static bool loadSaved(Values& out) {
    std::ifstream in(defaultsPath()); if (!in) return false;
    out = factoryValues(); std::string line; bool any = false;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line); std::string n; ss >> n; std::vector<double> vals; double x;
        while (ss >> x) vals.push_back(x);
        auto it = out.find(n);
        if (it != out.end() && vals.size() == it->second.size()) { it->second = vals; any = true; }
    }
    return any;
}
static Values currentDefaults() { Values v; if (!loadSaved(v)) v = factoryValues(); return v; }
static bool writeSaved(const Values& v) {
    std::string dir = homeDir() + "/Library/Application Support/Framely";
    mkdir(dir.c_str(), 0755);
    std::ofstream out(defaultsPath()); if (!out) return false;
    out << "# Framely – Picture in Picture saved default (written by Save as Default). name value\n";
    char buf[64];
    for (auto& [n, vals] : v) { out << n; for (double x : vals) { snprintf(buf, sizeof buf, " %.6g", x); out << buf; } out << "\n"; }
    return true;
}

// ============================================================ image area detection
// The source is the timeline-size frame with the image already fitted & centred and alpha 0 around it (verified in Resolve 21.2).
struct Area { int x1, y1, x2, y2; };
static Area alphaArea(Image* src) {
    OfxRectI b = src->getBounds();
    int W = b.x2 - b.x1, H = b.y2 - b.y1;
    unsigned nt = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<Area> part(nt, Area{b.x2, b.y2, b.x1 - 1, b.y1 - 1});
    std::vector<std::thread> th;
    for (unsigned t = 0; t < nt; t++) th.emplace_back([&, t]() {
        Area a = part[t];
        for (int y = b.y1 + H * t / nt; y < b.y1 + H * (t + 1) / nt; y++) {
            const float* row = (const float*)src->getPixelAddress(b.x1, y); if (!row) continue;
            int first = -1, last = -1;
            for (int x = 0; x < W; x++) if (row[x * 4 + 3] > 0.5f) { first = x; break; }
            if (first < 0) continue;
            for (int x = W - 1; x >= first; x--) if (row[x * 4 + 3] > 0.5f) { last = x; break; }
            a.x1 = std::min(a.x1, b.x1 + first); a.x2 = std::max(a.x2, b.x1 + last); a.y1 = std::min(a.y1, y); a.y2 = std::max(a.y2, y);
        }
        part[t] = a;
    });
    for (auto& t : th) t.join();
    Area r{b.x2, b.y2, b.x1 - 1, b.y1 - 1};
    for (auto& a : part) { r.x1 = std::min(r.x1, a.x1); r.y1 = std::min(r.y1, a.y1); r.x2 = std::max(r.x2, a.x2); r.y2 = std::max(r.y2, a.y2); }
    if (r.x2 < r.x1 || r.y2 < r.y1) return {b.x1, b.y1, b.x2, b.y2};
    return {r.x1, r.y1, r.x2 + 1, r.y2 + 1};
}

// ============================================================ render processor
class FramelyProcessor : public ImageProcessor {
public:
    framely::RenderPlan plan; framely::SrcView sv; int ox = 0, oy = 0;   // ox/oy: frame origin in pixel coords
    explicit FramelyProcessor(ImageEffect& e) : ImageProcessor(e) {}
    void multiThreadProcessImages(OfxRectI win) override {
        for (int y = win.y1; y < win.y2; ++y) {
            if (_effect.abort()) break;
            float* d = (float*)_dstImg->getPixelAddress(win.x1, y);
            if (!d) continue;
            for (int x = win.x1; x < win.x2; ++x, d += 4) framely::shadePixel(plan, sv, x - ox, y - oy, d);
        }
    }
};

// ============================================================ the effect
class FramelyPlugin : public ImageEffect {
public:
    Clip *srcClip, *dstClip;
    std::map<std::string, Param*> params;
    BooleanParam* initialized;
    bool inChange = false;
    // last seen frame & image area (full-res units) for basis conversion, pan normalising and the overlay
    std::mutex mx; double lastFW = 0, lastFH = 0, lastIW = 0, lastIH = 0;

    explicit FramelyPlugin(OfxImageEffectHandle h) : ImageEffect(h) {
        srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        dstClip = fetchClip(kOfxImageEffectOutputClipName);
        for (auto& d : PARAMS()) {
            switch (d.kind) {
                case kDouble: params[d.name] = fetchDoubleParam(d.name); break;
                case kBool:   params[d.name] = fetchBooleanParam(d.name); break;
                case kChoice: params[d.name] = fetchChoiceParam(d.name); break;
                case kRGB:    params[d.name] = fetchRGBParam(d.name); break;
                case kButton: params[d.name] = fetchPushButtonParam(d.name); break;
                default: break;
            }
        }
        initialized = fetchBooleanParam("initialized");
        flog("createInstance: pinStamp=%.0f initialized=%d corner=%.2f borderOn=%d | cropL=%g cropR=%g cropT=%g cropB=%g panX=%g panY=%g zoom=%g shDist=%g shSoft=%g", (int)initialized->getValue(), dp("corner")->getValue(), (int)bp("borderOn")->getValue(),
             dp("cropL")->getValue(), dp("cropR")->getValue(), dp("cropT")->getValue(), dp("cropB")->getValue(), dp("panX")->getValue(), dp("panY")->getValue(), dp("zoom")->getValue(), dp("shDist")->getValue(), dp("shSoft")->getValue());

        // A brand-new copy of the effect starts from the saved default (the file may be newer than this Resolve session's describe).
        if (!initialized->getValue()) {
            inChange = true;
            Values v; if (loadSaved(v)) { applyValues(v, false); pending = true; }
            initialized->setValue(true);
            inChange = false;
        }
        updateVisibility();
        flog("createInstance (initialized was applied), version " FRAMELY_VERSION);
    }

    DoubleParam* dp(const char* n) { return (DoubleParam*)params[n]; }
    BooleanParam* bp(const char* n) { return (BooleanParam*)params[n]; }
    ChoiceParam* cp(const char* n) { return (ChoiceParam*)params[n]; }
    RGBParam* rp(const char* n) { return (RGBParam*)params[n]; }
    double dv(const char* n, double t) { return dp(n)->getValueAtTime(t); }
    bool bv(const char* n, double t) { return bp(n)->getValueAtTime(t); }
    int ci(const char* n, double t) { int v; cp(n)->getValueAtTime(t, v); return v; }

    Settings settingsAt(double t) {
        Settings s;
        s.bSize = ci("bSize", t) == 0 ? framely::kScreen : framely::kImage;
        s.bCrop = ci("bCrop", t); s.bShape = ci("bShape", t); s.bBorder = ci("bBorder", t); s.bShadow = ci("bShadow", t);
        s.boxW = dv("boxW", t); s.boxH = dv("boxH", t); s.size = dv("size", t); s.posX = dv("posX", t); s.posY = dv("posY", t);
        s.safeOn = bv("safeOn", t); s.safeT = dv("safeT", t); s.safeB = dv("safeB", t); s.safeL = dv("safeL", t); s.safeR = dv("safeR", t);
        s.cropL = dv("cropL", t); s.cropR = dv("cropR", t); s.cropT = dv("cropT", t); s.cropB = dv("cropB", t);
        s.zoom = dv("zoom", t); s.panX = dv("panX", t); s.panY = dv("panY", t);
        s.corner = dv("corner", t); s.circle = dv("circle", t);
        s.borderOn = bv("borderOn", t); s.borderOutside = ci("borderPos", t) == 1; s.borderW = dv("borderW", t);
        double r, g, b; rp("borderColor")->getValueAtTime(t, r, g, b); s.borderColor = {r, g, b};
        s.shadowOn = bv("shadowOn", t); s.shOp = dv("shOp", t); s.shDist = dv("shDist", t); s.shSoft = dv("shSoft", t);
        s.shSize = dv("shSize", t); s.shAng = dv("shAng", t);
        rp("shadowColor")->getValueAtTime(t, r, g, b); s.shadowColor = {r, g, b};
        // Clamp to the active ranges (Resolve may not apply runtime range changes)
        auto cl = [&](double& v, const char* n, int basis) {
            const PDef* d = findDef(n); double mn = d->min, mxv = d->max;
            if (basis == framely::kScreen && !std::isnan(d->smin)) { mn = d->smin; mxv = d->smax; }
            v = framely::clampd(v, mn, mxv);
        };
        cl(s.cropL, "cropL", s.bCrop); cl(s.cropR, "cropR", s.bCrop); cl(s.cropT, "cropT", s.bCrop); cl(s.cropB, "cropB", s.bCrop);
        cl(s.corner, "corner", s.bShape); cl(s.borderW, "borderW", s.bBorder);
        cl(s.shDist, "shDist", s.bShadow); cl(s.shSoft, "shSoft", s.bShadow); cl(s.shSize, "shSize", s.bShadow);
        return s;
    }

    void frameSize(double t, double& FW, double& FH) {
        OfxRectD r = srcClip->getRegionOfDefinition(t);
        FW = r.x2 - r.x1; FH = r.y2 - r.y1;
        if (FW <= 0 || FH <= 0) { FW = 1920; FH = 1080; }
    }
    void imageAspect(double t, double& FW, double& FH, double& IW, double& IH) {
        frameSize(t, FW, FH);
        std::lock_guard<std::mutex> l(mx);
        if (lastIW > 0 && std::fabs(lastFW - FW) < 1 && std::fabs(lastFH - FH) < 1) { IW = lastIW; IH = lastIH; }
        else { IW = FW; IH = FH; }
    }

    // ---------------- render
    void render(const RenderArguments& args) override {
        std::unique_ptr<Image> dst(dstClip->fetchImage(args.time));
        std::unique_ptr<Image> src(srcClip->fetchImage(args.time));
        if (!dst) return;
        if (!src) { // nothing to draw
            OfxRectI w = args.renderWindow;
            for (int y = w.y1; y < w.y2; y++) { float* d = (float*)dst->getPixelAddress(w.x1, y); if (d) memset(d, 0, sizeof(float) * 4 * (w.x2 - w.x1)); }
            return;
        }
        if (src->getPixelDepth() != eBitDepthFloat || src->getPixelComponents() != ePixelComponentRGBA) throwSuiteStatusException(kOfxStatErrUnsupported);
        const double k = args.renderScale.x;
        OfxRectD rod = srcClip->getRegionOfDefinition(args.time);
        const double FW = rod.x2 - rod.x1, FH = rod.y2 - rod.y1;
        Area a = alphaArea(src.get());
        OfxRectI sb = src->getBounds();
        const int ox = (int)std::lround(rod.x1 * k), oy = (int)std::lround(rod.y1 * k);
        const double IW = (a.x2 - a.x1) / k, IH = (a.y2 - a.y1) / k;
        { std::lock_guard<std::mutex> l(mx); lastFW = FW; lastFH = FH; lastIW = IW; lastIH = IH; }
        Settings st = settingsAt(args.time);
        Geom G = framely::computeGeom(st, FW, FH, IW, IH);
        FramelyProcessor p(*this);
        p.plan = framely::makePlan(st, G, FW, FH, k, a.x1 - ox, a.y1 - oy, a.x2 - ox, a.y2 - oy);
        p.sv.data = (const float*)src->getPixelAddress(sb.x1, sb.y1);
        p.sv.rowBytes = src->getRowBytes(); p.sv.x1 = sb.x1 - ox; p.sv.y1 = sb.y1 - oy; p.sv.x2 = sb.x2 - ox; p.sv.y2 = sb.y2 - oy;
        p.ox = ox; p.oy = oy;
        p.setDstImg(dst.get());
        p.setRenderWindow(args.renderWindow);
        p.process();
    }
    bool isIdentity(const IsIdentityArguments&, Clip*&, double&) override {
        return false; }

    // ---------------- values in/out
    Values readValues() {
        Values v;
        for (auto& d : PARAMS()) {
            switch (d.kind) {
                case kDouble: v[d.name] = {dp(d.name)->getValue()}; break;
                case kBool:   v[d.name] = {bp(d.name)->getValue() ? 1.0 : 0.0}; break;
                case kChoice: { int c; cp(d.name)->getValue(c); v[d.name] = {(double)c}; break; }
                case kRGB:    { double r, g, b; rp(d.name)->getValue(r, g, b); v[d.name] = {r, g, b}; break; }
                default: break;
            }
        }
        return v;
    }
    void setOne(const PDef& d, const std::vector<double>& x) {
        switch (d.kind) {
            case kDouble: if (dp(d.name)->getNumKeys() == 0) dp(d.name)->setValue(x[0]); break;
            case kBool:   bp(d.name)->setValue(x[0] != 0); break;
            case kChoice: cp(d.name)->setValue((int)x[0]); break;
            case kRGB:    rp(d.name)->setValue(x[0], x[1], x[2]); break;
            default: break;
        }
    }
    // group == nullptr: everything
    void applyValues(const Values& v, bool edit, const char* group = nullptr) {
        if (edit) beginEditBlock("Framely reset");
        for (auto& d : PARAMS()) {
            if (d.kind == kButton) continue;
            if (group && (!d.group || strcmp(d.group, group) != 0)) continue;
            if (!strcmp(d.name, "measureFrom") || !strcmp(d.name, "dragMode")) { if (group) continue; }
            auto it = v.find(d.name); if (it != v.end()) setOne(d, it->second);
        }
        if (edit) endEditBlock();
        updateMeasureFrom(); updateRanges(); updateVisibility();
    }

    // ---------------- panel behaviour
    void updateVisibility() {
        bool fit = ci("bSize", 0) == 0, safe = bp("safeOn")->getValue(), guides = bp("guides")->getValue(), custom = bp("gCustom")->getValue();
        dp("boxW")->setIsSecret(!fit); dp("boxH")->setIsSecret(!fit); dp("size")->setIsSecret(fit);
        for (const char* n : {"safeT", "safeB", "safeL", "safeR"}) dp(n)->setIsSecret(!safe);
        for (const char* n : {"g916", "g45", "g11", "g43", "g169", "gCustom", "gCross", "gDot", "gBox", "gSafe", "gWarn", "gLabels"}) bp(n)->setIsSecret(!guides);
        dp("guideOp")->setIsSecret(!guides); dp("guideW")->setIsSecret(!guides);
        dp("customW")->setIsSecret(!(guides && custom)); dp("customH")->setIsSecret(!(guides && custom));
        dp("corner")->setLabel(ci("bShape", 0) == 0 ? "Corner Size" : "Roundness");
    }
    void updateRanges() {
        for (auto& d : PARAMS()) {
            if (d.kind != kDouble || !d.basisGroup) continue;
            bool screen = ci(d.basisGroup, 0) == 0;
            double mn = screen && !std::isnan(d.smin) ? d.smin : d.min, mxv = screen && !std::isnan(d.smax) ? d.smax : d.max;
            dp(d.name)->setRange(mn, mxv); dp(d.name)->setDisplayRange(mn, mxv);
        }
        // With the safe area on, Position stops at the safe area's edges.
        if (bp("safeOn")->getValue()) {
            double hx = std::max(.5, (100 - dp("safeL")->getValue() - dp("safeR")->getValue()) / 2);
            double hy = std::max(.5, (100 - dp("safeT")->getValue() - dp("safeB")->getValue()) / 2);
            dp("posX")->setRange(-hx, hx); dp("posX")->setDisplayRange(-hx, hx);
            dp("posY")->setRange(-hy, hy); dp("posY")->setDisplayRange(-hy, hy);
            if (dp("posX")->getNumKeys() == 0) { double v = dp("posX")->getValue(); if (std::fabs(v) > hx) dp("posX")->setValue(framely::clampd(v, -hx, hx)); }
            if (dp("posY")->getNumKeys() == 0) { double v = dp("posY")->getValue(); if (std::fabs(v) > hy) dp("posY")->setValue(framely::clampd(v, -hy, hy)); }
        } else {
            dp("posX")->setRange(-100, 100); dp("posX")->setDisplayRange(-100, 100);
            dp("posY")->setRange(-100, 100); dp("posY")->setDisplayRange(-100, 100);
        }
    }
    void updateMeasureFrom() {
        int first = -1; bool mixed = false;
        for (const char* b : BASIS_NAMES) { int v = ci(b, 0); if (first < 0) first = v; else if (v != first) mixed = true; }
        int want = mixed ? 2 : first;   // bSize: 0 = Fit in box (screen), 1 = Scale (image) — same numbering
        if (ci("measureFrom", 0) != want) cp("measureFrom")->setValue(want);
    }
    // Convert one group's values when it switches between Screen and Each image, exact for the image currently shown.
    void convertGroup(const std::string& basis, int from, int to, double t) {
        if (from == to) return;
        double FW, FH, IW, IH; imageAspect(t, FW, FH, IW, IH);
        Settings s = settingsAt(t);
        if (basis == "bSize") s.bSize = from; else if (basis == "bCrop") s.bCrop = from; else if (basis == "bShape") s.bShape = from;
        else if (basis == "bBorder") s.bBorder = from; else s.bShadow = from;
        Geom G = framely::computeGeom(s, FW, FH, IW, IH);
        auto set = [&](const char* n, double v) { if (dp(n)->getNumKeys() == 0) dp(n)->setValue(v); };
        if (basis == "bSize") {
            if (to == framely::kScreen) { set("boxW", G.win.w / FW * 100); set("boxH", G.win.h / FH * 100);
                set("posX", (G.win.cx - G.originX) / FW * 100); set("posY", -(G.win.cy - G.originY) / FH * 100); }
            else { set("size", G.s * 100); set("posX", (G.cx - G.originX) / FW * 100); set("posY", -(G.cy - G.originY) / FH * 100); }
        } else if (basis == "bCrop") {
            if (to == framely::kScreen) { set("cropL", G.cl / FW * 100); set("cropR", G.cr / FW * 100); set("cropT", G.ct / FH * 100); set("cropB", G.cb / FH * 100);
                set("panX", G.panPxX / (FW * G.s) * 100); set("panY", G.panPxY / (FH * G.s) * 100); }
            else { set("cropL", G.cl / G.refW * 100); set("cropR", G.cr / G.refW * 100); set("cropT", G.ct / G.refH * 100); set("cropB", G.cb / G.refH * 100);
                set("panX", G.panPxX / G.W * 100); set("panY", G.panPxY / G.H * 100); }
        } else if (basis == "bShape") {
            set("corner", to == framely::kScreen ? G.cornerPx / G.Ss * 100 : std::min(100.0, G.cornerPx / std::max(1e-9, G.half0) * 100));
        } else {
            std::vector<const char*> keys = basis == "bBorder" ? std::vector<const char*>{"borderW"} : std::vector<const char*>{"shDist", "shSoft", "shSize"};
            for (const char* key : keys) {
                double v = dp(key)->getValueAtTime(t);
                double px = from == framely::kScreen ? v / 100 * G.Ss : v / 100 * G.winShort;
                set(key, to == framely::kScreen ? px / G.Ss * 100 : px / std::max(1e-9, G.winShort) * 100);
            }
        }
    }
    void normalizePan(double t) {
        if (dp("panX")->getNumKeys() || dp("panY")->getNumKeys() || dp("zoom")->getNumKeys()) return;
        double FW, FH, IW, IH; imageAspect(t, FW, FH, IW, IH);
        Geom G = framely::computeGeom(settingsAt(t), FW, FH, IW, IH);
        double px, py; framely::normalizedPan(G, px, py);
        if (std::fabs(px - dp("panX")->getValue()) > 1e-4) dp("panX")->setValue(px);
        if (std::fabs(py - dp("panY")->getValue()) > 1e-4) dp("panY")->setValue(py);
    }

    std::map<std::string, int> lastBasis;
    void changedParam(const InstanceChangedArgs& args, const std::string& name) override {
        flog("changedParam %s reason=%d", name.c_str(), (int)args.reason);
        if (args.reason == eChangeUserEdit && !inChange) pinAll("first user edit");
        const bool isBasis = name == "bSize" || name == "bCrop" || name == "bShape" || name == "bBorder" || name == "bShadow";
        if (inChange || args.reason != eChangeUserEdit) {
            // our own or host edits (resets, undo): remember the basis so the next user switch converts from the right units
            if (isBasis) lastBasis[name] = ci(name.c_str(), args.time);
            if (!inChange && (isBasis || name == "safeOn" || name == "guides" || name == "gCustom")) { inChange = true; updateRanges(); updateVisibility(); inChange = false; }
            return;
        }
        inChange = true;
        const double t = args.time;
        try {
            if (name == "saveDefault") {
                bool ok = writeSaved(readValues());
                sendMessage(Message::eMessageMessage, "", ok ? "Saved. New copies of the effect start with these settings, and every reset goes back to them."
                                                              : "Couldn't save the default (no write access to ~/Library/Application Support/Framely).");
            } else if (name == "resetDefault") {
                applyValues(currentDefaults(), true);
            } else if (name == "factory") {
                applyValues(factoryValues(), true);
            } else if (name == "resetGuides" || name == "resetSize" || name == "resetCrop" || name == "resetShape" || name == "resetBorder" || name == "resetShadow") {
                static const std::map<std::string, const char*> g = {{"resetGuides", "grpGuides"}, {"resetSize", "grpSize"}, {"resetCrop", "grpCrop"},
                    {"resetShape", "grpShape"}, {"resetBorder", "grpBorder"}, {"resetShadow", "grpShadow"}};
                applyValues(currentDefaults(), true, g.at(name));
            } else if (name == "measureFrom") {
                int m = ci("measureFrom", t);
                if (m != 2) {
                    beginEditBlock("Measure from");
                    for (const char* b : BASIS_NAMES) { int from = ci(b, t); if (from != m) { convertGroup(b, from, m, t); cp(b)->setValue(m); } lastBasis[b] = m; }
                    endEditBlock();
                }
                updateMeasureFrom(); updateRanges(); updateVisibility();
            } else if (name == "bSize" || name == "bCrop" || name == "bShape" || name == "bBorder" || name == "bShadow") {
                int to = ci(name.c_str(), t);
                auto it = lastBasis.find(name);
                int from = it != lastBasis.end() ? it->second : 1 - to;   // two options: the previous value is the other one
                convertGroup(name, from, to, t);
                lastBasis[name] = to;
                updateMeasureFrom(); updateRanges(); updateVisibility();
            } else if (name == "panX" || name == "panY" || name == "zoom") {
                normalizePan(t);
            } else if (name == "safeOn" || name == "safeT" || name == "safeB" || name == "safeL" || name == "safeR") {
                updateRanges(); updateVisibility();
            } else if (name == "guides" || name == "gCustom") {
                updateVisibility();
            }
        } catch (...) { inChange = false; throw; }
        inChange = false;
    }
    // Resolve only saves values it saw change through its own UI flow (verified 2026-11-21: values set in
    // createInstance/getClipPreferences/instanceChanged-from-createInstance were NOT saved). So values applied from the
    // saved default are applied again ("pinned") at the first moment the user works with this copy of the effect.
    bool pending = false;
    void pinAll(const char* why) {
        if (!pending) return;
        pending = false;
        bool was = inChange; inChange = true;
        try {
            Values v; if (loadSaved(v)) for (auto& d : PARAMS()) { auto it = v.find(d.name); if (it != v.end() && d.kind != kButton) setOne(d, it->second); }
            initialized->setValue(true);
            dp("pinStamp")->setValue((double)time(nullptr));
            flog("pinAll(%s) done", why);
        } catch (...) { flog("pinAll(%s) threw", why); }
        inChange = was;
    }
    void beginEdit() override { flog("beginEdit (UI opened)"); pinAll("beginEdit"); }
    void endEdit() override { flog("endEdit"); }
    void syncPrivateData() override {
        flog("syncPrivateData: corner=%.2f borderOn=%d", dp("corner")->getValue(), (int)bp("borderOn")->getValue()); }
};

// ============================================================ viewer overlay: guides + dragging
static const OfxDrawSuiteV1* gDraw = nullptr;

class FramelyInteract : public OverlayInteract {
    FramelyPlugin* fx;
    bool dragging = false, dragImage = false, altDown = false;
    OfxPointD start{0, 0}; double s0 = 0, s1 = 0;
public:
    FramelyInteract(OfxInteractHandle h, ImageEffect* e) : OverlayInteract(h), fx((FramelyPlugin*)e) {
        for (auto& d : PARAMS()) if (d.kind != kButton) addParamToSlaveTo(fx->params[d.name]);
    }
    // y-down screen (mockup) → OFX canonical (y up)
    struct Ctx { OfxDrawContextHandle c; double FH, u, alpha, wt; };
    void col(Ctx& X, double r, double g, double b, double a = 1) { OfxRGBAColourF c{(float)r, (float)g, (float)b, (float)(a * X.alpha)}; gDraw->setColour(X.c, &c); }
    void poly(Ctx& X, const std::vector<OfxPointD>& pts, OfxDrawPrimitive prim) {
        std::vector<OfxPointD> q(pts); for (auto& p : q) p.y = X.FH - p.y;
        gDraw->draw(X.c, prim, q.data(), (int)q.size());
    }
    std::vector<OfxPointD> rrectPts(const framely::RRect& R) {
        std::vector<OfxPointD> p; double r = std::min({R.r, (R.x1 - R.x0) / 2, (R.y1 - R.y0) / 2});
        const int N = 12;
        auto arc = [&](double cx, double cy, double a0) { for (int i = 0; i <= N; i++) { double a = a0 + i * M_PI / 2 / N; p.push_back({cx + r * cos(a), cy + r * sin(a)}); } };
        arc(R.x1 - r, R.y1 - r, 0); arc(R.x0 + r, R.y1 - r, M_PI / 2); arc(R.x0 + r, R.y0 + r, M_PI); arc(R.x1 - r, R.y0 + r, 3 * M_PI / 2);
        return p;
    }
    // dark halo under a coloured line so it reads on any background
    void haloLoop(Ctx& X, const std::vector<OfxPointD>& pts, double r, double g, double b, OfxDrawLineStipplePattern st, OfxDrawPrimitive prim = kOfxDrawPrimitiveLineLoop) {
        gDraw->setLineStipple(X.c, st);
        gDraw->setLineWidth(X.c, (float)(4.5 * X.wt)); col(X, 0, 0, 0, .7); poly(X, pts, prim);
        gDraw->setLineWidth(X.c, (float)(2 * X.wt)); col(X, r, g, b); poly(X, pts, prim);
        gDraw->setLineStipple(X.c, kOfxDrawLineStipplePatternSolid);
    }
    void label(Ctx& X, bool labels, const char* text, double x, double y, double r, double g, double b, bool always = false) {
        if (!labels && !always) return;
        OfxPointD p{x, X.FH - y}; col(X, r, g, b);
        gDraw->drawText(X.c, text, &p, kOfxDrawTextAlignmentLeft | kOfxDrawTextAlignmentTop);
    }

    bool draw(const DrawArgs& a) override {
        static int nd = 0; if (nd++ < 3) flog("overlay draw: drawContext=%p suite=%p", a.drawContext, (void*)gDraw);
        if (!gDraw || !a.drawContext) return false;
        const double t = a.time;
        if (!fx->bv("guides", t)) return true;
        double FW, FH, IW, IH; fx->imageAspect(t, FW, FH, IW, IH);
        Settings S = fx->settingsAt(t);
        Geom G = framely::computeGeom(S, FW, FH, IW, IH);
        Ctx X{(OfxDrawContextHandle)a.drawContext, FH, a.pixelScale.x, fx->dv("guideOp", t) / 100, fx->dv("guideW", t) / 2};
        const double u = X.u; const bool labels = fx->bv("gLabels", t);
        // Box
        if (fx->bv("gBox", t) && S.bSize == framely::kScreen) {
            double bw = S.boxW / 100 * FW, bh = S.boxH / 100 * FH, bx = G.anchorX - bw / 2, by = G.anchorY - bh / 2;
            haloLoop(X, {{bx, by}, {bx + bw, by}, {bx + bw, by + bh}, {bx, by + bh}}, 1, 1, 1, kOfxDrawLineStipplePatternDash);
            label(X, labels, "Box", bx + 5 * u, by + 5 * u, 1, 1, 1);
        }
        // Safe area
        if (G.hasSafe && fx->bv("gSafe", t)) {
            auto& s = G.safe;
            haloLoop(X, {{s.x0, s.y0}, {s.x1, s.y0}, {s.x1, s.y1}, {s.x0, s.y1}}, .91, .64, .24, kOfxDrawLineStipplePatternDot);
            label(X, labels, "Safe area", s.x0 + 5 * u, s.y1 - 24 * u, .91, .64, .24);
        }
        // Ghost outlines: where other picture shapes would land
        struct Gh { const char* key; const char* lab; double w, h, r, g, b; };
        static const Gh GH[] = {{"g916", "9:16", 9, 16, .31, .82, 1}, {"g45", "4:5", 4, 5, 1, .82, .4}, {"g11", "1:1", 1, 1, .49, .95, .61},
                                {"g43", "4:3", 4, 3, 1, .62, .42}, {"g169", "16:9", 16, 9, 1, .42, .84}};
        int gi = 0;
        auto ghost = [&](const char* lab, double w, double h, double r, double g, double b) {
            Geom gg = framely::computeGeom(S, FW, FH, w * 1000, h * 1000);
            haloLoop(X, rrectPts(gg.shape), r, g, b, kOfxDrawLineStipplePatternDash);
            label(X, labels, lab, gg.shape.x0 + 5 * u, gg.shape.y0 + (5 + gi * 20) * u, r, g, b);
            gi++;
        };
        for (auto& gh : GH) if (fx->bv(gh.key, t)) ghost(gh.lab, gh.w, gh.h, gh.r, gh.g, gh.b);
        if (fx->bv("gCustom", t)) {
            char buf[32]; snprintf(buf, sizeof buf, "%g:%g", std::round(fx->dv("customW", t) * 10) / 10, std::round(fx->dv("customH", t) * 10) / 10);
            ghost(buf, fx->dv("customW", t), fx->dv("customH", t), .79, .65, 1);
        }
        // "No room" warning
        double reqX = G.anchorX, reqY = G.anchorY, realX = G.win.cx, realY = G.win.cy;
        if (fx->bv("gWarn", t) && G.hasSafe && std::hypot(realX - reqX, realY - reqY) > 1) {
            auto& s = G.safe; std::vector<std::pair<std::string, std::vector<OfxPointD>>> edges;
            if (reqY > realY + .5 && std::fabs(G.win.y1 - s.y1) < 1) edges.push_back({"lower", {{s.x0, s.y1}, {s.x1, s.y1}}});
            if (reqY < realY - .5 && std::fabs(G.win.y0 - s.y0) < 1) edges.push_back({"higher", {{s.x0, s.y0}, {s.x1, s.y0}}});
            if (reqX > realX + .5 && std::fabs(G.win.x1 - s.x1) < 1) edges.push_back({"right", {{s.x1, s.y0}, {s.x1, s.y1}}});
            if (reqX < realX - .5 && std::fabs(G.win.x0 - s.x0) < 1) edges.push_back({"left", {{s.x0, s.y0}, {s.x0, s.y1}}});
            for (auto& e : edges) {
                gDraw->setLineWidth(X.c, 7); col(X, 0, 0, 0, .7); poly(X, e.second, kOfxDrawPrimitiveLines);
                gDraw->setLineWidth(X.c, 4); col(X, 1, .3, .3); poly(X, e.second, kOfxDrawPrimitiveLines);
            }
            haloLoop(X, {{reqX, reqY}, {realX, realY}}, 1, .3, .3, kOfxDrawLineStipplePatternDash, kOfxDrawPrimitiveLines);
            std::vector<OfxPointD> ring; for (int i = 0; i < 24; i++) ring.push_back({realX + 7 * u * cos(i * M_PI / 12), realY + 7 * u * sin(i * M_PI / 12)});
            gDraw->setLineWidth(X.c, 2.5f); col(X, 1, .3, .3); poly(X, ring, kOfxDrawPrimitiveLineLoop);
            std::string why = "Held in by the safe area";
            if (!edges.empty()) { why = "Safe area: no room to go "; for (size_t i = 0; i < edges.size(); i++) why += (i ? " or " : "") + edges[i].first; }
            label(X, labels, why.c_str(), realX + 10 * u, realY - 26 * u, 1, .5, .5, true);
        }
        // Centre point
        double cxp = G.anchorX, cyp = G.anchorY, arm = 14 * u;
        if (fx->bv("gCross", t)) {
            std::vector<OfxPointD> c = {{cxp - arm, cyp}, {cxp + arm, cyp}, {cxp, cyp - arm}, {cxp, cyp + arm}};
            gDraw->setLineWidth(X.c, (float)(4.5 * X.wt)); col(X, 0, 0, 0, .75); poly(X, c, kOfxDrawPrimitiveLines);
            gDraw->setLineWidth(X.c, (float)(2 * X.wt)); col(X, 1, 1, 1); poly(X, c, kOfxDrawPrimitiveLines);
        }
        if (fx->bv("gDot", t)) {
            double r = 4.5 * u * std::max(1.0, X.wt * .8);
            std::vector<OfxPointD> d; for (int i = 0; i < 20; i++) d.push_back({cxp + r * cos(i * M_PI / 10), cyp + r * sin(i * M_PI / 10)});
            col(X, .91, .64, .24); poly(X, d, kOfxDrawPrimitivePolygon);
            gDraw->setLineWidth(X.c, 1.5f); col(X, 0, 0, 0, .75); poly(X, d, kOfxDrawPrimitiveLineLoop);
        }
        if (fx->bv("gCross", t) || fx->bv("gDot", t)) label(X, labels, "Centre", cxp + 8 * u, cyp + 8 * u, 1, 1, 1);
        return true;
    }

    bool penDown(const PenArgs& a) override {
        double FW, FH, IW, IH; fx->imageAspect(a.time, FW, FH, IW, IH);
        Geom G = framely::computeGeom(fx->settingsAt(a.time), FW, FH, IW, IH);
        double px = a.penPosition.x, py = FH - a.penPosition.y;
        // grab only inside the picture (with a little slack)
        double slack = 10 * a.pixelScale.x;
        if (framely::sdRRect(G.shape, px, py) > slack) return false;
        dragImage = (fx->ci("dragMode", a.time) == 1) != altDown;
        flog("penDown: drag %s", dragImage ? "image" : "picture");
        fx->pinAll("penDown");
        start = a.penPosition; dragging = true;
        if (dragImage) { s0 = fx->dv("panX", a.time); s1 = fx->dv("panY", a.time); }
        else { s0 = fx->dv("posX", a.time); s1 = fx->dv("posY", a.time); }
        fx->beginEditBlock(dragImage ? "Framely reframe" : "Framely move");
        return true;
    }
    bool penMotion(const PenArgs& a) override {
        if (!dragging) return false;
        double FW, FH, IW, IH; fx->imageAspect(a.time, FW, FH, IW, IH);
        double dx = a.penPosition.x - start.x, dy = a.penPosition.y - start.y;   // y up: + = up
        if (!dragImage) {
            fx->dp("posX")->setValue(s0 + dx / FW * 100);
            fx->dp("posY")->setValue(s1 + dy / FH * 100);
        } else {
            Geom G = framely::computeGeom(fx->settingsAt(a.time), FW, FH, IW, IH);
            fx->dp("panX")->setValue(s0 + dx / G.unitX * 100);
            fx->dp("panY")->setValue(s1 + dy / G.unitY * 100);
        }
        _effect->redrawOverlays();
        return true;
    }
    bool penUp(const PenArgs& a) override {
        if (!dragging) return false;
        dragging = false;
        if (dragImage) fx->normalizePan(a.time);
        fx->endEditBlock();
        return true;
    }
    bool keyDown(const KeyArgs& a) override { if (a.keySymbol == kOfxKey_Alt_L || a.keySymbol == kOfxKey_Alt_R) altDown = true; return false; }
    bool keyUp(const KeyArgs& a) override { if (a.keySymbol == kOfxKey_Alt_L || a.keySymbol == kOfxKey_Alt_R) altDown = false; return false; }
    void loseFocus(const FocusArgs&) override { altDown = false; }
};
class FramelyOverlayDescriptor : public DefaultEffectOverlayDescriptor<FramelyOverlayDescriptor, FramelyInteract> {};

// ============================================================ factory
mDeclarePluginFactory(FramelyFactory, {}, {});

void FramelyFactory::describe(ImageEffectDescriptor& desc) {
    gDraw = (const OfxDrawSuiteV1*)fetchSuite(kOfxDrawSuite, 1, true);
    desc.setLabels("Framely – Picture in Picture", "Framely – Picture in Picture", "Framely – Picture in Picture");
    desc.setPluginGrouping("Framely");
    desc.setPluginDescription("Picture in Picture that starts centred with the whole picture visible. Every value is a % of the screen or of each image. v" FRAMELY_VERSION);
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedBitDepth(eBitDepthFloat);
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(false);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setOverlayInteractDescriptor(new FramelyOverlayDescriptor);
}

void FramelyFactory::describeInContext(ImageEffectDescriptor& desc, ContextEnum) {
    ClipDescriptor* src = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    src->addSupportedComponent(ePixelComponentRGBA); src->setSupportsTiles(false);
    ClipDescriptor* dst = desc.defineClip(kOfxImageEffectOutputClipName);
    dst->addSupportedComponent(ePixelComponentRGBA); dst->setSupportsTiles(false);

    Values defs = currentDefaults();   // the saved default becomes the native ↺ default (as of this Resolve launch)
    PageParamDescriptor* page = desc.definePageParam("Controls");
    std::map<std::string, GroupParamDescriptor*> grp;
    auto addGroupIfNeeded = [&](const char* g) {
        if (!g || grp.count(g)) return;
        for (auto& G : GROUPS) if (!strcmp(G.name, g)) {
            GroupParamDescriptor* gd = desc.defineGroupParam(G.name); gd->setLabel(G.label); gd->setOpen(G.open); page->addChild(*gd); grp[g] = gd;
        }
    };
    for (auto& d : PARAMS()) {
        addGroupIfNeeded(d.group);
        ParamDescriptor* pd = nullptr;
        const std::vector<double>& dv = defs[d.name];
        switch (d.kind) {
            case kDouble: {
                DoubleParamDescriptor* p = desc.defineDoubleParam(d.name);
                double mn = d.min, mx = d.max;
                if (d.basisGroup) {   // describe with the range of the default basis
                    int b = (int)defs[d.basisGroup][0];
                    if (b == 0 && !std::isnan(d.smin)) { mn = d.smin; mx = d.smax; }
                }
                p->setRange(mn, mx); p->setDisplayRange(mn, mx); p->setDefault(dv.empty() ? d.def : dv[0]);
                p->setIncrement(0.1); p->setDigits(2); p->setDoubleType(eDoubleTypePlain);
                pd = p; break;
            }
            case kBool: { BooleanParamDescriptor* p = desc.defineBooleanParam(d.name); p->setDefault(dv.empty() ? d.def != 0 : dv[0] != 0); pd = p; break; }
            case kChoice: { ChoiceParamDescriptor* p = desc.defineChoiceParam(d.name); for (auto o : d.opts) p->appendOption(o); p->setDefault(dv.empty() ? (int)d.def : (int)dv[0]); pd = p; break; }
            case kRGB: { RGBParamDescriptor* p = desc.defineRGBParam(d.name); if (dv.size() == 3) p->setDefault(dv[0], dv[1], dv[2]); else p->setDefault(d.r, d.g, d.b); pd = p; break; }
            case kButton: { PushButtonParamDescriptor* p = desc.definePushButtonParam(d.name); pd = p; break; }
            default: break;
        }
        if (!pd) continue;
        pd->setLabel(d.label);
        if (d.hint && *d.hint) pd->setHint(d.hint);
        if (d.group) pd->setParent(*grp[d.group]);
        if (d.kind != kButton) {
            bool noAnim = d.kind == kChoice || (d.group && !strcmp(d.group, "grpGuides")) || !strcmp(d.name, "safeOn");
            if (noAnim) ((ValueParamDescriptor*)pd)->setAnimates(false);
        }
        page->addChild(*pd);
    }
    DoubleParamDescriptor* pin = desc.defineDoubleParam("pinStamp");
    pin->setDefault(0); pin->setIsSecret(true); pin->setAnimates(false); pin->setLabel("pinStamp"); pin->setRange(0, 1e12); pin->setDisplayRange(0, 1e12);
    page->addChild(*pin);
    BooleanParamDescriptor* init = desc.defineBooleanParam("initialized");
    init->setDefault(false); init->setIsSecret(true); init->setAnimates(false); init->setLabel("initialized");
    page->addChild(*init);
    flog("describeInContext (defaults from %s)", access(defaultsPath().c_str(), R_OK) == 0 ? "saved file" : "factory");
}

ImageEffect* FramelyFactory::createInstance(OfxImageEffectHandle h, ContextEnum) { return new FramelyPlugin(h); }

namespace OFX { namespace Plugin {
void getPluginIDs(PluginFactoryArray& ids) {
    static FramelyFactory p("com.framely.pictureinpicture", 1, 0);
    ids.push_back(&p);
}
}}
