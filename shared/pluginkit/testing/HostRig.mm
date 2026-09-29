#include "HostRig.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Steinberg {
FUnknown* gStandardPluginContext = nullptr;
}

namespace pk::testing {

int gFail = 0, gChecks = 0;

void initHost ()
{
    setvbuf (stdout, nullptr, _IONBF, 0);
    [NSApplication sharedApplication];
    auto* hostApp = new HostApplication ();
    PluginContextFactory::instance ().setPluginContext (hostApp);
    gStandardPluginContext = hostApp;
}

int finish (const char* name)
{
    std::printf ("\n%s: %d checks, %d failures\n", name, gChecks, gFail);
    return gFail ? 1 : 0;
}

bool Rig::load (const std::string& path)
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

bool Rig::start (double sr, int blockSize, bool activateAux)
{
    sampleRate = sr;
    block = blockSize;
    if (activateAux)
        for (int32 i = 0; i < component->getBusCount (kAudio, kInput); ++i)
            component->activateBus (kAudio, kInput, i, true);
    ProcessSetup setup {processMode, kSample32, block, sampleRate};
    if (processor->setupProcessing (setup) != kResultOk)
        return false;
    if (component->setActive (true) != kResultOk)
        return false;
    processor->setProcessing (true);
    data.prepare (*component, block, kSample32);
    data.numSamples = block;
    data.inputEvents = &events;
    data.inputParameterChanges = &changes;
    ctx.sampleRate = sampleRate;
    ctx.tempo = 120.0;
    ctx.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid | ProcessContext::kPlaying;
    data.processContext = &ctx;
    return true;
}

void Rig::stop ()
{
    processor->setProcessing (false);
    component->setActive (false);
    data.unprepare ();
}

void Rig::note (int pitch, float vel, int offset)
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

void Rig::param (ParamID id, double norm)
{
    int32 idx;
    if (auto* q = changes.addParameterData (id, idx))
        q->addPoint (0, norm, idx);
    controller->setParamNormalized (id, norm);
}

void Rig::render (double seconds, std::vector<float>& outL, std::vector<float>* outR, const InputFn& input)
{
    const int blocks = (int)std::ceil (seconds * sampleRate / block);
    for (int b = 0; b < blocks; ++b)
    {
        for (int32 bus = 0; bus < data.numInputs; ++bus)
            for (int32 ch = 0; ch < data.inputs[bus].numChannels; ++ch)
            {
                float* buf = data.inputs[bus].channelBuffers32[ch];
                std::fill (buf, buf + block, 0.0f);
                if (input)
                    input (bus, ch, buf, block, position);
            }
        ctx.projectTimeSamples = position;
        processor->process (data);
        events.clear ();
        changes.clearQueue ();
        ctx.projectTimeMusic += block * ctx.tempo / 60.0 / sampleRate;
        position += block;
        const float* l = data.outputs[0].channelBuffers32[0];
        outL.insert (outL.end (), l, l + block);
        if (outR && data.outputs[0].numChannels > 1)
        {
            const float* r = data.outputs[0].channelBuffers32[1];
            outR->insert (outR->end (), r, r + block);
        }
    }
}

bool Rig::applyState (const std::function<bool (IBStream*)>& writer)
{
    MemoryStream s1, s2;
    if (!writer (&s1) || !writer (&s2))
        return false;
    s1.seek (0, IBStream::kIBSeekSet, nullptr);
    s2.seek (0, IBStream::kIBSeekSet, nullptr);
    return component->setState (&s1) == kResultOk && controller->setComponentState (&s2) == kResultOk;
}

void pump (double seconds)
{
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];
}

EditorWindow::EditorWindow (IEditController* controller)
{
    plugView = controller->createView (ViewType::kEditor);
    if (!plugView)
        return;
    ViewRect r;
    plugView->getSize (&r);
    NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect (0, 0, r.getWidth (), r.getHeight ())
                                                styleMask:NSWindowStyleMaskBorderless
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    win.releasedWhenClosed = NO;
    window = (__bridge_retained void*)win;
    attached = plugView->isPlatformTypeSupported (kPlatformTypeNSView) == kResultTrue &&
               plugView->attached ((__bridge void*)[win contentView], kPlatformTypeNSView) == kResultOk;
    pump (0.25);
}

EditorWindow::~EditorWindow ()
{
    if (plugView)
    {
        if (attached)
            plugView->removed ();
        plugView->release ();
    }
    if (window)
    {
        NSWindow* win = (__bridge_transfer NSWindow*)window;
        [win close];
    }
}

void EditorWindow::send (int type, double x, double y, int clicks, unsigned long mods)
{
    NSWindow* win = (__bridge NSWindow*)window;
    const double h = [[win contentView] bounds].size.height;
    NSEvent* e = [NSEvent mouseEventWithType:(NSEventType)type
                                    location:NSMakePoint (x, h - y)
                               modifierFlags:(NSEventModifierFlags)mods
                                   timestamp:[[NSProcessInfo processInfo] systemUptime]
                                windowNumber:[win windowNumber]
                                     context:nil
                                 eventNumber:0
                                  clickCount:clicks
                                    pressure:1.0];
    // Deliver straight to the view under the mouse (NSWindow won't dispatch to an off-screen window).
    NSView* content = [win contentView];
    NSView* target = (__bridge NSView*)captured;
    if (type == NSEventTypeLeftMouseDown || !target)
        target = [content hitTest:[[content superview] convertPoint:e.locationInWindow fromView:nil]];
    if (type == NSEventTypeLeftMouseDown)
    {
        captured = (__bridge void*)target;
        [target mouseDown:e];
    }
    else if (type == NSEventTypeLeftMouseDragged)
        [target mouseDragged:e];
    else
    {
        [target mouseUp:e];
        captured = nullptr;
    }
    pump (0.02);
}

void EditorWindow::mouseDown (double x, double y, int clicks, unsigned long mods) { send (NSEventTypeLeftMouseDown, x, y, clicks, mods); }
void EditorWindow::mouseDrag (double x, double y, unsigned long mods) { send (NSEventTypeLeftMouseDragged, x, y, 1, mods); }
void EditorWindow::mouseUp (double x, double y, int clicks, unsigned long mods) { send (NSEventTypeLeftMouseUp, x, y, clicks, mods); }

void EditorWindow::click (double x, double y, int clicks, unsigned long mods)
{
    mouseDown (x, y, clicks, mods);
    mouseUp (x, y, clicks, mods);
}

void EditorWindow::drag (double x0, double y0, double x1, double y1, unsigned long mods, int steps)
{
    mouseDown (x0, y0, 1, mods);
    for (int i = 1; i <= steps; ++i)
        mouseDrag (x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, mods);
    mouseUp (x1, y1, 1, mods);
}

bool EditorWindow::savePng (const std::string& file)
{
    if (!attached)
        return false;
    pump (0.2);
    NSWindow* win = (__bridge NSWindow*)window;
    NSView* content = [win contentView];
    // the plug-in's view redraws only what changed, and the window may keep more than one buffer:
    // redraw all of it, twice, so the picture is never an older buffer's partly stale one
    for (int pass = 0; pass < 2; ++pass)
    {
        [content setNeedsDisplay:YES];
        for (NSView* v in [content subviews])
            [v setNeedsDisplay:YES];
        [content display];
        pump (0.05);
    }
    NSBitmapImageRep* rep = [content bitmapImageRepForCachingDisplayInRect:[content bounds]];
    [content cacheDisplayInRect:[content bounds] toBitmapImageRep:rep];
    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    return [png writeToFile:[NSString stringWithUTF8String:file.c_str ()] atomically:YES];
}

double rms (const std::vector<float>& x, size_t a, size_t b)
{
    b = std::min (b, x.size ());
    double s = 0;
    for (size_t i = a; i < b; ++i)
        s += (double)x[i] * x[i];
    return b > a ? std::sqrt (s / (b - a)) : 0.0;
}

double dbfs (double v) { return 20.0 * std::log10 (std::max (1e-12, v)); }

bool allFinite (const std::vector<float>& x)
{
    for (float v : x)
        if (!std::isfinite (v))
            return false;
    return true;
}

size_t soundEnd (const std::vector<float>& x, float thr)
{
    size_t last = 0;
    for (size_t i = 0; i < x.size (); ++i)
        if (std::fabs (x[i]) > thr)
            last = i;
    return last;
}

int countNonAutomatable (IEditController* controller)
{
    int n = 0;
    for (int32 i = 0; i < controller->getParameterCount (); ++i)
    {
        ParameterInfo info {};
        controller->getParameterInfo (i, info);
        if (!(info.flags & ParameterInfo::kCanAutomate))
            ++n;
    }
    return n;
}

} // namespace pk::testing
