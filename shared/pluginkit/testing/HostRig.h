// Test harness that hosts a built VST3 bundle: processing (instruments and effects, including
// side-chain buses), state, and the real editor in an offscreen window with synthetic mouse
// events and PNG snapshots. macOS only (the editor part uses Cocoa).
#pragma once

#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace pk::testing {

extern int gFail, gChecks;

#define PK_CHECK(c, ...)                                                 \
    do                                                                   \
    {                                                                    \
        ++pk::testing::gChecks;                                          \
        if (!(c))                                                        \
        {                                                                \
            ++pk::testing::gFail;                                        \
            std::printf ("  FAIL line %d: %s  ", __LINE__, #c);          \
            std::printf (__VA_ARGS__);                                   \
            std::printf ("\n");                                          \
        }                                                                \
    } while (0)

// Call first in main(): sets up the host context and Cocoa.
void initHost ();
// Prints the summary and returns the process exit code.
int finish (const char* name);

// Fills one input channel for the block starting at `pos` (absolute sample index).
using InputFn = std::function<void (int bus, int channel, float* buffer, int numSamples, long long pos)>;

struct Rig
{
    VST3::Hosting::Module::Ptr module;
    Steinberg::IPtr<Steinberg::Vst::PlugProvider> provider;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> processor;
    Steinberg::Vst::HostProcessData data;
    Steinberg::Vst::EventList events {512};
    Steinberg::Vst::ParameterChanges changes {64};
    Steinberg::Vst::ProcessContext ctx {};
    double sampleRate = 48000.0;
    int block = 480;
    Steinberg::int32 processMode = Steinberg::Vst::kRealtime; // set before start()
    long long position = 0;

    bool load (const std::string& path);
    // activateAuxInputs: switch on side-chain input buses before preparing buffers.
    bool start (double sr = 48000.0, int blockSize = 480, bool activateAuxInputs = true);
    void stop ();
    void note (int pitch, float velocity, int offset = 0);
    void param (Steinberg::Vst::ParamID id, double normalized); // sent to processor + controller
    // Processes `seconds` of audio; appends output channels 0/1 of the main bus.
    void render (double seconds, std::vector<float>& outL, std::vector<float>* outR = nullptr, const InputFn& input = {});
    // Pushes a state to both component and controller using `writer`.
    bool applyState (const std::function<bool (Steinberg::IBStream*)>& writer);
};

void pump (double seconds);

// The plug-in editor attached to an offscreen window. Every test starts from a known layout: the editor
// opens in `layout` (pk.layout.set before the view is made; by default the Classic layout, the fixed one
// the tests' coordinates are in, whatever layout a new instance would have).
class EditorWindow
{
public:
    explicit EditorWindow (Steinberg::Vst::IEditController* controller, const std::string& layout = "default", const std::string& name = "Classic");
    ~EditorWindow ();
    bool ok () const { return attached; }
    Steinberg::IPlugView* view () const { return plugView; }
    // Editor coordinates (top-left origin). mods: NSEventModifierFlags.
    void mouseDown (double x, double y, int clicks = 1, unsigned long mods = 0);
    void mouseDrag (double x, double y, unsigned long mods = 0);
    void mouseUp (double x, double y, int clicks = 1, unsigned long mods = 0);
    void click (double x, double y, int clicks = 1, unsigned long mods = 0);
    void drag (double x0, double y0, double x1, double y1, unsigned long mods = 0, int steps = 8);
    bool savePng (const std::string& file);
    // Lets the editor resize its window (a frame that takes every size the editor asks for: a layout's
    // shape, a section folding). Off by default: the editor keeps the window it opened with, as a host
    // that refuses resizing (an arranged layout is then zoomed to fit it).
    void allowResize (bool on);
    double width () const;  // the window's content now
    double height () const;

private:
    void send (int type, double x, double y, int clicks, unsigned long mods);
    void* window = nullptr;
    void* captured = nullptr;
    void* resizer = nullptr; // the IPlugFrame given to the view
    Steinberg::IPlugView* plugView = nullptr;
    bool attached = false;
};

// ---- layouts (Menu > Layout; pluginkit/Layout.h)
// The controller's open editor in another layout (pk::ControllerBase::kMsgSetLayout): "default" (or "")
// the Classic layout, "wide", or an arrangement's text; it is built in it again at once.
void setLayout (Steinberg::Vst::IEditController* controller, const std::string& text, const std::string& name = {});
// Where the control bound to parameter `id` is shown in the open editor, in the coordinates
// EditorWindow's mouse functions take (pk::ControllerBase::kMsgFindControl, through the view tree:
// right in any layout and at any zoom). False when none is shown.
struct ControlRect
{
    double left = 0, top = 0, right = 0, bottom = 0;
    double cx () const { return 0.5 * (left + right); }
    double cy () const { return 0.5 * (top + bottom); }
};
bool findControl (Steinberg::Vst::IEditController* controller, Steinberg::Vst::ParamID id, ControlRect& r);
// Classic, Wide and Classic again in an open editor (its window resizing; it must be in Classic first, as
// an EditorWindow opens): each knob of `knobs` is found in each layout (in Wide elsewhere, in the window
// made wider and shorter) and turned there by a drag (its value follows), and in Classic again it is back
// where it was. `png`: Wide's screenshot.
void checkLayouts (Rig& rig, EditorWindow& win, const std::vector<Steinberg::Vst::ParamID>& knobs, const std::string& png);

double rms (const std::vector<float>& x, size_t a, size_t b);
double dbfs (double linear);
bool allFinite (const std::vector<float>& x);
size_t soundEnd (const std::vector<float>& x, float threshold);
// Every parameter reported by the controller is automatable.
int countNonAutomatable (Steinberg::Vst::IEditController* controller);
// The Presets menu's entries as the controller reports them (pk::presets::kMsgMenu; sub-menu entries
// as "Parent/Child"), and checks that it starts with Init and has Save as Default and the other
// commands and at least one factory preset (a fresh preset folder assumed: no tag filter).
std::vector<std::string> presetMenu (Steinberg::Vst::IEditController* controller);
void checkPresetMenu (Steinberg::Vst::IEditController* controller);
// A new instance (before any state, a fresh preset folder) as the controller reports it: the end
// saturator on (`saturator`), its Gentlr on (`gentlr`) and Gentlr's Slope at Signature (`slope`, by its
// text). -1 for one the plug-in does not have.
void checkNewInstanceGentlr (Steinberg::Vst::IEditController* controller, long long saturator, long long gentlr, long long slope);

// Modifier flag values (NSEventModifierFlag*), usable without Cocoa headers.
constexpr unsigned long kShift = 1ul << 17;
constexpr unsigned long kAlt = 1ul << 19;
constexpr unsigned long kCmd = 1ul << 20;

} // namespace pk::testing
