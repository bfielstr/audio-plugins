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

// The plug-in editor attached to an offscreen window.
class EditorWindow
{
public:
    explicit EditorWindow (Steinberg::Vst::IEditController* controller);
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

private:
    void send (int type, double x, double y, int clicks, unsigned long mods);
    void* window = nullptr;
    void* captured = nullptr;
    Steinberg::IPlugView* plugView = nullptr;
    bool attached = false;
};

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

// Modifier flag values (NSEventModifierFlag*), usable without Cocoa headers.
constexpr unsigned long kShift = 1ul << 17;
constexpr unsigned long kAlt = 1ul << 19;
constexpr unsigned long kCmd = 1ul << 20;

} // namespace pk::testing
