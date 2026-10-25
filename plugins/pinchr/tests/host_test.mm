// End-to-end test of the built Pinchr.vst3. usage: pinchr_hosttest <Pinchr.vst3> <output dir>
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
using namespace pinchr;
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

// a bass: a detuned saw pair at 55 Hz with a little noise, the same on both channels (about -18 dBFS RMS)
static float bassAt (long long k)
{
    const double t = (double)k / 48000.0;
    const double a = 55.0 * t, b = 55.0 * 1.012 * t + 0.3;
    uint32_t s = (uint32_t)k * 2654435761u;
    s ^= s >> 13;
    return (float)(0.15 * ((2.0 * (a - std::floor (a)) - 1.0) + (2.0 * (b - std::floor (b)) - 1.0)) + 0.01 * ((double)(s & 0xffff) / 32768.0 - 1.0));
}
static InputFn bass ()
{
    return [] (int, int, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
            buf[i] = bassAt (pos + i);
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
            return finish ("pinchr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (rig.component->getBusCount (kEvent, Steinberg::Vst::kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets
        // a new instance: the clips at the end off
        CHECK (rig.controller->getParamNormalized (kSoftClip) == 0.0 && rig.controller->getParamNormalized (kHardClip) == 0.0,
               "a new instance: Soft Clip and Hard Clip off");
        CHECK (std::fabs (plainOf (rig, kRatio) - 4.0) < 1e-9 && std::fabs (plainOf (rig, kDrive) - 5.0) < 1e-9 &&
                   std::lround (plainOf (rig, kCurve)) == kGermSoft,
               "a new instance: the defaults (Ratio 4, Drive 5, Germ Soft)");

        // the defaults on the bass: finite, compressed and level-ish
        State st = baseState ();
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        const uint32 latency = rig.processor->getLatencySamples ();
        CHECK (latency > 0 && latency < 300, "latency reported %u", latency);
        std::vector<float> out, outR;
        rig.render (2.0, out, &outR, bass ());
        CHECK (allFinite (out) && allFinite (outR), "finite");
        const double level = dbfs (rms (out, 48000, out.size ()));
        CHECK (level > -30.0 && level < -12.0, "the defaults: %.1f dBFS RMS", level);

        // state round trip
        rig.param (kCurve, toNormalized (kCurve, kZenerHard));
        rig.param (kFeedback, toNormalized (kFeedback, 7.5));
        rig.render (0.05, out, nullptr, bass ());
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::lround (toPlain (kCurve, back.norm[kCurve])) == kZenerHard, "Curve saved");
        CHECK (std::fabs (toPlain (kFeedback, back.norm[kFeedback]) - 7.5) < 1e-6, "Feedback saved");
        rig.param (kCurve, defaultNormalized (kCurve));
        rig.param (kFeedback, defaultNormalized (kFeedback));

        // editor (Classic): Dirty clicked on and off, then a screenshot
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            ViewRect r;
            CHECK (win.view () && win.view ()->getSize (&r) == kResultOk && r.getWidth () == (int32)Editor::kWidth &&
                       r.getHeight () == (int32)(Editor::kHeight + pk::EditorBase::kInfoHeight),
                   "editor size %d x %d", r.getWidth (), r.getHeight ());
            pump (0.1);
            const double sx = Editor::kCompLeft + 292.0 + 30.0, sy = Editor::kRowTop + Editor::kSwitchTop + Editor::kSwitchH / 2.0;
            win.click (sx, sy);
            pump (0.05);
            CHECK (plainOf (rig, kDirty) >= 0.5, "Dirty clicked on");
            win.click (sx, sy);
            pump (0.05);
            CHECK (plainOf (rig, kDirty) < 0.5, "Dirty clicked off");

            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, bass ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_pinchr.png"), "screenshot");
            // Classic, Wide and Classic again: knobs found and turned in each (Wide's screenshot)
            checkLayouts (rig, win, {(uint32_t)kRatio, (uint32_t)kFeedback, (uint32_t)pinchr::kMix}, outDir + "/ui_pinchr_wide.png");
        }
        rig.stop ();
        return finish ("pinchr host test");
    }
}
