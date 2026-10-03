// End-to-end test of the built Orbitr.vst3. usage: orbitr_hosttest <Orbitr.vst3> <output dir>
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
using namespace orbitr;
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

static double toneDb (const std::vector<float>& x, double f, size_t a, size_t b)
{
    double s = 0, c = 0;
    for (size_t i = a; i < b; ++i)
    {
        s += x[i] * std::sin (2.0 * M_PI * f * i / 48000.0);
        c += x[i] * std::cos (2.0 * M_PI * f * i / 48000.0);
    }
    return 20.0 * std::log10 (std::max (1e-12, 2.0 * std::sqrt (s * s + c * c) / (double)(b - a)));
}

// a 1 kHz sine at -12 dBFS, the same on both channels
static InputFn tone ()
{
    return [] (int, int, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
            buf[i] = (float)(0.25 * std::sin (2 * M_PI * 1000.0 * (double)(pos + i) / 48000.0));
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
            return finish ("orbitr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (rig.component->getBusCount (kEvent, kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");

        State st = baseState ();
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        const uint32 latency = rig.processor->getLatencySamples ();
        CHECK (latency >= 480 && latency < 600, "latency reported %u (10 ms and the end saturator's)", latency);

        // Dry/Wet 0 %: the tone passes at its level
        rig.param (kDryWet, 0.0);
        std::vector<float> out, outR;
        rig.render (2.0, out, &outR, tone ());
        const size_t a = 48000, b = 2 * 48000;
        const double dry = toneDb (out, 1000.0, a, b);
        CHECK (std::fabs (dry + 12.04) < 0.1, "Dry/Wet 0 %%: 1 kHz at %.2f dB", dry);
        CHECK (allFinite (out), "finite");

        // the defaults (Liquid Debris-like): the swarm moves the tone off 1 kHz and apart on the two sides
        rig.param (kDryWet, 1.0);
        out.clear ();
        outR.clear ();
        rig.render (2.0, out, &outR, tone ());
        double diff = 0.0;
        for (size_t i = a; i < b; ++i)
            diff = std::max (diff, (double)std::fabs (out[i] - outR[i]));
        const double wet = toneDb (out, 1000.0, a, b), level = dbfs (rms (out, a, b));
        CHECK (diff > 0.01, "left and right differ (%.3f)", diff);
        CHECK (wet < dry - 1.0, "the swarm moves the tone's pitch: 1 kHz at %.2f dB (dry %.2f)", wet, dry);
        CHECK (level > -24.0 && level < -9.0, "about as loud as the input: %.1f dBFS", level);
        CHECK (allFinite (out) && allFinite (outR), "finite");

        // state round trip
        rig.param (kOrbs, toNormalized (kOrbs, 9.0));
        rig.render (0.05, out, nullptr, tone ()); // (the processor takes the change with its next block)
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::lround (toPlain (kOrbs, back.norm[kOrbs])) == 9, "orbs saved");

        // editor: the Pattern switch, then a screenshot while audio is flowing (the orbs moving)
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            ViewRect r;
            CHECK (win.view () && win.view ()->getSize (&r) == kResultOk && r.getWidth () == (int32)Editor::kWidth &&
                       r.getHeight () == (int32)Editor::kHeight,
                   "editor size %d x %d", r.getWidth (), r.getHeight ());
            pump (0.1);
            const double py = Editor::kMotionTop + Editor::kPatternTop + Editor::kPatternH / 2;
            win.click (Editor::kMotionLeft + Editor::kPatternLeft + Editor::kPatternW / 4, py);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kPattern)) == kPatternOrbit, "Orbit clicked: %.0f", plainOf (rig, kPattern));
            win.click (Editor::kMotionLeft + Editor::kPatternLeft + Editor::kPatternW * 3 / 4, py);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kPattern)) == kPatternSwarm, "Swarm clicked: %.0f", plainOf (rig, kPattern));
            rig.param (kPattern, toNormalized (kPattern, kPatternSwarm));
            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, tone ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_orbitr.png"), "screenshot");
        }
        rig.stop ();
        return finish ("orbitr host test");
    }
}
