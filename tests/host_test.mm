// End-to-end test of the built Simplr.vst3 through the VST3 hosting API:
// loads the bundle, restores a state that references a sample, plays MIDI through process(),
// checks the audio, round-trips the state and renders the editor into offscreen windows.
//
// usage: simplr_hosttest <path/to/Simplr.vst3> <output dir for screenshots>

#include "Params.h"
#include "plugin/StateIO.h"

#include "dr_wav.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#import <Cocoa/Cocoa.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Steinberg {
FUnknown* gStandardPluginContext = nullptr;
}

static int gFail = 0, gChecks = 0;
#define CHECK(c, ...)                                                    \
    do                                                                   \
    {                                                                    \
        ++gChecks;                                                       \
        if (!(c))                                                        \
        {                                                                \
            ++gFail;                                                     \
            std::printf ("  FAIL line %d: %s  ", __LINE__, #c);          \
            std::printf (__VA_ARGS__);                                   \
            std::printf ("\n");                                          \
        }                                                                \
    } while (0)

static constexpr double kSr = 48000.0;
static constexpr int kBlock = 480;

// A 2-bar 120 BPM loop (4 s): kick/snare/hat bursts over a bass line.
static std::string writeTestLoop (const std::string& path)
{
    const int sr = 44100, n = sr * 4;
    std::vector<float> inter ((size_t)n * 2, 0.0f);
    uint32_t seed = 1;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (float)((seed >> 8) & 0xFFFF) / 32768.0f - 1.0f;
    };
    for (int step = 0; step < 16; ++step)
    {
        const int s0 = step * sr / 4; // eighth notes at 120 BPM
        for (int i = 0; i < sr / 4 && s0 + i < n; ++i)
        {
            const float t = (float)i / sr;
            float v = 0.0f;
            if (step % 4 == 0)
                v += 0.8f * std::sin (2.0f * (float)M_PI * (50.0f + 120.0f * std::exp (-t * 30.0f)) * t) * std::exp (-t * 9.0f);
            if (step % 4 == 2)
                v += 0.5f * rnd () * std::exp (-t * 18.0f);
            v += 0.15f * rnd () * std::exp (-t * 60.0f);
            v += 0.2f * std::sin (2.0f * (float)M_PI * (step < 8 ? 55.0f : 73.4f) * (float)(s0 + i) / sr);
            inter[(size_t)(s0 + i) * 2] += v;
            inter[(size_t)(s0 + i) * 2 + 1] += v * 0.9f;
        }
    }
    drwav_data_format fmt {};
    fmt.container = drwav_container_riff;
    fmt.format = DR_WAVE_FORMAT_PCM;
    fmt.channels = 2;
    fmt.sampleRate = (drwav_uint32)sr;
    fmt.bitsPerSample = 16;
    drwav w;
    if (!drwav_init_file_write (&w, path.c_str (), &fmt, nullptr))
        return {};
    std::vector<int16_t> pcm (inter.size ());
    for (size_t i = 0; i < inter.size (); ++i)
        pcm[i] = (int16_t)std::lround (std::clamp (inter[i], -1.0f, 1.0f) * 30000.0f);
    drwav_write_pcm_frames (&w, (drwav_uint64)n, pcm.data ());
    drwav_uninit (&w);
    return path;
}

struct Rig
{
    VST3::Hosting::Module::Ptr module;
    IPtr<PlugProvider> provider;
    IPtr<IComponent> component;
    IPtr<IEditController> controller;
    FUnknownPtr<IAudioProcessor> processor;
    HostProcessData data;
    EventList events {512};
    ParameterChanges changes {16};
    ProcessContext ctx {};

    bool load (const std::string& path)
    {
        std::string err;
        module = VST3::Hosting::Module::create (path, err);
        if (!module)
        {
            std::printf ("module error: %s\n", err.c_str ());
            return false;
        }
        auto factory = module->getFactory ();
        for (auto& ci : factory.classInfos ())
            if (ci.category () == kVstAudioEffectClass)
            {
                provider = owned (new PlugProvider (factory, ci, true));
                break;
            }
        if (!provider || !provider->initialize ())
            return false;
        component = provider->getComponentPtr ();
        controller = provider->getControllerPtr ();
        processor = FUnknownPtr<IAudioProcessor> (component);
        return component && controller && processor;
    }

    bool start ()
    {
        ProcessSetup setup {kRealtime, kSample32, kBlock, kSr};
        if (processor->setupProcessing (setup) != kResultOk)
            return false;
        if (component->setActive (true) != kResultOk)
            return false;
        processor->setProcessing (true);
        data.prepare (*component, kBlock, kSample32);
        data.numSamples = kBlock;
        data.inputEvents = &events;
        data.inputParameterChanges = &changes;
        ctx.sampleRate = kSr;
        ctx.tempo = 120.0;
        ctx.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid | ProcessContext::kPlaying;
        data.processContext = &ctx;
        return true;
    }

    void stop ()
    {
        processor->setProcessing (false);
        component->setActive (false);
        data.unprepare ();
    }

    void note (int pitch, float vel, int offset = 0)
    {
        Event e {};
        e.sampleOffset = offset;
        if (vel > 0.0f)
        {
            e.type = Event::kNoteOnEvent;
            e.noteOn.pitch = (int16)pitch;
            e.noteOn.velocity = vel;
            e.noteOn.noteId = -1;
        }
        else
        {
            e.type = Event::kNoteOffEvent;
            e.noteOff.pitch = (int16)pitch;
            e.noteOff.noteId = -1;
        }
        events.addEvent (e);
    }

    void param (ParamID id, double norm)
    {
        int32 idx;
        if (auto* q = changes.addParameterData (id, idx))
            q->addPoint (0, norm, idx);
        controller->setParamNormalized (id, norm);
    }

    // Renders `seconds` and appends the left channel to `out`.
    void render (double seconds, std::vector<float>& out, std::vector<float>* right = nullptr)
    {
        const int blocks = (int)std::ceil (seconds * kSr / kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            processor->process (data);
            events.clear ();
            changes.clearQueue ();
            ctx.projectTimeMusic += kBlock * ctx.tempo / 60.0 / kSr;
            const float* l = data.outputs[0].channelBuffers32[0];
            const float* r = data.outputs[0].channelBuffers32[1];
            out.insert (out.end (), l, l + kBlock);
            if (right)
                right->insert (right->end (), r, r + kBlock);
        }
    }
};

static double rms (const std::vector<float>& x, size_t a, size_t b)
{
    b = std::min (b, x.size ());
    double s = 0;
    for (size_t i = a; i < b; ++i)
        s += (double)x[i] * x[i];
    return b > a ? std::sqrt (s / (b - a)) : 0.0;
}

static bool allFinite (const std::vector<float>& x)
{
    for (float v : x)
        if (!std::isfinite (v))
            return false;
    return true;
}

static size_t soundEnd (const std::vector<float>& x, float thr)
{
    size_t last = 0;
    for (size_t i = 0; i < x.size (); ++i)
        if (std::fabs (x[i]) > thr)
            last = i;
    return last;
}

static bool applyState (Rig& rig, const simplr::PluginState& st)
{
    MemoryStream s1, s2;
    if (!simplr::writeState (&s1, st))
        return false;
    s1.seek (0, IBStream::kIBSeekSet, nullptr);
    if (rig.component->setState (&s1) != kResultOk)
        return false;
    simplr::writeState (&s2, st);
    s2.seek (0, IBStream::kIBSeekSet, nullptr);
    return rig.controller->setComponentState (&s2) == kResultOk;
}

static simplr::PluginState baseState (const std::string& sample)
{
    simplr::PluginState st;
    for (uint32_t i = 0; i < simplr::kNumParams; ++i)
    {
        st.norm[i] = simplr::defaultNormalized (i);
        st.has[i] = true;
    }
    st.samplePath = sample;
    auto set = [&] (uint32_t id, double plain) { st.norm[id] = simplr::toNormalized (id, plain); };
    set (simplr::kVolume, 0.0);
    set (simplr::kWarpBeats, 8);
    return st;
}

static void pump (double seconds)
{
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];
}

static bool snapshot (Rig& rig, const std::string& file, std::function<void (IPlugView*)> beforeCapture = {})
{
    IPlugView* view = rig.controller->createView (ViewType::kEditor);
    if (!view)
        return false;
    ViewRect r;
    view->getSize (&r);
    NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect (0, 0, r.getWidth (), r.getHeight ())
                                                styleMask:NSWindowStyleMaskBorderless
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    win.releasedWhenClosed = NO;
    NSView* content = [win contentView];
    const bool attached = view->isPlatformTypeSupported (kPlatformTypeNSView) == kResultTrue &&
                          view->attached ((__bridge void*)content, kPlatformTypeNSView) == kResultOk;
    bool ok = false;
    if (attached)
    {
        pump (0.3);
        if (beforeCapture)
        {
            beforeCapture (view);
            pump (0.3);
        }
        [content display];
        NSBitmapImageRep* rep = [content bitmapImageRepForCachingDisplayInRect:[content bounds]];
        [content cacheDisplayInRect:[content bounds] toBitmapImageRep:rep];
        NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        ok = [png writeToFile:[NSString stringWithUTF8String:file.c_str ()] atomically:YES];
        view->removed ();
    }
    view->release ();
    [win close];
    return ok && attached;
}

// Sends a synthetic mouse event to the window. (x, y) are editor coordinates (top-left origin).
static void mouse (NSWindow* win, NSEventType type, double x, double y, int clicks = 1, NSEventModifierFlags mods = 0)
{
    const double h = [[win contentView] bounds].size.height;
    NSEvent* e = [NSEvent mouseEventWithType:type
                                    location:NSMakePoint (x, h - y)
                               modifierFlags:mods
                                   timestamp:[[NSProcessInfo processInfo] systemUptime]
                                windowNumber:[win windowNumber]
                                     context:nil
                                 eventNumber:0
                                  clickCount:clicks
                                    pressure:1.0];
    // Deliver straight to the view under the mouse (NSWindow won't dispatch to an off-screen window).
    static NSView* captured = nil;
    NSView* content = [win contentView];
    NSView* target = captured;
    if (type == NSEventTypeLeftMouseDown || !target)
        target = [content hitTest:[[content superview] convertPoint:e.locationInWindow fromView:nil]];
    if (type == NSEventTypeLeftMouseDown)
    {
        captured = target;
        [target mouseDown:e];
    }
    else if (type == NSEventTypeLeftMouseDragged)
        [target mouseDragged:e];
    else
    {
        [target mouseUp:e];
        captured = nil;
    }
    pump (0.02);
}

static void click (NSWindow* win, double x, double y, int clicks = 1)
{
    mouse (win, NSEventTypeLeftMouseDown, x, y, clicks);
    mouse (win, NSEventTypeLeftMouseUp, x, y, clicks);
}

static void uiInteraction (Rig& rig)
{
    IPlugView* view = rig.controller->createView (ViewType::kEditor);
    ViewRect r;
    view->getSize (&r);
    NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect (0, 0, r.getWidth (), r.getHeight ())
                                                styleMask:NSWindowStyleMaskBorderless
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    win.releasedWhenClosed = NO;
    view->attached ((__bridge void*)[win contentView], kPlatformTypeNSView);
    pump (0.2);
    auto plain = [&] (uint32_t id) { return simplr::toPlain (id, rig.controller->getParamNormalized (id)); };

    // mode selector: click "Slicing", then "One-Shot"
    click (win, 480 + 260 * 2.5 / 3, 17);
    CHECK (std::lround (plain (simplr::kMode)) == simplr::kModeSlicing, "click Slicing -> mode %f", plain (simplr::kMode));
    click (win, 480 + 260 * 1.5 / 3, 17);
    CHECK (std::lround (plain (simplr::kMode)) == simplr::kModeOneShot, "click One-Shot -> mode %f", plain (simplr::kMode));
    click (win, 480 + 260 * 0.5 / 3, 17);

    // warp toggle
    const double warpBefore = plain (simplr::kWarp);
    click (win, 780, 17);
    CHECK (plain (simplr::kWarp) != warpBefore, "warp toggle didn't change");
    click (win, 780, 17);

    // drag the filter frequency knob down by 50 px (knob at 16..72 x 624..688)
    const double before = rig.controller->getParamNormalized (simplr::kFilterFreq);
    mouse (win, NSEventTypeLeftMouseDown, 44, 650);
    for (int i = 1; i <= 10; ++i)
        mouse (win, NSEventTypeLeftMouseDragged, 44, 650 + i * 5);
    mouse (win, NSEventTypeLeftMouseUp, 44, 700);
    const double after = rig.controller->getParamNormalized (simplr::kFilterFreq);
    CHECK (std::fabs ((before - after) - 0.25) < 0.02, "knob drag: %f -> %f", before, after);
    // double-click resets to the default
    click (win, 44, 650, 1);
    mouse (win, NSEventTypeLeftMouseDown, 44, 650, 2);
    mouse (win, NSEventTypeLeftMouseUp, 44, 650, 2);
    CHECK (std::fabs (rig.controller->getParamNormalized (simplr::kFilterFreq) - simplr::defaultNormalized (simplr::kFilterFreq)) < 1e-6,
           "double-click reset: %f", rig.controller->getParamNormalized (simplr::kFilterFreq));

    // --- loop bar (Classic, state has loop on, flags 0.1..0.8, start 10 %, length 80 %, loop 40 %) ---
    auto wx = [] (double pos) { return 8.0 + pos * 1094.0; };
    const double rs = 0.1 + 0.1 * 0.7, re = rs + 0.8 * 0.7, ls = re - 0.4 * (re - rs);
    const double barX = wx ((ls + re) / 2), barY = 285;
    CHECK (plain (simplr::kLoopOn) >= 0.5, "loop should start on");
    click (win, barX, barY);
    CHECK (plain (simplr::kLoopOn) < 0.5, "clicking the loop bar should switch looping off");
    click (win, barX, barY);
    CHECK (plain (simplr::kLoopOn) >= 0.5, "clicking again should switch it back on");
    const double loopFramesBefore = plain (simplr::kLoopLen) * (re - rs);
    mouse (win, NSEventTypeLeftMouseDown, barX, barY);
    mouse (win, NSEventTypeLeftMouseDragged, barX - 40, barY);
    mouse (win, NSEventTypeLeftMouseDragged, barX - 84, barY);
    mouse (win, NSEventTypeLeftMouseUp, barX - 84, barY);
    const double newRe = rs + plain (simplr::kLength) * 0.7;
    CHECK (std::fabs (newRe - (re - 84.0 / 1094.0)) < 0.004, "loop drag moved the end to %f (want %f)", newRe,
           re - 84.0 / 1094.0);
    CHECK (std::fabs (plain (simplr::kLoopLen) * (newRe - rs) - loopFramesBefore) < 0.004,
           "loop length should be kept while moving (%f vs %f)", plain (simplr::kLoopLen) * (newRe - rs), loopFramesBefore);
    CHECK (plain (simplr::kLoopOn) >= 0.5, "dragging must not toggle the loop");

    // --- envelope display: shift-drag bends a curve, double-click adds / removes breakpoints ---
    // amp envelope plot area in the editor: x 402..688, y 476..606
    const double ax = 402, aw = 286, atop = 476, ah = 130;
    auto envGeom = [&] (double& peakX, double& decayEndX, double& holdEndX, double& relEndX, std::vector<double>& ptX) {
        auto w = [] (double ms) { return std::log10 (1.0 + ms / 2.0) + 0.05; };
        const int n = (int)std::lround (plain (simplr::envParam (0, simplr::kEnvPointCount)));
        double units = w (plain (simplr::kAmpA)) + w (plain (simplr::kAmpD)) + 1.2 + w (plain (simplr::kAmpR));
        for (int i = 0; i < n; ++i)
            units += w (plain (simplr::envPointParam (0, i, simplr::kPtTime)));
        const double sc = aw / units;
        double x = ax + w (plain (simplr::kAmpA)) * sc;
        peakX = x;
        ptX.clear ();
        for (int i = 0; i < n; ++i)
        {
            x += w (plain (simplr::envPointParam (0, i, simplr::kPtTime))) * sc;
            ptX.push_back (x);
        }
        decayEndX = x + w (plain (simplr::kAmpD)) * sc;
        holdEndX = decayEndX + 1.2 * sc;
        relEndX = holdEndX + w (plain (simplr::kAmpR)) * sc;
    };
    double peakX, decayX, holdX, relX;
    std::vector<double> ptX;
    envGeom (peakX, decayX, holdX, relX, ptX);
    const double curveBefore = plain (simplr::envParam (0, simplr::kEnvCurveR));
    const double rx = (holdX + relX) / 2, ry = atop + ah * 0.6;
    mouse (win, NSEventTypeLeftMouseDown, rx, ry, 1, NSEventModifierFlagShift);
    mouse (win, NSEventTypeLeftMouseDragged, rx, ry - 20, 1, NSEventModifierFlagShift);
    mouse (win, NSEventTypeLeftMouseDragged, rx, ry - 40, 1, NSEventModifierFlagShift);
    mouse (win, NSEventTypeLeftMouseUp, rx, ry - 40, 1, NSEventModifierFlagShift);
    const double curveAfter = plain (simplr::envParam (0, simplr::kEnvCurveR));
    CHECK (curveAfter > curveBefore + 0.3, "shift-drag up on the release should bow it up: %f -> %f", curveBefore, curveAfter);

    const double addX = peakX + (decayX - peakX) * 0.45, addY = atop + ah * 0.5;
    mouse (win, NSEventTypeLeftMouseDown, addX, addY, 2);
    mouse (win, NSEventTypeLeftMouseUp, addX, addY, 2);
    CHECK (std::lround (plain (simplr::envParam (0, simplr::kEnvPointCount))) == 1, "double-click should add a breakpoint (count %f)",
           plain (simplr::envParam (0, simplr::kEnvPointCount)));
    CHECK (std::fabs (plain (simplr::envPointParam (0, 0, simplr::kPtLevel)) - 0.5) < 0.03, "breakpoint level %f",
           plain (simplr::envPointParam (0, 0, simplr::kPtLevel)));
    const double total = plain (simplr::envPointParam (0, 0, simplr::kPtTime)) + plain (simplr::kAmpD);
    CHECK (std::fabs (total - 600.0) < 6.0, "splitting keeps the decay's total time: %f ms", total);
    envGeom (peakX, decayX, holdX, relX, ptX);
    if (!ptX.empty ())
    {
        const double py = atop + ah * (1.0 - plain (simplr::envPointParam (0, 0, simplr::kPtLevel)));
        mouse (win, NSEventTypeLeftMouseDown, ptX[0], py, 2);
        mouse (win, NSEventTypeLeftMouseUp, ptX[0], py, 2);
        CHECK (std::lround (plain (simplr::envParam (0, simplr::kEnvPointCount))) == 0, "double-click on it should remove it");
        CHECK (std::fabs (plain (simplr::kAmpD) - 600.0) < 6.0, "removing merges the time back: %f", plain (simplr::kAmpD));
    }

    // --- help toggle ---
    auto tipsOn = [&] {
        MemoryStream ms;
        rig.controller->getState (&ms);
        ms.seek (0, IBStream::kIBSeekSet, nullptr);
        double scale = 0;
        bool tips = false;
        int32 n = 0;
        ms.read (&scale, sizeof (scale), &n);
        ms.read (&tips, sizeof (tips), &n);
        return tips;
    };
    CHECK (tipsOn (), "help tooltips default to on");
    click (win, 1033, 17);
    CHECK (!tipsOn (), "the ? button should switch help off");
    click (win, 1033, 17);
    CHECK (tipsOn (), "and back on");

    // drag the sample start flag (waveform x 8..1102) to the middle
    const double flagX = 8.0 + plain (simplr::kSampleStart) * 1094.0;
    mouse (win, NSEventTypeLeftMouseDown, flagX, 150);
    mouse (win, NSEventTypeLeftMouseDragged, 200, 150);
    mouse (win, NSEventTypeLeftMouseDragged, 555, 150);
    mouse (win, NSEventTypeLeftMouseUp, 555, 150);
    CHECK (std::fabs (plain (simplr::kSampleStart) - 0.5) < 0.02, "flag drag -> start %f", plain (simplr::kSampleStart));

    view->removed ();
    view->release ();
    [win close];
}

int main (int argc, char** argv)
{
    @autoreleasepool
    {
        if (argc < 3)
        {
            std::printf ("usage: %s <Simplr.vst3> <outdir>\n", argv[0]);
            return 2;
        }
        setvbuf (stdout, nullptr, _IONBF, 0);
        [NSApplication sharedApplication];
        const std::string outDir = argv[2];
        const std::string wav = writeTestLoop (outDir + "/test_loop.wav");
        CHECK (!wav.empty (), "could not write test wav");

        auto* hostApp = new HostApplication ();
        PluginContextFactory::instance ().setPluginContext (hostApp);
        gStandardPluginContext = hostApp;

        Rig rig;
        CHECK (rig.load (argv[1]), "load failed");
        if (gFail)
            return 1;

        // --- controller surface ---------------------------------------------------
        const int32 count = rig.controller->getParameterCount ();
        CHECK (count == (int32)simplr::kNumParams + 3, "param count %d", count);
        String128 str;
        rig.controller->getParamStringByValue (simplr::kFilterFreq, 1.0, str);
        const std::string freqText = StringConvert::convert (std::u16string (reinterpret_cast<const char16_t*> (str)));
        CHECK (freqText == "22.00 kHz", "freq text '%s'", freqText.c_str ());
        ParamValue parsed = -1;
        rig.controller->getParamValueByString (simplr::kFilterFreq, (TChar*)u"1 kHz", parsed);
        CHECK (std::fabs (simplr::toPlain (simplr::kFilterFreq, parsed) - 1000.0) < 1.0, "parse 1 kHz -> %f", parsed);
        FUnknownPtr<IMidiMapping> mm (rig.controller);
        ParamID pid = 0;
        CHECK (mm && mm->getMidiControllerAssignment (0, 3, kPitchBend, pid) == kResultTrue &&
                   pid == simplr::kMidiPitchBend,
               "pitch bend mapping");
        int notAutomatable = 0;
        for (int32 i = 0; i < count; ++i)
        {
            ParameterInfo info {};
            rig.controller->getParameterInfo (i, info);
            if (!(info.flags & ParameterInfo::kCanAutomate))
                ++notAutomatable;
        }
        CHECK (notAutomatable == 0, "%d parameters are not automatable", notAutomatable);
        CHECK (mm && mm->getMidiControllerAssignment (0, 0, kCtrlSustainOnOff, pid) == kResultTrue &&
                   pid == simplr::kMidiSustain,
               "sustain mapping");

        // --- restore state that references the sample -------------------------------
        auto st = baseState (wav);
        CHECK (applyState (rig, st), "setState");
        CHECK (rig.start (), "start processing");

        // Classic: note on/off
        std::vector<float> out;
        rig.note (60, 1.0f);
        rig.render (0.5, out);
        rig.note (60, 0.0f);
        rig.render (0.5, out);
        CHECK (allFinite (out), "non-finite output");
        CHECK (rms (out, 0, 24000) > 0.05, "classic silent: %f", rms (out, 0, 24000));
        CHECK (rms (out, 36000, 48000) < 1e-4, "classic didn't release: %f", rms (out, 36000, 48000));

        // Parameter automation through process(): close the filter
        rig.param (simplr::kFilterType, simplr::toNormalized (simplr::kFilterType, simplr::kHighpass));
        rig.param (simplr::kFilterFreq, simplr::toNormalized (simplr::kFilterFreq, 6000.0));
        rig.param (simplr::kFilterSlope, 1.0);
        out.clear ();
        rig.note (60, 1.0f);
        rig.render (0.5, out);
        rig.note (60, 0.0f);
        rig.render (0.2, out);
        const double closed = rms (out, 4800, 24000);
        rig.param (simplr::kFilterType, 0.0);
        rig.param (simplr::kFilterFreq, 1.0);
        rig.param (simplr::kFilterSlope, 0.0);
        out.clear ();
        rig.note (60, 1.0f);
        rig.render (0.5, out);
        rig.note (60, 0.0f);
        rig.render (0.2, out);
        const double open = rms (out, 4800, 24000);
        CHECK (closed < open * 0.25, "filter automation: closed %f open %f", closed, open);

        // Slicing: 16 region slices, note C1+4 plays the 5th slice only
        rig.param (simplr::kMode, simplr::toNormalized (simplr::kMode, simplr::kModeSlicing));
        rig.param (simplr::kSliceBy, simplr::toNormalized (simplr::kSliceBy, simplr::kSliceRegion));
        rig.param (simplr::kRegions, simplr::toNormalized (simplr::kRegions, 3));
        out.clear ();
        rig.render (0.05, out);
        out.clear ();
        rig.note (simplr::kSliceBaseNote + 4, 1.0f);
        rig.render (0.6, out);
        const double sliceSecs = soundEnd (out, 1e-4f) / kSr;
        CHECK (sliceSecs > 0.15 && sliceSecs < 0.3, "slice length %f s (want ~0.25)", sliceSecs);

        // Warp: 8 beats over 4 s at 120 BPM; at 60 BPM a one-shot lasts 8 s
        rig.param (simplr::kMode, simplr::toNormalized (simplr::kMode, simplr::kModeOneShot));
        rig.param (simplr::kWarp, 1.0);
        rig.param (simplr::kWarpMode, simplr::toNormalized (simplr::kWarpMode, simplr::kWarpComplex));
        rig.ctx.tempo = 60.0;
        out.clear ();
        rig.note (60, 1.0f);
        rig.render (9.0, out);
        const double warpSecs = soundEnd (out, 1e-3f) / kSr;
        CHECK (std::fabs (warpSecs - 8.0) < 0.3, "warped length %f s (want 8)", warpSecs);
        CHECK (allFinite (out), "warp non-finite");
        rig.ctx.tempo = 120.0;
        rig.param (simplr::kWarp, 0.0);
        rig.param (simplr::kMode, 0.0);

        // Sustain pedal via MIDI-mapped parameter
        rig.param (simplr::kLoopOn, 1.0);
        out.clear ();
        rig.param (simplr::kMidiSustain, 1.0);
        rig.note (60, 1.0f);
        rig.render (0.1, out);
        rig.note (60, 0.0f);
        rig.render (0.4, out);
        CHECK (rms (out, 19200, 24000) > 0.02, "sustain pedal didn't hold");
        rig.param (simplr::kMidiSustain, 0.0);
        rig.render (0.3, out);
        CHECK (rms (out, out.size () - 4800, out.size ()) < 1e-4, "pedal up didn't release");
        rig.param (simplr::kLoopOn, 0.0);

        // Polyphony + CPU
        rig.param (simplr::kLoopOn, 1.0);
        const auto t0 = std::chrono::steady_clock::now ();
        for (int i = 0; i < 16; ++i)
            rig.note (36 + i * 3, 0.8f);
        out.clear ();
        rig.render (2.0, out);
        const double cpu = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count () / 2.0 * 100.0;
        std::printf ("  16-voice CPU through the plug-in: %.1f%% of one core\n", cpu);
        CHECK (allFinite (out), "poly non-finite");
        CHECK (cpu < 30.0, "too slow: %f%%", cpu);
        for (int i = 0; i < 16; ++i)
            rig.note (36 + i * 3, 0.0f);
        rig.render (0.2, out);
        rig.param (simplr::kLoopOn, 0.0);

        // --- state round trip ----------------------------------------------------
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        simplr::PluginState back;
        CHECK (simplr::readState (&saved, back), "readState of plug-in output");
        CHECK (back.samplePath == wav, "path '%s'", back.samplePath.c_str ());
        CHECK (std::fabs (back.norm[simplr::kVolume] - st.norm[simplr::kVolume]) < 1e-9, "volume not saved");
        CHECK (std::fabs (back.norm[simplr::kRegions] - simplr::toNormalized (simplr::kRegions, 3)) < 1e-9,
               "automated param not saved");

        // States from 0.1.x (no loop-fade parameter) migrate the old flag
        {
            auto old = baseState (wav);
            old.has[simplr::kLoopFadePower] = false;
            for (uint32_t id = simplr::kEnvExtBase; id < simplr::kNumParams; ++id)
                old.has[id] = false;
            old.constantPowerFade = false;
            rig.stop ();
            CHECK (applyState (rig, old), "old-format state");
            MemoryStream s3;
            rig.component->getState (&s3);
            s3.seek (0, IBStream::kIBSeekSet, nullptr);
            simplr::PluginState b3;
            simplr::readState (&s3, b3);
            CHECK (b3.norm[simplr::kLoopFadePower] == 0.0, "linear fade should migrate: %f", b3.norm[simplr::kLoopFadePower]);
            CHECK (std::fabs (b3.norm[simplr::envParam (0, simplr::kEnvCurveD)] -
                              simplr::defaultNormalized (simplr::envParam (0, simplr::kEnvCurveD))) < 1e-9,
                   "missing new params should get defaults");
            rig.start ();
        }

        // Missing sample: loads without crashing, path is preserved
        auto missing = baseState ("/nonexistent/folder/gone.wav");
        rig.stop ();
        CHECK (applyState (rig, missing), "setState with missing file");
        MemoryStream saved2;
        rig.component->getState (&saved2);
        saved2.seek (0, IBStream::kIBSeekSet, nullptr);
        simplr::PluginState back2;
        simplr::readState (&saved2, back2);
        CHECK (back2.samplePath == missing.samplePath, "missing path lost: '%s'", back2.samplePath.c_str ());
        CHECK (rig.start (), "restart");
        out.clear ();
        rig.note (60, 1.0f);
        rig.render (0.2, out);
        CHECK (rms (out, 0, out.size ()) == 0.0, "no sample should be silent");
        rig.note (60, 0.0f);
        rig.stop ();

        // --- editor screenshots --------------------------------------------------
        auto st2 = baseState (wav);
        applyState (rig, st2);
        CHECK (rig.start (), "restart 2");
        rig.note (60, 1.0f);
        rig.render (0.3, out);
        CHECK (snapshot (rig, outDir + "/ui_classic.png"), "classic snapshot");
        rig.note (60, 0.0f);
        rig.render (0.3, out);

        auto set = [&] (uint32_t id, double plain) { rig.controller->setParamNormalized (id, simplr::toNormalized (id, plain)); };
        auto st3 = baseState (wav);
        st3.norm[simplr::kMode] = simplr::toNormalized (simplr::kMode, simplr::kModeSlicing);
        st3.norm[simplr::kWarp] = 1.0;
        st3.norm[simplr::kFilterFreq] = simplr::toNormalized (simplr::kFilterFreq, 1800.0);
        st3.norm[simplr::kFilterRes] = 0.6;
        st3.norm[simplr::kFilterSlope] = 1.0;
        st3.norm[simplr::kFilterCircuit] = simplr::toNormalized (simplr::kFilterCircuit, simplr::kPRD);
        st3.norm[simplr::kLfoOn] = 1.0;
        st3.norm[simplr::kLfoPitch] = 0.2;
        st3.edits.manual.push_back (0.33);
        rig.stop ();
        applyState (rig, st3);
        rig.start ();
        CHECK (snapshot (rig, outDir + "/ui_slicing.png", [&] (IPlugView*) { (void)set; }), "slicing snapshot");

        auto st4 = baseState (wav);
        st4.norm[simplr::kLoopOn] = 1.0;
        st4.norm[simplr::kSampleStart] = 0.1;
        st4.norm[simplr::kSampleEnd] = 0.8;
        st4.norm[simplr::kStart] = 0.1;
        st4.norm[simplr::kLength] = 0.8;
        st4.norm[simplr::kLoopLen] = 0.4;
        st4.norm[simplr::kFilterType] = simplr::toNormalized (simplr::kFilterType, simplr::kMorph);
        st4.norm[simplr::kFilterMorph] = 0.35;
        st4.norm[simplr::kFilterRes] = 0.5;
        st4.norm[simplr::kFilterFreq] = simplr::toNormalized (simplr::kFilterFreq, 900.0);
        rig.stop ();
        applyState (rig, st4);
        rig.start ();
        CHECK (snapshot (rig, outDir + "/ui_classic_loop.png"), "loop snapshot");
        rig.stop ();

        rig.start ();
        uiInteraction (rig);
        rig.stop ();

        // breakpoint envelope + loop-off screenshot
        {
            auto st5 = baseState (wav);
            auto setp = [&] (uint32_t id, double v) { st5.norm[id] = simplr::toNormalized (id, v); };
            setp (simplr::kAmpA, 30.0);
            setp (simplr::kAmpD, 300.0);
            setp (simplr::kAmpS, 0.45);
            setp (simplr::kAmpR, 800.0);
            setp (simplr::envParam (0, simplr::kEnvCurveA), 0.6);
            setp (simplr::envParam (0, simplr::kEnvCurveR), -0.8);
            setp (simplr::envParam (0, simplr::kEnvPointCount), 2);
            setp (simplr::envPointParam (0, 0, simplr::kPtTime), 120.0);
            setp (simplr::envPointParam (0, 0, simplr::kPtLevel), 0.25);
            setp (simplr::envPointParam (0, 0, simplr::kPtCurve), -0.7);
            setp (simplr::envPointParam (0, 1, simplr::kPtTime), 200.0);
            setp (simplr::envPointParam (0, 1, simplr::kPtLevel), 0.8);
            setp (simplr::envPointParam (0, 1, simplr::kPtCurve), 0.5);
            setp (simplr::kLoopOn, 0.0);
            setp (simplr::kLength, 0.7);
            applyState (rig, st5);
            rig.start ();
            CHECK (snapshot (rig, outDir + "/ui_envelope_points.png"), "envelope snapshot");
            rig.stop ();
        }

        // editor resize constraint keeps the aspect ratio
        if (IPlugView* v = rig.controller->createView (ViewType::kEditor))
        {
            ViewRect r (0, 0, 1665, 900);
            CHECK (v->canResize () == kResultTrue, "resizable");
            v->checkSizeConstraint (&r);
            CHECK (std::abs (r.getWidth () * 724 - r.getHeight () * 1110) < 1110, "aspect %dx%d", r.getWidth (),
                   r.getHeight ());
            v->release ();
        }

        rig.provider = nullptr;
        rig.module = nullptr;
        std::printf ("\nhost test: %d checks, %d failures\n", gChecks, gFail);
        return gFail ? 1 : 0;
    }
}
