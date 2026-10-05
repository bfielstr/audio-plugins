// Draw benchmark for an editor (issue #5). Hosts the plug-in in-process (its processor and controller,
// connected as a host connects them), builds its real editor into a frame that is not in a window
// (EditorBase::openDetached) and draws it into an offscreen bitmap the way the window's context is
// drawn, with and without the cached layers (pk::CachedLayer; without is the drawing before the cache):
//   - the whole window, at rest;
//   - while audio plays through the plug-in (between draws ~33 ms of a test signal is processed and the
//     editor idles, the 30 Hz timer, so meters, spectra and playheads move as in a host): what one
//     tick repaints (the rectangles the views invalidated), and each large display's own drawing;
//   - once the audio has stopped (6 s of quiet input): how many repaints a second the views still
//     ask for;
// at 100, 150 and 200 % zoom. It also checks that both ways give the same pixels, at rest and with the
// meters lit. It prints times, it does not judge them: it fails only if the editor cannot be built or
// the two renderings differ.
// Linux only (the cairo backend draws offscreen without a display). Built with -DPK_DRAW_BENCH=ON:
//   <plugin>_drawbench [ticks] [--all] [--quiet-rects] [--dump <dir>] [--compare <dir>] [--set <id>=<normalized>]...
// ticks: draws timed per figure (the median is shown). --all times every view, not only the large
// displays; --quiet-rects lists the rectangles still repainted once the audio has stopped. --dump
// writes the renderings (PNG to look at, .rgba to compare); --compare reports how far this build's
// renderings at rest are from ones dumped by another build (before / after a change to a view). --set
// gives a parameter a value (normalized, in the controller) before the editor is built (to look at a
// state: an end saturator switched on, its section open).
#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/LayoutCheck.h"
#include "pluginkit/vst/ControllerBase.h"
#include "pluginkit/vst/EditorBase.h"

#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/platform/iplatformbitmap.h"
#include "vstgui/lib/platform/platformfactory.h"

#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

extern "C" bool ModuleEntry (void*);
extern "C" bool ModuleExit ();
extern "C" Steinberg::IPluginFactory* GetPluginFactory ();

using namespace VSTGUI;
using namespace Steinberg;

namespace {

struct Pixels
{
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
};

Pixels readPixels (CBitmap* bmp)
{
    Pixels p;
    auto access = VSTGUI::owned (CBitmapPixelAccess::create (bmp));
    if (!access)
        return p;
    p.w = (int)access->getBitmapWidth ();
    p.h = (int)access->getBitmapHeight ();
    p.rgba.resize ((size_t)p.w * p.h * 4);
    size_t i = 0;
    for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x)
        {
            access->setPosition ((uint32_t)x, (uint32_t)y);
            CColor c;
            access->getColor (c);
            p.rgba[i++] = c.red;
            p.rgba[i++] = c.green;
            p.rgba[i++] = c.blue;
            p.rgba[i++] = c.alpha;
        }
    return p;
}

// pixels that differ by more than `tol` in a channel, and the largest difference
struct Diff
{
    long count = 0;
    int maxDelta = 0;
};

Diff compare (const Pixels& a, const Pixels& b, int tol = 2)
{
    Diff d;
    if (a.w != b.w || a.h != b.h)
    {
        d.count = -1;
        return d;
    }
    for (size_t i = 0; i < a.rgba.size (); i += 4)
    {
        int m = 0;
        for (int c = 0; c < 4; ++c)
            m = std::max (m, std::abs ((int)a.rgba[i + c] - (int)b.rgba[i + c]));
        d.maxDelta = std::max (d.maxDelta, m);
        if (m > tol)
            ++d.count;
    }
    return d;
}

void writeFiles (CBitmap* bmp, const Pixels& p, const std::string& base)
{
    if (auto pb = bmp->getPlatformBitmap ())
    {
        auto png = getPlatformFactory ().createBitmapMemoryPNGRepresentation (pb);
        if (FILE* f = std::fopen ((base + ".png").c_str (), "wb"))
        {
            std::fwrite (png.data (), 1, png.size (), f);
            std::fclose (f);
        }
    }
    if (FILE* f = std::fopen ((base + ".rgba").c_str (), "wb"))
    {
        const int32_t wh[2] = {p.w, p.h};
        std::fwrite (wh, sizeof (wh), 1, f);
        std::fwrite (p.rgba.data (), 1, p.rgba.size (), f);
        std::fclose (f);
    }
}

bool readFile (const std::string& base, Pixels& p)
{
    FILE* f = std::fopen ((base + ".rgba").c_str (), "rb");
    if (!f)
        return false;
    int32_t wh[2] = {0, 0};
    bool ok = std::fread (wh, sizeof (wh), 1, f) == 1 && wh[0] > 0 && wh[1] > 0;
    if (ok)
    {
        p.w = wh[0];
        p.h = wh[1];
        p.rgba.resize ((size_t)p.w * p.h * 4);
        ok = std::fread (p.rgba.data (), 1, p.rgba.size (), f) == p.rgba.size ();
    }
    std::fclose (f);
    return ok;
}

double nowMs ()
{
    using namespace std::chrono;
    return duration<double, std::milli> (steady_clock::now ().time_since_epoch ()).count ();
}

double median (std::vector<double> t)
{
    if (t.empty ())
        return 0.0;
    std::nth_element (t.begin (), t.begin () + t.size () / 2, t.end ());
    return t[t.size () / 2];
}

// The large displays: views (not containers) of at least 150 x 40 at 100 % and narrower than the
// window (Smemplr's modulation overlay covers it all); with `all`, every view.
void findDisplays (CViewContainer* c, std::vector<CView*>& out, bool all, double maxW)
{
    c->forEachChild ([&] (CView* v) {
        if (auto* sub = v->asViewContainer ())
            findDisplays (sub, out, all, maxW);
        else if (all || (v->getWidth () >= 150 && v->getHeight () >= 40 && v->getWidth () < maxW))
            out.push_back (v);
    });
}

// The plug-in, processing: a test signal through its inputs (a swelling chord over soft noise, so
// levels, spectra and gain reduction move), and a chord every two seconds for an instrument.
struct Audio
{
    IPtr<Vst::PlugProvider> provider;
    IPtr<Vst::IComponent> component;
    IPtr<Vst::IEditController> controller;
    FUnknownPtr<Vst::IAudioProcessor> processor;
    Vst::HostProcessData data;
    Vst::EventList events {64};
    Vst::ParameterChanges changes {8};
    Vst::ProcessContext ctx {};
    double rate = 48000.0;
    int block = 480;
    long long pos = 0;
    bool running = false;
    uint32_t noise = 22222;

    bool start ()
    {
        processor = FUnknownPtr<Vst::IAudioProcessor> (component);
        if (!processor)
            return false;
        Vst::ProcessSetup setup {Vst::kRealtime, Vst::kSample32, block, rate};
        if (processor->setupProcessing (setup) != kResultOk || component->setActive (true) != kResultOk)
            return false;
        processor->setProcessing (true);
        data.prepare (*component, block, Vst::kSample32);
        data.numSamples = block;
        data.inputEvents = &events;
        data.inputParameterChanges = &changes;
        ctx.sampleRate = rate;
        ctx.tempo = 120.0;
        ctx.state = Vst::ProcessContext::kTempoValid | Vst::ProcessContext::kProjectTimeMusicValid | Vst::ProcessContext::kPlaying;
        data.processContext = &ctx;
        running = true;
        return true;
    }
    void stop ()
    {
        if (!running)
            return;
        processor->setProcessing (false);
        component->setActive (false);
        data.unprepare ();
        running = false;
    }
    // about `ms` of audio (whole blocks)
    // (silent: the inputs quiet, as when the transport stops)
    void play (double ms, bool silent = false)
    {
        if (!running)
            return;
        const int blocks = std::max (1, (int)std::lround (ms * 0.001 * rate / block));
        for (int b = 0; b < blocks; ++b)
        {
            for (int32 bus = 0; bus < data.numInputs; ++bus)
                for (int32 ch = 0; ch < data.inputs[bus].numChannels; ++ch)
                {
                    float* buf = data.inputs[bus].channelBuffers32[ch];
                    for (int i = 0; i < block; ++i)
                    {
                        const double t = (double)(pos + i) / rate;
                        const double swell = 0.55 + 0.45 * std::sin (2.0 * M_PI * 0.7 * t);
                        noise = noise * 1664525u + 1013904223u;
                        const double n = ((double)(noise >> 8) / 8388608.0 - 1.0) * 0.05;
                        const double tone = 0.25 * std::sin (2.0 * M_PI * 220.0 * t) + 0.15 * std::sin (2.0 * M_PI * 330.0 * t + ch) +
                                            0.1 * std::sin (2.0 * M_PI * 1760.0 * t);
                        buf[i] = silent ? 0.0f : bus == 0 ? (float)(swell * tone + n) : (float)(0.5 * tone);
                    }
                }
            if (!silent && pos % (long long)(rate * 2) < block)
                for (int16 p : {48, 60, 64, 67})
                {
                    Vst::Event e {};
                    e.type = Vst::Event::kNoteOnEvent;
                    e.noteOn.pitch = p;
                    e.noteOn.velocity = 0.8f;
                    e.noteOn.noteId = -1;
                    events.addEvent (e);
                }
            ctx.projectTimeSamples = pos;
            processor->process (data);
            events.clear ();
            changes.clearQueue ();
            ctx.projectTimeMusic += block * ctx.tempo / 60.0 / rate;
            pos += block;
        }
    }
};

// Holds the editor's content in its frame and keeps what the views invalidate, in window coordinates
// (as a window's frame hands them to the platform: through the frame's transform, made integral),
// joined as the macOS frame joins them (rectangles less than 24 px apart become one).
struct Recorder : CViewContainer
{
    explicit Recorder (const CRect& r) : CViewContainer (r) {}
    std::vector<CRect> rects;
    void drawBackgroundRect (CDrawContext*, const CRect&) override {}
    void invalidRect (const CRect& rect) override
    {
        if (auto* f = getFrame ())
        {
            CRect r (rect);
            f->getTransform ().transform (r);
            r.makeIntegral ();
            rects.push_back (r);
        }
    }
    std::vector<CRect> take ()
    {
        std::vector<CRect> out;
        for (CRect r : rects)
        {
            bool joined = true;
            while (joined)
            {
                joined = false;
                for (size_t i = 0; i < out.size (); ++i)
                {
                    CRect near = out[i];
                    near.extend (24, 24);
                    if (near.rectOverlap (r))
                    {
                        r.unite (out[i]);
                        out.erase (out.begin () + (long)i);
                        joined = true;
                        break;
                    }
                }
            }
            out.push_back (r);
        }
        rects.clear ();
        return out;
    }
};

struct Sample
{
    std::string name;
    double directMs = 0, cachedMs = 0;
};

// ---- the layouts' check (--check-layouts) ------------------------------------------------------------

// The whole frame drawn directly (no cached layers) into a bitmap of its size.
Pixels render (CFrame* frame, const std::string& dumpBase = {})
{
    const CRect all (0, 0, std::round (frame->getWidth ()), std::round (frame->getHeight ()));
    auto off = COffscreenContext::create (all.getSize (), 1.0);
    if (!off)
        return {};
    pk::CachedLayer::setEnabled (false);
    off->beginDraw ();
    frame->drawRect (off, all);
    off->endDraw ();
    pk::CachedLayer::setEnabled (true);
    Pixels p = readPixels (off->getBitmap ());
    if (!dumpBase.empty ())
        writeFiles (off->getBitmap (), p, dumpBase);
    return p;
}

// The parameters with a control shown (the control and every container above it visible).
void visibleParams (CViewContainer* c, std::set<uint32>& out)
{
    c->forEachChild ([&] (CView* v) {
        if (!v->isVisible ())
            return;
        if (auto* sub = v->asViewContainer ())
            visibleParams (sub, out);
        else if (auto* pv = dynamic_cast<pk::ParamView*> (v))
            out.insert (pv->paramId ());
    });
}

// What the layout check reports that is clutter (controls overlapping or touching), and the rest (texts
// that spill out of their boxes, which do not depend on the layout).
void splitReport (const std::vector<std::string>& lines, std::vector<std::string>& clutter, size_t& spills)
{
    spills = 0;
    for (const auto& l : lines)
        if (l.rfind ("spill", 0) == 0)
            ++spills;
        else
            clutter.push_back (l);
}

// Default, Wide, a dragged arrangement and Default again, each built in the same editor as the Layout
// menu builds it: the arranged ones place every view of the build in a block and every panel exactly once,
// show every control the Default shows, inside the window, with nothing overlapping or touching; Default
// after them draws the same pixels as Default first (and as another build's rendering, --compare).
int checkLayoutsOf (Vst::IEditController* controller, pk::ControllerBase* ctl, const std::string& name, const std::string& dumpDir,
                    const std::string& compareDir)
{
    int failures = 0;
    auto fail = [&] (const std::string& what) {
        std::printf ("  FAIL: %s\n", what.c_str ());
        ++failures;
    };
    ctl->uiLayout.clear ();
    auto view = Steinberg::owned (controller->createView (Vst::ViewType::kEditor));
    auto* editor = dynamic_cast<pk::EditorBase*> (view.get ());
    CFrame* frame = editor ? editor->openDetached (1.0) : nullptr;
    if (!frame)
    {
        std::printf ("FAIL: no editor\n");
        return 1;
    }
    const pk::layout::Spec spec = editor->layoutSpec (true);
    std::printf ("layouts of %s: %zu panels\n", name.c_str (), spec.panels.size ());
    if (spec.empty ())
    {
        fail ("the editor declares no panels (no Layout menu); its root's views are:");
        if (frame->getNbViews () > 0)
            if (auto* root = frame->getView (0)->asViewContainer ())
                root->forEachChild ([] (CView* v) {
                    const CRect r = v->getViewSize ();
                    std::printf ("    %s [%g %g %g %g]\n", pk::typeName (typeid (*v)).c_str (), r.left, r.top, r.right, r.bottom);
                });
    }
    const auto base = [&] (const char* tag) { return dumpDir.empty () ? std::string () : dumpDir + "/" + name + "-layout-" + tag; };

    // Default: the editor as built
    const Pixels first = render (frame, base ("default"));
    const double defW = editor->fullWidth (), defH = editor->fullHeight ();
    std::set<uint32> defParams;
    visibleParams (frame, defParams);
    std::vector<std::string> defClutter;
    size_t defSpills = 0;
    splitReport (pk::layoutReport (frame), defClutter, defSpills);
    std::printf ("  default: %.0f x %.0f, %zu controls shown, %zu overlaps or touches, %zu spills\n", defW, defH, defParams.size (), defClutter.size (),
                 defSpills);
    if (!compareDir.empty ())
    {
        Pixels other;
        if (readFile (compareDir + "/" + name + "-100", other))
        {
            const Diff d = compare (other, first);
            std::printf ("  default against %s: %ld px differ (largest channel difference %d)\n", compareDir.c_str (), d.count, d.maxDelta);
            if (d.count != 0)
                fail ("the Default layout draws differently from the rendering compared with");
        }
        else
            std::printf ("  default against %s: no rendering to compare\n", compareDir.c_str ());
    }

    // an arranged layout, checked
    auto checkArranged = [&] (const char* tag, const std::string& text) {
        editor->setLayout (text, tag, true);
        std::printf ("  %s (%s): %.0f x %.0f\n", tag, text.c_str (), editor->fullWidth (), editor->fullHeight ());
        render (frame, base (tag));
        if (!editor->arrangedLayout ())
        {
            fail (std::string (tag) + ": not arranged");
            return;
        }
        for (const auto& p : editor->layoutProblems ())
            fail (std::string (tag) + ": in no panel: " + p);
        std::map<std::string, int> seen;
        const auto blocks = editor->layoutBlocks ();
        for (const auto& [id, r] : blocks)
        {
            ++seen[id];
            if (r.left < 0 || r.top < 0 || r.right > editor->fullWidth () || r.bottom > editor->contentHeightNow ())
                fail (std::string (tag) + ": block " + id + " outside the window");
        }
        for (const auto& p : spec.panels)
            if (seen[p.id] != 1)
                fail (std::string (tag) + ": panel " + p.id + " shown " + std::to_string (seen[p.id]) + " times");
        if (seen.size () != spec.panels.size ())
            fail (std::string (tag) + ": blocks of panels the spec does not have");
        std::set<uint32> params;
        visibleParams (frame, params);
        for (uint32 id : defParams)
            if (!params.count (id))
                fail (std::string (tag) + ": the control of parameter " + std::to_string (id) + " is not shown");
        std::vector<std::string> clutter;
        size_t spills = 0;
        splitReport (pk::layoutReport (frame), clutter, spills);
        for (const auto& l : clutter)
            fail (std::string (tag) + ": " + l);
        if (spills > defSpills)
            fail (std::string (tag) + ": texts spill that do not in the Default layout");
    };
    checkArranged ("wide", "wide");
    // wide and short: no taller than the Default
    if (editor->fullHeight () > defH)
        fail ("wide: taller than the Default layout");
    // dragged: the Wide template's first panel moved into a row of its own at the bottom, and its second
    // panel's column widened (as the editor does on a drop and on an edge's drag)
    if (!spec.empty ())
    {
        using namespace pk::layout;
        const Arrangement w = wide (spec);
        const Geometry g = place (spec, w);
        Drop d = dropAt (w, g, 40, g.rows.back ().bottom + 20);
        Arrangement m = move (spec, w, w.rows[0][0].ids[0], d);
        if (m.rows.size () > 0 && m.rows[0].size () > 0)
            m = resize (spec, m, m.rows[0][0].ids[0], naturalWidth (spec, m.rows[0][0]) + 40);
        checkArranged ("dragged", toString (m));
    }

    // Default again: the same pixels as at first
    editor->setLayout ("", "Default", true);
    const Pixels back = render (frame, base ("default-again"));
    const Diff d = compare (first, back);
    std::printf ("  default again: %ld px differ from the first (largest channel difference %d)\n", d.count, d.maxDelta);
    if (d.count != 0)
        fail ("the Default layout after Wide draws differently");
    if (editor->arrangedLayout () || editor->fullWidth () != defW || editor->fullHeight () != defH)
        fail ("the Default layout after Wide has another size");
    view->removed ();
    std::printf ("%s: %d failures\n", name.c_str (), failures);
    return failures ? 1 : 0;
}

} // namespace

int main (int argc, char** argv)
{
    int ticks = 30;
    std::string dumpDir, compareDir;
    bool allViews = false, listQuiet = false, checkLayouts = false;
    const char* layoutText = nullptr;
    std::vector<std::pair<uint32, double>> sets;
    for (int i = 1; i < argc; ++i)
    {
        unsigned id = 0;
        double v = 0.0;
        if (!std::strcmp (argv[i], "--set") && i + 1 < argc && std::sscanf (argv[i + 1], "%u=%lf", &id, &v) == 2)
        {
            sets.emplace_back ((uint32)id, v);
            ++i;
            continue;
        }
        if (!std::strcmp (argv[i], "--dump") && i + 1 < argc)
            dumpDir = argv[++i];
        else if (!std::strcmp (argv[i], "--compare") && i + 1 < argc)
            compareDir = argv[++i];
        else if (!std::strcmp (argv[i], "--all"))
            allViews = true;
        else if (!std::strcmp (argv[i], "--quiet-rects"))
            listQuiet = true;
        else if (!std::strcmp (argv[i], "--layout") && i + 1 < argc)
            layoutText = argv[++i];
        else if (!std::strcmp (argv[i], "--check-layouts"))
            checkLayouts = true;
        else
            ticks = std::max (1, std::atoi (argv[i]));
    }

    // the module as a host loads it (VSTGUI's platform, the factory), then the plug-in through the SDK's
    // provider (processor and controller, connected)
    if (!ModuleEntry (dlopen (nullptr, RTLD_NOW)))
    {
        std::printf ("FAIL: ModuleEntry\n");
        return 1;
    }
    int result = 0;
    {
        Vst::HostApplication host;
        Vst::PluginContextFactory::instance ().setPluginContext (&host);
        VST3::Hosting::PluginFactory factory (Steinberg::owned (GetPluginFactory ()));
        Audio audio;
        std::string pluginName;
        for (auto& ci : factory.classInfos ())
            if (ci.category () == kVstAudioEffectClass)
            {
                audio.provider = Steinberg::owned (new Vst::PlugProvider (factory, ci, true));
                pluginName = ci.name ();
                break;
            }
        if (!audio.provider || !audio.provider->initialize ())
        {
            std::printf ("FAIL: the plug-in does not load\n");
            return 1;
        }
        audio.component = audio.provider->getComponentPtr ();
        audio.controller = audio.provider->getControllerPtr ();
        if (!audio.component || !audio.controller || !audio.start ())
        {
            std::printf ("FAIL: the plug-in does not start\n");
            return 1;
        }
        for (const auto& [id, v] : sets)
            audio.controller->setParamNormalized (id, v);
        auto* ctlBase = dynamic_cast<pk::ControllerBase*> (audio.controller.get ());
        if (ctlBase && layoutText)
            ctlBase->uiLayout = layoutText;
        if (checkLayouts)
        {
            result = ctlBase ? checkLayoutsOf (audio.controller, ctlBase, pluginName, dumpDir, compareDir) : 1;
            audio.stop ();
            audio.provider = nullptr;
            ModuleExit ();
            return result;
        }
        std::printf ("draw benchmark: %s, median of %d draws per figure, ms per draw\n", pluginName.c_str (), ticks);

        for (double zoom : {1.0, 1.5, 2.0})
        {
            auto view = Steinberg::owned (audio.controller->createView (Vst::ViewType::kEditor));
            auto* editor = dynamic_cast<pk::EditorBase*> (view.get ());
            Recorder* recorder = nullptr;
            CFrame* frame = editor ? editor->openDetached (zoom, [&] (const CRect& r) { return recorder = new Recorder (r); }) : nullptr;
            if (!frame || !recorder)
            {
                std::printf ("FAIL: no editor\n");
                result = 1;
                break;
            }
            const CRect all (0, 0, std::round (frame->getWidth ()), std::round (frame->getHeight ()));
            auto off = COffscreenContext::create (all.getSize (), 1.0);
            if (!off)
            {
                std::printf ("FAIL: no offscreen context\n");
                result = 1;
                break;
            }
            auto drawOnce = [&] (const CRect& r) {
                off->beginDraw ();
                frame->drawRect (off, r);
                off->endDraw ();
            };
            auto timed = [&] (const CRect& r) {
                const double t0 = nowMs ();
                drawOnce (r);
                return nowMs () - t0;
            };
            // the whole window drawn both ways in the same state: the pixels must match (cached: the
            // first draw paints directly, the second makes the bitmaps, the third blits them)
            auto check = [&] (const char* state) {
                pk::CachedLayer::setEnabled (false);
                drawOnce (all);
                const Pixels direct = readPixels (off->getBitmap ());
                pk::CachedLayer::setEnabled (true);
                drawOnce (all);
                drawOnce (all);
                drawOnce (all);
                const Pixels cached = readPixels (off->getBitmap ());
                const Diff d = compare (direct, cached);
                if (d.count != 0)
                {
                    std::printf ("  FAIL (%s): cached and direct renderings differ: %ld px (largest channel difference %d)\n", state, d.count,
                                 d.maxDelta);
                    result = 1;
                    if (!dumpDir.empty ())
                    {
                        const std::string base = dumpDir + "/" + pluginName + "-" + std::to_string ((int)std::lround (zoom * 100)) + "-" + state;
                        writeFiles (off->getBitmap (), direct, base + "-direct");
                        writeFiles (off->getBitmap (), cached, base + "-cached");
                    }
                }
                else
                    std::printf ("  %s: cached = direct (largest channel difference %d)\n", state, d.maxDelta);
                return cached;
            };

            std::printf ("\nzoom %.0f %%  (%d x %d px)\n", zoom * 100, (int)all.getWidth (), (int)all.getHeight ());
            // at rest: the editor as it opens, no audio yet
            const Pixels rest = check ("at rest");
            std::printf ("  %-34s %9s %9s %7s\n", "", "direct", "cached", "ratio");
            {
                std::vector<double> d, c;
                for (int i = 0; i < ticks; ++i)
                {
                    pk::CachedLayer::setEnabled (false);
                    d.push_back (timed (all));
                    pk::CachedLayer::setEnabled (true);
                    c.push_back (timed (all));
                }
                std::printf ("  %-34s %9.3f %9.3f %6.1fx\n", "whole window, at rest", median (d), median (c),
                             median (d) / std::max (1e-6, median (c)));
            }
            char tag[32];
            std::snprintf (tag, sizeof (tag), "%.0f", zoom * 100);
            const std::string base = pluginName + "-" + tag;
            if (!dumpDir.empty ())
                writeFiles (off->getBitmap (), rest, dumpDir + "/" + base);
            if (!compareDir.empty ())
            {
                Pixels other;
                if (readFile (compareDir + "/" + base, other))
                {
                    const Diff o = compare (other, rest);
                    std::printf ("  against %s: %ld px differ (largest channel difference %d)\n", compareDir.c_str (), o.count, o.maxDelta);
                }
                else
                    std::printf ("  against %s: no rendering to compare\n", compareDir.c_str ());
            }

            // playing: every tick (~33 ms of audio, then the editor's idle) repaints what the views
            // invalidated, both ways in the same state; and each large display's own drawing (the view
            // alone, without what its rectangle brings along: the panels behind it, controls over it)
            std::vector<CView*> displays;
            findDisplays (frame, displays, allViews, frame->getViewSize ().getWidth () / zoom - 1.0);
            std::vector<Sample> samples;
            for (CView* v : displays)
                samples.push_back ({pk::typeName (typeid (*v))});
            auto timedView = [&] (CView* v) {
                off->beginDraw ();
                off->saveGlobalState ();
                const double t0 = nowMs ();
                {
                    CDrawContext::Transform tr (*off, v->getGlobalTransform ());
                    off->setClipRect (v->getViewSize ());
                    v->drawRect (off, v->getViewSize ());
                }
                const double t = nowMs () - t0;
                off->restoreGlobalState ();
                off->endDraw ();
                return t;
            };
            std::vector<std::vector<double>> dt (samples.size ()), ct (samples.size ());
            std::vector<double> tickD, tickC;
            double area = 0.0;
            for (int i = 0; i < ticks + 10; ++i)
            {
                audio.play (33.0);
                recorder->rects.clear ();
                editor->idle ();
                const std::vector<CRect> dirty = recorder->take ();
                if (i < 10)
                    continue; // (the meters come up first)
                double d = 0.0, c = 0.0;
                for (const CRect& r : dirty)
                {
                    pk::CachedLayer::setEnabled (false);
                    d += timed (r);
                    pk::CachedLayer::setEnabled (true);
                    c += timed (r);
                    area += r.getWidth () * r.getHeight ();
                }
                tickD.push_back (d);
                tickC.push_back (c);
                for (size_t k = 0; k < displays.size (); ++k)
                {
                    pk::CachedLayer::setEnabled (false);
                    dt[k].push_back (timedView (displays[k]));
                    pk::CachedLayer::setEnabled (true);
                    ct[k].push_back (timedView (displays[k]));
                }
            }
            std::printf ("  %-34s %9.3f %9.3f %6.1fx   (%.0f %% of the window repainted)\n", "playing, a tick's repaint", median (tickD),
                         median (tickC), median (tickD) / std::max (1e-6, median (tickC)), 100.0 * area / ticks / (all.getWidth () * all.getHeight ()));
            for (size_t k = 0; k < samples.size (); ++k)
            {
                samples[k].directMs = median (dt[k]);
                samples[k].cachedMs = median (ct[k]);
            }
            std::sort (samples.begin (), samples.end (), [] (const Sample& a, const Sample& b) { return a.directMs > b.directMs; });
            std::printf ("  playing, a display's own drawing:\n");
            for (const auto& s : samples)
                std::printf ("    %-32s %9.3f %9.3f %6.1fx\n", s.name.c_str (), s.directMs, s.cachedMs, s.directMs / std::max (1e-6, s.cachedMs));
            const Pixels playing = check ("playing");
            if (!dumpDir.empty ())
                writeFiles (off->getBitmap (), playing, dumpDir + "/" + base + "-playing");

            // stopped: the inputs quiet for six seconds (meters fall back, histories run out), then how
            // many repaints a second the views still ask for, and how much of the window they cover
            for (int i = 0; i < 180; ++i)
            {
                audio.play (33.0, true);
                editor->idle ();
            }
            recorder->rects.clear ();
            double quietArea = 0.0;
            size_t quietRects = 0;
            for (int i = 0; i < 30; ++i)
            {
                audio.play (33.0, true);
                editor->idle ();
                for (const CRect& r : recorder->take ())
                {
                    if (listQuiet)
                        std::printf ("    repainted when quiet: %.0f %.0f %.0f %.0f\n", r.left, r.top, r.right, r.bottom);
                    quietArea += r.getWidth () * r.getHeight ();
                    ++quietRects;
                }
            }
            std::printf ("  stopped (6 s after the audio): %zu repaints a second, %.0f %% of the window a second\n", quietRects,
                         100.0 * quietArea / (all.getWidth () * all.getHeight ()));
            view->removed ();
        }
        audio.stop ();
        audio.provider = nullptr;
    }
    ModuleExit ();
    return result;
}
