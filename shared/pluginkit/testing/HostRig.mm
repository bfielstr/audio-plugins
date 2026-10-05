#include "HostRig.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

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
    // an empty preset folder of its own: a default saved on this computer (Save as Default) must not
    // change what a new instance starts from in the tests
    if (!std::getenv ("PK_PRESETS_DIR"))
    {
        const std::string dir = std::string (NSTemporaryDirectory ().UTF8String) + "pk-hosttest-presets-" + std::to_string ((long)getpid ());
        setenv ("PK_PRESETS_DIR", dir.c_str (), 1);
    }
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

namespace {
// The window's side of the editor (IPlugFrame): resizes the window to what the editor asks for, while
// allowed, and tells the view its new size as a host does.
class Resizer : public IPlugFrame
{
public:
    NSWindow* win = nil;
    bool allow = false;
    tresult PLUGIN_API resizeView (IPlugView* view, ViewRect* r) override
    {
        if (!allow || !view || !r || !win)
            return kResultFalse;
        [win setContentSize:NSMakeSize (r->getWidth (), r->getHeight ())];
        view->onSize (r);
        return kResultTrue;
    }
    tresult PLUGIN_API queryInterface (const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (iid, IPlugFrame::iid) || FUnknownPrivate::iidEqual (iid, FUnknown::iid))
        {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef () override { return 1; } // (owned by the EditorWindow)
    uint32 PLUGIN_API release () override { return 1; }
};
} // namespace

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
    auto* rs = new Resizer ();
    rs->win = win;
    resizer = rs;
    plugView->setFrame (rs);
    // the layout check (pk::layoutReport): the editor appends what overlaps, touches or spills to the
    // file in PK_LAYOUT_REPORT when it opens; printed here the first time an editor opens in this test
    // (a note for whoever reads the log, not a failure: some layouts wait for their rework)
    static bool reported = false;
    const std::string report = std::string ([NSTemporaryDirectory () UTF8String]) + "pk_layout_report.txt";
    if (!reported)
    {
        std::remove (report.c_str ());
        setenv ("PK_LAYOUT_REPORT", report.c_str (), 1);
    }
    attached = plugView->isPlatformTypeSupported (kPlatformTypeNSView) == kResultTrue &&
               plugView->attached ((__bridge void*)[win contentView], kPlatformTypeNSView) == kResultOk;
    if (!reported)
    {
        reported = true;
        unsetenv ("PK_LAYOUT_REPORT");
        if (FILE* f = std::fopen (report.c_str (), "r"))
        {
            char line[1024];
            while (std::fgets (line, sizeof (line), f))
                std::printf ("  layout %s", line);
            std::fclose (f);
        }
        std::remove (report.c_str ());
    }
    pump (0.25);
}

EditorWindow::~EditorWindow ()
{
    if (plugView)
    {
        if (attached)
            plugView->removed ();
        plugView->setFrame (nullptr);
        plugView->release ();
    }
    delete static_cast<Resizer*> (resizer);
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

void EditorWindow::allowResize (bool on)
{
    if (resizer)
        static_cast<Resizer*> (resizer)->allow = on;
}

double EditorWindow::width () const
{
    return window ? [[(__bridge NSWindow*)window contentView] bounds].size.width : 0.0;
}

double EditorWindow::height () const
{
    return window ? [[(__bridge NSWindow*)window contentView] bounds].size.height : 0.0;
}

void setLayout (IEditController* controller, const std::string& text, const std::string& name)
{
    FUnknownPtr<IConnectionPoint> cp (controller);
    if (!cp)
        return;
    auto msg = owned (new HostMessage ());
    msg->setMessageID ("pk.layout.set");
    msg->getAttributes ()->setBinary ("text", text.data (), (uint32)text.size ());
    msg->getAttributes ()->setBinary ("name", name.data (), (uint32)name.size ());
    cp->notify (msg);
    pump (0.1);
}

bool findControl (IEditController* controller, ParamID id, ControlRect& r)
{
    FUnknownPtr<IConnectionPoint> cp (controller);
    if (!cp)
        return false;
    auto msg = owned (new HostMessage ());
    msg->setMessageID ("pk.ui.find");
    msg->getAttributes ()->setInt ("id", (int64)id);
    cp->notify (msg);
    const void* data = nullptr;
    uint32 size = 0;
    if (msg->getAttributes ()->getBinary ("rect", data, size) != kResultOk || !data || size != 4 * sizeof (double))
        return false;
    const double* v = static_cast<const double*> (data);
    r = {v[0], v[1], v[2], v[3]};
    return true;
}

void checkLayouts (Rig& rig, EditorWindow& win, const std::vector<ParamID>& knobs, const std::string& png)
{
    win.allowResize (true);
    const double w0 = win.width (), h0 = win.height ();
    std::vector<ControlRect> before (knobs.size ());
    for (size_t i = 0; i < knobs.size (); ++i)
        PK_CHECK (findControl (rig.controller, knobs[i], before[i]), "Default: the control of parameter %u is found", knobs[i]);
    // a knob turned by a drag up: its value rises (then put back)
    auto turn = [&] (ParamID id, const ControlRect& r, const char* where) {
        const double v0 = rig.controller->getParamNormalized (id);
        rig.param (id, 0.25);
        win.drag (r.cx (), r.cy (), r.cx (), r.cy () - 30.0);
        pump (0.05);
        const double v1 = rig.controller->getParamNormalized (id);
        PK_CHECK (v1 > 0.26, "%s: dragging the knob of parameter %u up turns it (%.3f)", where, id, v1);
        rig.param (id, v0);
    };
    setLayout (rig.controller, "wide", "Wide");
    pump (0.2);
    PK_CHECK (win.width () > w0 && win.height () <= h0 + 1, "Wide: the window wider and not taller (%.0f x %.0f -> %.0f x %.0f)", w0, h0, win.width (),
              win.height ());
    bool moved = false;
    for (size_t i = 0; i < knobs.size (); ++i)
    {
        ControlRect r;
        const bool found = findControl (rig.controller, knobs[i], r);
        PK_CHECK (found, "Wide: the control of parameter %u is found", knobs[i]);
        if (!found)
            continue;
        PK_CHECK (r.left >= 0 && r.top >= 0 && r.right <= win.width () + 0.5 && r.bottom <= win.height () + 0.5,
                  "Wide: the control of parameter %u is in the window", knobs[i]);
        moved = moved || std::fabs (r.left - before[i].left) > 1 || std::fabs (r.top - before[i].top) > 1;
        turn (knobs[i], r, "Wide");
    }
    PK_CHECK (moved, "Wide: the controls are elsewhere than in the Default");
    if (!png.empty ())
        PK_CHECK (win.savePng (png), "screenshot, Wide");
    setLayout (rig.controller, "", "Default");
    pump (0.2);
    // (as wide as it was; as tall, or shorter by a section folded since it opened, which a window that
    // may resize now follows)
    PK_CHECK (std::fabs (win.width () - w0) < 1 && win.height () <= h0 + 1, "Default again: the window as it was (%.0f x %.0f)", win.width (),
              win.height ());
    for (size_t i = 0; i < knobs.size (); ++i)
    {
        ControlRect r;
        PK_CHECK (findControl (rig.controller, knobs[i], r) && std::fabs (r.left - before[i].left) < 0.5 && std::fabs (r.top - before[i].top) < 0.5,
                  "Default again: the control of parameter %u where it was", knobs[i]);
        turn (knobs[i], r, "Default again");
    }
    win.allowResize (false);
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

std::vector<std::string> presetMenu (IEditController* controller)
{
    std::vector<std::string> out;
    FUnknownPtr<IConnectionPoint> cp (controller);
    if (!cp)
        return out;
    // pk::presets::kMsgMenu: the controller answers on the message itself
    auto msg = owned (new HostMessage ());
    msg->setMessageID ("pk.preset.menu");
    cp->notify (msg);
    const void* data = nullptr;
    uint32 size = 0;
    if (msg->getAttributes ()->getBinary ("items", data, size) != kResultOk || !data)
        return out;
    std::string cur;
    for (const char* p = (const char*)data; p < (const char*)data + size; ++p)
        if (*p == '\n')
        {
            out.push_back (cur);
            cur.clear ();
        }
        else
            cur += *p;
    return out;
}

void checkPresetMenu (IEditController* controller)
{
    const auto items = presetMenu (controller);
    PK_CHECK (!items.empty () && items[0] == "Init", "the Presets menu starts with Init (%zu entries)", items.size ());
    auto has = [&] (const char* t) { return std::find (items.begin (), items.end (), std::string (t)) != items.end (); };
    for (const char* t : {"Save", "Save As...", "Rename...", "Edit Tags...", "Delete...", "Save as Default", "Load Default", "Reset Default",
                          "Load Preset File...", "Tags/All"})
        PK_CHECK (has (t), "the Presets menu has %s", t);
    // at least one factory preset: an entry inside a category sub-menu other than Tags
    bool factory = false;
    for (const auto& i : items)
        factory = factory || (i.find ('/') != std::string::npos && i.rfind ("Tags", 0) != 0);
    PK_CHECK (factory, "the Presets menu lists factory presets");
}

} // namespace pk::testing
