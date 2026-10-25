// End-to-end test of the built Bubr.vst3. usage: bubr_hosttest <Bubr.vst3> <output dir>
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/Editor.h"

#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace bubr;
#define CHECK PK_CHECK

static State baseState ()
{
    State st;
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        st.norm[id] = defaultNormalized (id);
        st.has[id] = true;
    }
    return st;
}

static double plainOf (Rig& rig, uint32_t id) { return toPlain (id, rig.controller->getParamNormalized (id)); }

// a kick every half second (a falling sine, 45 Hz at its end) with a bass under it, the same on both channels
static float drumsAt (long long k)
{
    const double t = (double)k / 48000.0, b = std::fmod (t, 0.5);
    const double kick = 0.6 * std::sin (2.0 * 3.14159265358979 * (45.0 * b + 2.7 * (1.0 - std::exp (-b * 30.0)))) * std::exp (-b * 6.0);
    const double a = 41.2 * t;
    return (float)(kick + 0.12 * (2.0 * (a - std::floor (a)) - 1.0));
}
static InputFn drums ()
{
    return [] (int, int, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
            buf[i] = drumsAt (pos + i);
    };
}

int main (int argc, char** argv)
{
    @autoreleasepool
    {
        if (argc < 3)
            return 2;
        initHost ();
        const std::string outDir = argv[2];
        Rig rig;
        CHECK (rig.load (argv[1]), "load");
        if (gFail)
            return finish ("bubr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (rig.component->getBusCount (kEvent, Steinberg::Vst::kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets
        // a new instance: the clips at the end off, the reference's defaults
        CHECK (rig.controller->getParamNormalized (kSoftClip) == 0.0 && rig.controller->getParamNormalized (kHardClip) == 0.0,
               "a new instance: Soft Clip and Hard Clip off");
        CHECK (std::fabs (plainOf (rig, kFreq) - 40.0) < 1e-6 && std::fabs (plainOf (rig, kBoost) - 0.55) < 1e-9 &&
                   std::fabs (plainOf (rig, kGate) + 30.0) < 1e-9,
               "a new instance: the defaults (40 Hz, Boost 55 %%, Gate -30 dB)");

        // the defaults on the drums: finite, louder than the input, nothing over 0 dBFS (the output limiter)
        State st = baseState ();
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        const uint32 latency = rig.processor->getLatencySamples ();
        CHECK (latency > 0 && latency < 200, "latency reported %u (the lookahead)", latency);
        std::vector<float> out, outR;
        rig.render (2.0, out, &outR, drums ());
        CHECK (allFinite (out) && allFinite (outR), "finite");
        float peak = 0.0f;
        for (float v : out)
            peak = std::max (peak, std::fabs (v));
        const double level = dbfs (rms (out, 48000, out.size ()));
        CHECK (peak <= 1.0f && level > -20.0 && level < -2.0, "the defaults: %.1f dBFS RMS, peak %.3f", level, peak);

        // state round trip
        rig.param (kFreq, toNormalized (kFreq, 63.0));
        rig.param (kDriveX2, 1.0);
        rig.render (0.05, out, nullptr, drums ());
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::fabs (toPlain (kFreq, back.norm[kFreq]) - 63.0) < 1e-6, "Freq saved");
        CHECK (back.norm[kDriveX2] >= 0.5, "Drive x2 saved");
        rig.param (kFreq, defaultNormalized (kFreq));
        rig.param (kDriveX2, 0.0);

        // editor (Classic): x2 clicked on and off, then a screenshot
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            ViewRect r;
            CHECK (win.view () && win.view ()->getSize (&r) == kResultOk && r.getWidth () == (int32)Editor::kWidth &&
                       r.getHeight () == (int32)(Editor::kHeight + pk::EditorBase::kInfoHeight),
                   "editor size %d x %d", r.getWidth (), r.getHeight ());
            pump (0.1);
            const double sx = Editor::kEffLeft + Editor::kKnobX[1] + Editor::kKnobW / 2.0,
                         sy = Editor::kRowTop + Editor::kKnobRow2 + 22.0 + Editor::kSwitchH / 2.0;
            win.click (sx, sy);
            pump (0.05);
            CHECK (plainOf (rig, kDriveX2) >= 0.5, "x2 clicked on");
            win.click (sx, sy);
            pump (0.05);
            CHECK (plainOf (rig, kDriveX2) < 0.5, "x2 clicked off");

            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, drums ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_bubr.png"), "screenshot");
            // Classic, Wide and Classic again: knobs found and turned in each (Wide's screenshot)
            checkLayouts (rig, win, {(uint32_t)kBoost, (uint32_t)kRatio, (uint32_t)kDryWet}, outDir + "/ui_bubr_wide.png");
        }
        rig.stop ();
        return finish ("bubr host test");
    }
}
