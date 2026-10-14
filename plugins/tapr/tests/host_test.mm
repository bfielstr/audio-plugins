// End-to-end test of the built Tapr.vst3. usage: tapr_hosttest <Tapr.vst3> <output dir>
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
using namespace tapr;
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

// where the display (Classic) puts a Cut bell's handle: TapeView's scales (the response at the left of the
// curve's box, which is as wide as the display is tall less 16)
static double displayX (double hz)
{
    const double h = Editor::kDisplayBottom - Editor::kTop, side = h - 16.0;
    const double left = 8.0, right = (Editor::kWidth - 8.0) - side - 8.0 - 10.0;
    return left + std::log (hz / 20.0) / std::log (1000.0) * (right - left);
}
static double displayY (double db)
{
    const double top = Editor::kTop, h = Editor::kDisplayBottom - Editor::kTop - 14.0;
    return top + (21.0 - db) / 42.0 * h;
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
            return finish ("tapr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (rig.component->getBusCount (kEvent, kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets
        // a new instance: the end saturator off (the tape stage is the saturator), its Gentlr ready
        checkNewInstanceGentlr (rig.controller, -1, (uint32_t)kTailExtBase + pk::kTailExtClarity, (uint32_t)kTailExt3Base + pk::kTailExt3Slope);
        CHECK (rig.controller->getParamNormalized ((uint32_t)kTailBase + pk::kTailOn) == 0.0, "a new instance: the end saturator off");
        CHECK (std::fabs (plainOf (rig, kDrive) - 0.75) < 1e-9 && std::fabs (plainOf (rig, tapr::kOutput) + 4.4) < 1e-9 &&
                   std::lround (plainOf (rig, kCutPlace)) == kPost,
               "a new instance: the defaults (Drive 75 %%, Output -4.4 dB, Cut Post)");

        // Mix 0 from the start: the input comes out bit for bit, delayed by the latency
        State st = baseState ();
        st.norm[tapr::kMix] = 0.0;
        st.norm[tapr::kOutput] = toNormalized (tapr::kOutput, 0.0);
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        const uint32 latency = rig.processor->getLatencySamples ();
        CHECK (latency > 0 && latency < 300, "latency reported %u", latency);
        std::vector<float> out, outR;
        rig.render (1.0, out, &outR, bass ());
        size_t differ = 0;
        for (size_t i = latency; i < out.size (); ++i)
            differ += out[i] != bassAt ((long long)(i - latency)) || outR[i] != out[i];
        CHECK (differ == 0, "Mix 0: the input bit for bit (%zu samples differ)", differ);
        CHECK (allFinite (out), "finite");

        // the defaults on the bass: finite, and the tape holds it at about -13 dBFS RMS
        rig.param (tapr::kMix, 1.0);
        rig.param (tapr::kOutput, defaultNormalized (tapr::kOutput));
        out.clear ();
        outR.clear ();
        rig.render (2.0, out, &outR, bass ());
        CHECK (allFinite (out) && allFinite (outR), "finite");
        const double level = dbfs (rms (out, 48000, out.size ()));
        CHECK (level > -18.0 && level < -8.0, "the defaults: %.1f dBFS RMS", level);

        // state round trip
        rig.param (kCompPlace, toNormalized (kCompPlace, kPost));
        rig.param (kCutLo, toNormalized (kCutLo, 6.0));
        rig.render (0.05, out, nullptr, bass ());
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::lround (toPlain (kCompPlace, back.norm[kCompPlace])) == kPost, "Compression Place saved");
        CHECK (std::fabs (toPlain (kCutLo, back.norm[kCutLo]) - 6.0) < 1e-6, "Cut Lo saved");
        rig.param (kCompPlace, toNormalized (kCompPlace, kPre));
        rig.param (kCutLo, toNormalized (kCutLo, 0.0));

        // editor (Classic): Cut clicked off and on, the LO handle dragged up, a double-click resets it, then a screenshot
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            ViewRect r;
            CHECK (win.view () && win.view ()->getSize (&r) == kResultOk && r.getWidth () == (int32)Editor::kWidth &&
                       r.getHeight () == (int32)(Editor::kHeight + pk::EditorBase::kInfoHeight),
                   "editor size %d x %d", r.getWidth (), r.getHeight ());
            pump (0.1);
            const double sx = Editor::kCutLeft + 12.0 + 45.0, sy = Editor::kRowTop + Editor::kSwitchTop + Editor::kSwitchH / 2;
            win.click (sx, sy);
            pump (0.05);
            CHECK (plainOf (rig, kCutOn) < 0.5, "Cut clicked off");
            win.click (sx, sy);
            pump (0.05);
            CHECK (plainOf (rig, kCutOn) >= 0.5, "Cut clicked on");

            // the LO handle: 30 px up (+ about 6.8 dB)
            const double hx = displayX (126.32), hy = displayY (0.0);
            win.drag (hx, hy, hx, hy - 30.0);
            pump (0.05);
            CHECK (plainOf (rig, kCutLo) > 4.0 && plainOf (rig, kCutLo) < 9.0, "LO dragged up to %.1f dB", plainOf (rig, kCutLo));
            // a double-click on it puts it back at 0 dB
            win.click (hx, displayY (plainOf (rig, kCutLo)), 2);
            pump (0.05);
            CHECK (std::fabs (plainOf (rig, kCutLo)) < 1e-6, "LO back at 0 dB: %.2f dB", plainOf (rig, kCutLo));

            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, bass ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_tapr.png"), "screenshot");
            // Classic, Wide and Classic again: knobs found and turned in each (Wide's screenshot)
            checkLayouts (rig, win, {(uint32_t)kDrive, (uint32_t)kCutLo, (uint32_t)tapr::kMix}, outDir + "/ui_tapr_wide.png");
        }
        rig.stop ();
        return finish ("tapr host test");
    }
}
