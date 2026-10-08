// End-to-end test of the built Ciphr.vst3. usage: ciphr_hosttest <Ciphr.vst3> <output dir>
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
using namespace ciphr;
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
    // about ciphr's own sound (a new instance has it on: checkNewInstanceGentlr)
    st.norm[kTailBase + pk::kTailOn] = 0.0;
    st.norm[kTailExtBase + pk::kTailExtClarity] = 0.0;
    st.norm[kTailExt3Base + pk::kTailExt3Slope] = 0.0;
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

// a 1 kHz sine at -12 dBFS on the side-chain input, the same on both channels
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
            return finish ("ciphr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (rig.component->getBusCount (kEvent, Steinberg::Vst::kInput) == 1, "an event input");
        CHECK (rig.component->getBusCount (kAudio, Steinberg::Vst::kInput) == 1, "the side-chain input");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets
        checkNewInstanceGentlr (rig.controller, kTailBase + pk::kTailOn, kTailExtBase + pk::kTailExtClarity, kTailExt3Base + pk::kTailExt3Slope);

        State st = baseState ();
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        const uint32 latency = rig.processor->getLatencySamples ();
        CHECK (latency < 200, "latency reported %u (the end saturator's)", latency);

        // silence until a note plays
        std::vector<float> out, outR;
        rig.render (0.5, out, &outR);
        CHECK (rms (out, 0, out.size ()) == 0.0, "silent with no notes");

        // a chord: audible, finite, stereo (the processor's taps are panned)
        out.clear ();
        outR.clear ();
        for (int p : {48, 55, 60, 64})
            rig.note (p, 0.8f);
        rig.render (2.0, out, &outR);
        const double level = dbfs (rms (out, 24000, out.size ()));
        double diff = 0.0;
        for (size_t i = 0; i < out.size (); ++i)
            diff = std::max (diff, (double)std::fabs (out[i] - outR[i]));
        CHECK (level > -36.0 && level < -3.0, "a chord at %.1f dBFS", level);
        CHECK (diff > 0.005, "stereo (%.3f)", diff);
        CHECK (allFinite (out) && allFinite (outR), "finite");
        for (int p : {48, 55, 60, 64})
            rig.note (p, 0.0f);
        // Release 500 ms and the processor's echoes (Regen 35 %): quiet after a few seconds
        out.clear ();
        rig.render (8.0, out);
        CHECK (dbfs (rms (out, out.size () - 48000, out.size ())) < -70.0, "the notes and their echoes fade (%.1f dBFS)",
               dbfs (rms (out, out.size () - 48000, out.size ())));

        // the side-chain input: Input 100 %, Blend 0: the tone passes at its level
        rig.param (ciphr::kInput, 1.0);
        rig.param (kBlend, 0.0);
        out.clear ();
        rig.render (1.5, out, nullptr, tone ());
        const double through = toneDb (out, 1000.0, 24000, out.size ());
        CHECK (std::fabs (through + 12.04) < 0.2, "the input passes: 1 kHz at %.2f dB", through);
        rig.param (ciphr::kInput, 0.0);
        rig.param (kBlend, defaultNormalized (kBlend));

        // state round trip
        rig.param (kVariant, toNormalized (kVariant, 37.0));
        rig.render (0.05, out); // (the processor takes the change with its next block)
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::lround (toPlain (kVariant, back.norm[kVariant])) == 37, "Variant saved");

        // editor: the Input Path switch, then a screenshot while a chord plays
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            ViewRect r;
            CHECK (win.view () && win.view ()->getSize (&r) == kResultOk && r.getWidth () == (int32)Editor::kWidth &&
                       r.getHeight () == (int32)(Editor::kHeight + pk::EditorBase::kInfoHeight),
                   "editor size %d x %d", r.getWidth (), r.getHeight ());
            pump (0.1);
            const double py = Editor::kRow1 + Editor::kPathTop + Editor::kPathH / 2;
            win.click (Editor::kInputLeft + Editor::kPathLeft + Editor::kPathW * 3 / 4, py);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kInputPath)) == kPathVoices, "Voices clicked: %.0f", plainOf (rig, kInputPath));
            win.click (Editor::kInputLeft + Editor::kPathLeft + Editor::kPathW / 4, py);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kInputPath)) == kPathDirect, "Direct clicked: %.0f", plainOf (rig, kInputPath));
            // Cross's knob (the second in GENERATOR): a double-click puts it back to the centre
            rig.param (kCross, 0.8);
            pump (0.05);
            const double cx = Editor::kGenLeft + Editor::kKnobLeft + Editor::kKnobStep + Editor::kKnobW / 2;
            const double cy = Editor::kRow1 + Editor::kKnobTop + Editor::kKnobH / 2;
            win.click (cx, cy, 2);
            pump (0.05);
            CHECK (std::fabs (plainOf (rig, kCross)) < 1e-6, "Cross back to the centre: %.2f", plainOf (rig, kCross));
            // Disperse's On switch (the third row): a click switches it on, another off
            const double ox = Editor::kDispLeft + Editor::kDispOnLeft + Editor::kDispOnW / 2;
            const double oy = Editor::kRow3 + Editor::kDispOnTop + Editor::kDispOnH / 2;
            win.click (ox, oy);
            pump (0.05);
            CHECK (plainOf (rig, kDisperseOn) >= 0.5, "Disperse switched on: %.0f", plainOf (rig, kDisperseOn));
            win.click (ox, oy);
            pump (0.05);
            CHECK (plainOf (rig, kDisperseOn) < 0.5, "Disperse switched off: %.0f", plainOf (rig, kDisperseOn));

            for (int p : {45, 52, 57, 64})
                rig.note (p, 0.8f);
            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out);
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_ciphr.png"), "screenshot");
            // Classic, Wide and Classic again: knobs found and turned in each (Wide's screenshot)
            checkLayouts (rig, win, {(uint32_t)kTimbre, (uint32_t)kCutoff, (uint32_t)kSpace, (uint32_t)kBlend, (uint32_t)kDisperse}, outDir + "/ui_ciphr_wide.png");
            for (int p : {45, 52, 57, 64})
                rig.note (p, 0.0f);
        }
        rig.stop ();
        return finish ("ciphr host test");
    }
}
