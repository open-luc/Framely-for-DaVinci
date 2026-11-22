// FramelyProbe: a throwaway OFX plugin that answers host-capability questions
// for DaVinci Resolve before the real Framely plugin is built.
//  - Does Resolve call the overlay interact (draw, pen down/drag)?  Which drawing API works?
//  - Is overlay drawing kept out of renders?
//  - Can a slider's range be changed at runtime (setRange/setDisplayRange)?
//  - Can a push button set other params (needed for Reset buttons)?
//  - Are group open/closed states remembered?
//  - When is describeInContext called (per launch / per instance)? -> saved-defaults strategy
//  - What does the source image look like (bounds, alpha, render scale, PAR)?
// Everything is logged to ~/Library/Logs/FramelyProbe.log

#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <ctime>
#include <string>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <OpenGL/gl.h>

#include "ofxsImageEffect.h"
#include "ofxsInteract.h"
#include "ofxsMultiThread.h"
#include "ofxsProcessing.H"
#include "ofxDrawSuite.h"

using namespace OFX;

static void plog(const char* fmt, ...) {
    std::string path = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/Library/Logs/FramelyProbe.log";
    FILE* f = fopen(path.c_str(), "a");
    if (!f) return;
    time_t t = time(nullptr); char ts[32]; strftime(ts, sizeof ts, "%H:%M:%S", localtime(&t));
    fprintf(f, "[%s pid %d] ", ts, (int)getpid());
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

static double readDefaultFromFile(double fallback) {
    std::string path = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/Library/Application Support/Framely/probe-default.txt";
    std::ifstream in(path); double v;
    if (in >> v) return v;
    return fallback;
}

static const OfxDrawSuiteV1* gDraw = nullptr;
static int gDescribeCount = 0;

// ---------------- render ----------------
class ProbeProcessor : public ImageProcessor {
public:
    Image* src = nullptr;
    double mx = 0, my = 0, bar = 0;   // marker in pixel coords; bar = fraction 0..1
    bool drawMarker = true;
    explicit ProbeProcessor(ImageEffect& e) : ImageProcessor(e) {}
    void multiThreadProcessImages(OfxRectI win) override {
        for (int y = win.y1; y < win.y2; ++y) {
            if (_effect.abort()) break;
            float* d = (float*)_dstImg->getPixelAddress(win.x1, y);
            for (int x = win.x1; x < win.x2; ++x, d += 4) {
                float* s = src ? (float*)src->getPixelAddress(x, y) : nullptr;
                if (s) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; } else { d[0] = d[1] = d[2] = d[3] = 0; }
                if (drawMarker && (std::fabs(x - mx) < 4 || std::fabs(y - my) < 4) && std::fabs(x - mx) < 60 && std::fabs(y - my) < 60) {
                    d[0] = 1; d[1] = 0; d[2] = 1; d[3] = 1;   // magenta "render" crosshair
                }
                OfxRectI b = _dstImg->getBounds();
                double fx = double(x - b.x1) / (b.x2 - b.x1);
                if (y - b.y1 < 30 && fx < bar) { d[0] = 1; d[1] = 1; d[2] = 0; d[3] = 1; }  // yellow bar = "Ranged" value
            }
        }
    }
};

class ProbePlugin : public ImageEffect {
public:
    Clip *srcClip, *dstClip;
    DoubleParam *posX, *posY, *ranged;
    ChoiceParam* rangeMode;
    BooleanParam* marker;
    PushButtonParam* setBtn;
    explicit ProbePlugin(OfxImageEffectHandle h) : ImageEffect(h) {
        srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        dstClip = fetchClip(kOfxImageEffectOutputClipName);
        posX = fetchDoubleParam("posX"); posY = fetchDoubleParam("posY");
        ranged = fetchDoubleParam("ranged"); rangeMode = fetchChoiceParam("rangeMode");
        marker = fetchBooleanParam("marker"); setBtn = fetchPushButtonParam("setBtn");
        plog("createInstance: ranged=%.3f posX=%.3f", ranged->getValue(), posX->getValue());
    }
    void render(const RenderArguments& args) override {
        std::unique_ptr<Image> dst(dstClip->fetchImage(args.time));
        std::unique_ptr<Image> src(srcClip->fetchImage(args.time));
        OfxRectD rod = srcClip->getRegionOfDefinition(args.time);
        OfxRectI sb = src ? src->getBounds() : OfxRectI{0,0,0,0};
        OfxRectI db = dst->getBounds();
        static int n = 0;
        if (n++ < 40) {
            // alpha bbox + corner samples
            int ax1 = 1<<30, ay1 = 1<<30, ax2 = -1, ay2 = -1; double asum = 0; long cnt = 0;
            if (src) for (int y = sb.y1; y < sb.y2; y += 2) for (int x = sb.x1; x < sb.x2; x += 2) {
                float* p = (float*)src->getPixelAddress(x, y); if (!p) continue;
                asum += p[3]; cnt++;
                if (p[3] > 0.5f) { ax1 = std::min(ax1, x); ay1 = std::min(ay1, y); ax2 = std::max(ax2, x); ay2 = std::max(ay2, y); }
            }
            float* c = src ? (float*)src->getPixelAddress(sb.x1, sb.y1) : nullptr;
            float* m = src ? (float*)src->getPixelAddress((sb.x1+sb.x2)/2, (sb.y1+sb.y2)/2) : nullptr;
            plog("render t=%.1f scale=(%.3f,%.3f) win=[%d %d %d %d] src=[%d %d %d %d] dst=[%d %d %d %d] rod=[%.0f %.0f %.0f %.0f] par=%.3f field=%d "
                 "alphaBBox=[%d %d %d %d] meanA=%.3f corner=(%.3f %.3f %.3f %.3f) mid=(%.3f %.3f %.3f %.3f) depth=%d comps=%d premult=%d",
                 args.time, args.renderScale.x, args.renderScale.y, args.renderWindow.x1, args.renderWindow.y1, args.renderWindow.x2, args.renderWindow.y2,
                 sb.x1, sb.y1, sb.x2, sb.y2, db.x1, db.y1, db.x2, db.y2, rod.x1, rod.y1, rod.x2, rod.y2, srcClip->getPixelAspectRatio(), (int)args.fieldToRender,
                 ax1, ay1, ax2, ay2, cnt ? asum / cnt : -1, c ? c[0] : -1, c ? c[1] : -1, c ? c[2] : -1, c ? c[3] : -1,
                 m ? m[0] : -1, m ? m[1] : -1, m ? m[2] : -1, m ? m[3] : -1, (int)dst->getPixelDepth(), (int)dst->getPixelComponents(), (int)dst->getPreMultiplication());
        }
        ProbeProcessor p(*this);
        p.setDstImg(dst.get()); p.src = src.get();
        // posX/posY are % of frame from centre (up +), like Framely
        double w = rod.x2 - rod.x1, h = rod.y2 - rod.y1;
        p.mx = (rod.x1 + w / 2 + posX->getValueAtTime(args.time) / 100 * w) * args.renderScale.x;
        p.my = (rod.y1 + h / 2 + posY->getValueAtTime(args.time) / 100 * h) * args.renderScale.y;
        int mode; rangeMode->getValue(mode);
        p.bar = ranged->getValueAtTime(args.time) / (mode == 0 ? 10.0 : 100.0);
        p.drawMarker = marker->getValueAtTime(args.time);
        p.setRenderWindow(args.renderWindow);
        p.process();
    }
    void changedParam(const InstanceChangedArgs& args, const std::string& name) override {
        plog("changedParam %s reason=%d", name.c_str(), (int)args.reason);
        if (name == "rangeMode") {
            int mode; rangeMode->getValue(mode);
            double mx = mode == 0 ? 10 : 100;
            ranged->setRange(0, mx); ranged->setDisplayRange(0, mx);
            plog("  set ranged range to 0..%.0f", mx);
        } else if (name == "setBtn") {
            ranged->setValue(4.2); posX->setValue(0); posY->setValue(0);
            plog("  button set ranged=4.2, pos=0,0 -> now ranged=%.3f", ranged->getValue());
        }
    }
    bool isIdentity(const IsIdentityArguments&, Clip*&, double&) override { return false; }
};

// ---------------- overlay ----------------
class ProbeInteract : public OverlayInteract {
    ImageEffect* fx; DoubleParam *px, *py; bool dragging = false; OfxPointD start{0,0}; double sx = 0, sy = 0;
    int draws = 0;
public:
    ProbeInteract(OfxInteractHandle h, ImageEffect* e) : OverlayInteract(h), fx(e) {
        px = e->fetchDoubleParam("posX"); py = e->fetchDoubleParam("posY");
        addParamToSlaveTo(px); addParamToSlaveTo(py);
        plog("interact created");
    }
    OfxRectD rod(double t) { return fx->fetchClip(kOfxImageEffectSimpleSourceClipName)->getRegionOfDefinition(t); }
    void centre(double t, double& cx, double& cy) {
        OfxRectD r = rod(t); double w = r.x2 - r.x1, h = r.y2 - r.y1;
        cx = r.x1 + w / 2 + px->getValueAtTime(t) / 100 * w; cy = r.y1 + h / 2 + py->getValueAtTime(t) / 100 * h;
    }
    bool draw(const DrawArgs& a) override {
        double cx, cy; centre(a.time, cx, cy);
        OfxRectD r = rod(a.time);
        double L = 80 * a.pixelScale.x;
        if (draws++ < 5) plog("overlay draw #%d: drawContext=%p drawSuite=%p pixelScale=(%.3f,%.3f) rod=[%.0f %.0f %.0f %.0f] centre=(%.1f,%.1f) renderScale=(%.3f,%.3f)",
                             draws, a.drawContext, (void*)gDraw, a.pixelScale.x, a.pixelScale.y, r.x1, r.y1, r.x2, r.y2, cx, cy, a.renderScale.x, a.renderScale.y);
        if (gDraw && a.drawContext) {
            OfxDrawContextHandle c = (OfxDrawContextHandle)a.drawContext;
            OfxRGBAColourF col = {0.2f, 1.f, 0.4f, 1.f};      // green = Draw Suite path
            gDraw->setColour(c, &col); gDraw->setLineWidth(c, 3);
            OfxPointD pts[4] = {{cx - L, cy}, {cx + L, cy}, {cx, cy - L}, {cx, cy + L}};
            gDraw->draw(c, kOfxDrawPrimitiveLines, pts, 4);
            OfxPointD box[2] = {{r.x1 + (r.x2-r.x1)*0.1, r.y1 + (r.y2-r.y1)*0.1}, {r.x2 - (r.x2-r.x1)*0.1, r.y2 - (r.y2-r.y1)*0.1}};
            gDraw->setLineStipple(c, kOfxDrawLineStipplePatternDash);
            gDraw->draw(c, kOfxDrawPrimitiveRectangle, box, 2);
            gDraw->setLineStipple(c, kOfxDrawLineStipplePatternSolid);
            OfxPointD tp = {cx + 10 * a.pixelScale.x, cy + 10 * a.pixelScale.y};
            gDraw->drawText(c, "Framely probe overlay (Draw Suite)", &tp, kOfxDrawTextAlignmentLeft);
        } else {
            glPushAttrib(GL_ALL_ATTRIB_BITS);
            glLineWidth(3); glColor3f(0.2f, 0.6f, 1.f);       // blue = OpenGL path
            glBegin(GL_LINES); glVertex2d(cx - L, cy); glVertex2d(cx + L, cy); glVertex2d(cx, cy - L); glVertex2d(cx, cy + L); glEnd();
            glBegin(GL_LINE_LOOP);
            glVertex2d(r.x1 + (r.x2-r.x1)*0.1, r.y1 + (r.y2-r.y1)*0.1); glVertex2d(r.x2 - (r.x2-r.x1)*0.1, r.y1 + (r.y2-r.y1)*0.1);
            glVertex2d(r.x2 - (r.x2-r.x1)*0.1, r.y2 - (r.y2-r.y1)*0.1); glVertex2d(r.x1 + (r.x2-r.x1)*0.1, r.y2 - (r.y2-r.y1)*0.1); glEnd();
            glPopAttrib();
        }
        return true;
    }
    bool penDown(const PenArgs& a) override {
        double cx, cy; centre(a.time, cx, cy);
        plog("penDown at (%.1f,%.1f) centre (%.1f,%.1f) pixelScale %.3f", a.penPosition.x, a.penPosition.y, cx, cy, a.pixelScale.x);
        dragging = true; start = a.penPosition; sx = px->getValueAtTime(a.time); sy = py->getValueAtTime(a.time);
        fx->beginEditBlock("probe drag");
        return true;
    }
    bool penMotion(const PenArgs& a) override {
        if (!dragging) return false;
        OfxRectD r = rod(a.time); double w = r.x2 - r.x1, h = r.y2 - r.y1;
        px->setValue(sx + (a.penPosition.x - start.x) / w * 100);
        py->setValue(sy + (a.penPosition.y - start.y) / h * 100);
        _effect->redrawOverlays();
        return true;
    }
    bool penUp(const PenArgs& a) override {
        if (!dragging) return false;
        dragging = false; fx->endEditBlock();
        plog("penUp: posX=%.2f posY=%.2f", px->getValue(), py->getValue());
        return true;
    }
    bool keyDown(const KeyArgs& a) override { plog("keyDown %d", a.keySymbol); return false; }
};

class ProbeOverlayDescriptor : public DefaultEffectOverlayDescriptor<ProbeOverlayDescriptor, ProbeInteract> {};

// ---------------- factory ----------------
mDeclarePluginFactory(ProbeFactory, {}, {});

void ProbeFactory::describe(ImageEffectDescriptor& desc) {
    const ImageEffectHostDescription& H = *getImageEffectHostDescription();
    gDraw = (const OfxDrawSuiteV1*)fetchSuite(kOfxDrawSuite, 1, true);
    plog("==== describe: host=%s label=%s v%d.%d.%d (%s) api %d.%d overlays=%d customInteract=%d multiRes=%d tiles=%d metal=%d drawSuite=%p",
         H.hostName.c_str(), H.hostLabel.c_str(), H.versionMajor, H.versionMinor, H.versionMicro, H.versionLabel.c_str(),
         H.APIVersionMajor, H.APIVersionMinor, (int)H.supportsOverlays, (int)H.supportsCustomInteract, (int)H.supportsMultiResolution,
         (int)H.supportsTiles, (int)H.supportsMetalRender, (void*)gDraw);
    desc.setLabels("Framely Probe", "Framely Probe", "Framely Probe");
    desc.setPluginGrouping("Framely");
    desc.setPluginDescription("Test plugin for Framely development.");
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedBitDepth(eBitDepthFloat);
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(false);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setOverlayInteractDescriptor(new ProbeOverlayDescriptor);
}

void ProbeFactory::describeInContext(ImageEffectDescriptor& desc, ContextEnum) {
    double def = readDefaultFromFile(3.0);
    plog("describeInContext #%d: ranged default from file = %.3f", ++gDescribeCount, def);
    ClipDescriptor* src = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    src->addSupportedComponent(ePixelComponentRGBA); src->setSupportsTiles(false);
    ClipDescriptor* dst = desc.defineClip(kOfxImageEffectOutputClipName);
    dst->addSupportedComponent(ePixelComponentRGBA); dst->setSupportsTiles(false);
    PageParamDescriptor* page = desc.definePageParam("Controls");

    GroupParamDescriptor* gA = desc.defineGroupParam("groupA"); gA->setLabel("Group A (starts open)"); gA->setOpen(true);
    GroupParamDescriptor* gB = desc.defineGroupParam("groupB"); gB->setLabel("Group B (starts closed)"); gB->setOpen(false);
    page->addChild(*gA); page->addChild(*gB);

    DoubleParamDescriptor* d = desc.defineDoubleParam("posX"); d->setLabel("Position X"); d->setDefault(0);
    d->setRange(-100, 100); d->setDisplayRange(-100, 100); d->setParent(*gA); page->addChild(*d);
    d = desc.defineDoubleParam("posY"); d->setLabel("Position Y"); d->setDefault(0);
    d->setRange(-100, 100); d->setDisplayRange(-100, 100); d->setParent(*gA); page->addChild(*d);
    BooleanParamDescriptor* b = desc.defineBooleanParam("marker"); b->setLabel("Draw magenta marker in render"); b->setDefault(true);
    b->setParent(*gA); page->addChild(*b);

    ChoiceParamDescriptor* c = desc.defineChoiceParam("rangeMode"); c->setLabel("Ranged slider range");
    c->appendOption("0 to 10"); c->appendOption("0 to 100"); c->setDefault(0); c->setParent(*gB); page->addChild(*c);
    d = desc.defineDoubleParam("ranged"); d->setLabel("Ranged"); d->setDefault(def);
    d->setRange(0, 10); d->setDisplayRange(0, 10); d->setParent(*gB); page->addChild(*d);
    PushButtonParamDescriptor* p = desc.definePushButtonParam("setBtn"); p->setLabel("Set Ranged to 4.2 + centre");
    p->setParent(*gB); page->addChild(*p);
}

ImageEffect* ProbeFactory::createInstance(OfxImageEffectHandle h, ContextEnum) { return new ProbePlugin(h); }

namespace OFX { namespace Plugin {
void getPluginIDs(PluginFactoryArray& ids) {
    static ProbeFactory p("com.framely.probe", 1, 0);
    ids.push_back(&p);
}
}}
