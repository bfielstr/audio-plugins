// End-to-end test of the built Deepr.vst3. usage: deepr_hosttest <Deepr.vst3> <output dir>
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/Editor.h"

#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace deepr;
#define CHECK PK_CHECK

static State baseState ()
{
    State st;
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        st.norm[id] = defaultNormalized (id);
        st.has[id] = true;
    }
    // the end saturator as a new instance had it up to 0.24 (off, its Gentlr off, 12 / 12): the checks are
    // about deepr's own sound (a new instance has it on: checkNewInstanceGentlr)
    st.norm[kTailBase + pk::kTailOn] = 0.0;
    st.norm[kTailExtBase + pk::kTailExtClarity] = 0.0;
    st.norm[kTailExt3Base + pk::kTailExt3Slope] = 0.0;
    return st;
}

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

// sines at the given frequencies and levels (dBFS), the same on both channels
static InputFn tones (std::vector<std::pair<double, double>> freqDb)
{
    return [freqDb] (int, int, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
        {
            const double t = (double)(pos + i) / 48000.0;
            double v = 0.0;
            for (auto [f, db] : freqDb)
                v += std::pow (10.0, db / 20.0) * std::sin (2 * M_PI * f * t);
            buf[i] = (float)v;
        }
    };
}

// a bass note: the sub well over the threshold, a low-mid harmonic and some top
static InputFn bass () { return tones ({{45.0, -6.0}, {250.0, -18.0}, {3000.0, -26.0}}); }

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
            return finish ("deepr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (rig.component->getBusCount (kEvent, kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets
        checkNewInstanceGentlr (rig.controller, kTailBase + pk::kTailOn, kTailExtBase + pk::kTailExtClarity, kTailExt3Base + pk::kTailExt3Slope);

        State st = baseState ();
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        const uint32 latency = rig.processor->getLatencySamples ();
        CHECK (latency < 120, "latency reported %u (the end saturator's only)", latency);

        // neutral (Depth 0, stereo sub): an all-pass of the input, every tone keeps its level
        rig.param (kDepth, toNormalized (kDepth, 0.0));
        rig.param (kMonoSub, toNormalized (kMonoSub, 0.0));
        std::vector<float> out;
        rig.render (2.0, out, nullptr, bass ());
        const size_t a = 48000, b = 2 * 48000;
        const double sub0 = toneDb (out, 45.0, a, b), mid0 = toneDb (out, 250.0, a, b), top0 = toneDb (out, 3000.0, a, b);
        CHECK (std::fabs (sub0 + 6.0) < 0.2 && std::fabs (mid0 + 18.0) < 0.2 && std::fabs (top0 + 26.0) < 0.2,
               "flat at Depth 0: %.2f / %.2f / %.2f dB", sub0, mid0, top0);
        CHECK (allFinite (out), "finite");

        // Depth 6 dB with the sub far over the threshold: the low mids dipped by the full Depth, the sub kept
        rig.param (kDepth, toNormalized (kDepth, 6.0));
        rig.param (kMonoSub, toNormalized (kMonoSub, 1.0));
        out.clear ();
        rig.render (2.0, out, nullptr, bass ());
        const double sub1 = toneDb (out, 45.0, a, b), mid1 = toneDb (out, 250.0, a, b), top1 = toneDb (out, 3000.0, a, b);
        CHECK (std::fabs (mid1 - (mid0 - 6.0)) < 0.5, "250 Hz dipped by the Depth: %.2f -> %.2f dB", mid0, mid1);
        CHECK (std::fabs (sub1 - sub0) < 0.2, "the sub untouched: %.2f -> %.2f dB", sub0, sub1);
        CHECK (top0 - top1 < 1.0, "3 kHz left alone: %.2f -> %.2f dB", top0, top1);

        // without the sub: no dip
        out.clear ();
        rig.render (2.0, out, nullptr, tones ({{250.0, -18.0}}));
        const double alone = toneDb (out, 250.0, a, b);
        CHECK (std::fabs (alone - mid0) < 0.1, "no sub, no dip: %.2f dB", alone);

        // state round trip
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::fabs (toPlain (kDepth, back.norm[kDepth]) - 6.0) < 1e-6, "depth saved");

        // editor: screenshot while audio is flowing (the meters moving)
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            ViewRect r;
            CHECK (win.view () && win.view ()->getSize (&r) == kResultOk && r.getWidth () == (int32)Editor::kWidth &&
                       r.getHeight () == (int32)(Editor::kHeight + pk::EditorBase::kInfoHeight),
                   "editor size %d x %d", r.getWidth (), r.getHeight ());
            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, bass ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_deepr.png"), "screenshot");
        }
        rig.stop ();
        return finish ("deepr host test");
    }
}
