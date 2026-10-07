// End-to-end test of the built Smeezr.vst3. usage: smeezr_hosttest <Smeezr.vst3> <output dir>
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
using namespace smeezr;
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

// a 1 kHz sine at -12 dBFS, the same on both channels
static float toneAt (long long pos) { return (float)(0.25 * std::sin (2 * M_PI * 1000.0 * (double)pos / 48000.0)); }
static InputFn tone ()
{
    return [] (int, int, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
            buf[i] = toneAt (pos + i);
    };
}

// a bright, dense test signal: a detuned saw pair at 110 Hz with noise, the same on both channels
// (gain 1: hot, about -14 dBFS RMS; 0.5: a typical level, about -20 dBFS)
static InputFn music (double gain = 1.0)
{
    return [gain] (int, int, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
        {
            const long long k = pos + i;
            const double t = (double)k / 48000.0;
            const double a = 110.0 * t, b = 110.0 * 1.01 * t + 0.3;
            uint32_t s = (uint32_t)k * 2654435761u;
            s ^= s >> 13;
            buf[i] = (float)(gain * (0.15 * ((2.0 * (a - std::floor (a)) - 1.0) + (2.0 * (b - std::floor (b)) - 1.0)) +
                                     0.02 * ((double)(s & 0xffff) / 32768.0 - 1.0)));
        }
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
            return finish ("smeezr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (rig.component->getBusCount (kEvent, kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets

        // Squeeze 0 from the start: the tone comes out bit for bit, delayed by the latency
        State st = baseState ();
        st.norm[kSqueeze] = 0.0;
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        const uint32 latency = rig.processor->getLatencySamples ();
        CHECK (latency < 200, "latency reported %u (the end saturator's)", latency);
        std::vector<float> out, outR;
        rig.render (1.0, out, &outR, tone ());
        size_t differ = 0;
        for (size_t i = latency; i < out.size (); ++i)
            differ += out[i] != toneAt ((long long)(i - latency)) || outR[i] != out[i];
        CHECK (differ == 0, "Squeeze 0: the input bit for bit (%zu samples differ)", differ);
        CHECK (allFinite (out), "finite");

        // 50 % and 100 % on a bright signal at a typical level: about as loud at 50 %, louder at 100 % (OTT's
        // makeup lifts it). On a hot signal OTT holds the level instead (it pulls everything towards the same
        // loudness), so there 100 % only has to stay within a few dB of 50 %.
        auto levelAt = [&] (double squeeze, double gain) {
            rig.param (kSqueeze, squeeze);
            out.clear ();
            outR.clear ();
            rig.render (3.0, out, &outR, music (gain));
            CHECK (allFinite (out) && allFinite (outR), "finite (Squeeze %.0f %%, gain %.2f)", 100.0 * squeeze, gain);
            return dbfs (rms (out, 96000, out.size ()));
        };
        const double half = levelAt (0.5, 0.5), full = levelAt (1.0, 0.5);
        CHECK (half > -36.0 && half < -12.0, "50 %%: %.1f dBFS", half);
        CHECK (full > half + 2.0, "100 %% louder than 50 %%: %.1f against %.1f dBFS", full, half);
        const double hotHalf = levelAt (0.5, 1.0), hotFull = levelAt (1.0, 1.0);
        CHECK (std::fabs (hotFull - hotHalf) < 3.0, "hot input: 100 %% %.1f against 50 %% %.1f dBFS", hotFull, hotHalf);

        // state round trip
        rig.param (kSqueeze, 0.85);
        rig.param (kSpeed, toNormalized (kSpeed, kSpeedSlow));
        rig.render (0.05, out, nullptr, music ());
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::fabs (back.norm[kSqueeze] - 0.85) < 1e-9, "Squeeze saved");
        CHECK (std::lround (toPlain (kSpeed, back.norm[kSpeed])) == kSpeedSlow, "Speed saved");

        // editor (Classic): the Speed switch, the big knob dragged and double-clicked, then a screenshot
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            ViewRect r;
            CHECK (win.view () && win.view ()->getSize (&r) == kResultOk && r.getWidth () == (int32)Editor::kWidth &&
                       r.getHeight () == (int32)(Editor::kHeight + pk::EditorBase::kInfoHeight),
                   "editor size %d x %d", r.getWidth (), r.getHeight ());
            pump (0.1);
            const double sy = Editor::kRowTop + Editor::kSpeedTop + Editor::kSpeedH / 2;
            win.click (Editor::kMixLeft + Editor::kSpeedLeft + Editor::kSpeedW / 4, sy);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kSpeed)) == kSpeedFast, "Fast clicked: %.0f", plainOf (rig, kSpeed));
            win.click (Editor::kMixLeft + Editor::kSpeedLeft + Editor::kSpeedW * 3 / 4, sy);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kSpeed)) == kSpeedSlow, "Slow clicked: %.0f", plainOf (rig, kSpeed));

            // the big knob: from 0, dragged up 100 px (200 px for the whole range) to about 50 %
            rig.param (kSqueeze, 0.0);
            pump (0.05);
            const double kx = Editor::kSqueezeLeft + Editor::kBigLeft + Editor::kBigW / 2;
            const double ky = Editor::kRowTop + Editor::kBigTop + Editor::kBigH / 2;
            win.drag (kx, ky, kx, ky - 100.0);
            pump (0.05);
            CHECK (std::fabs (plainOf (rig, kSqueeze) - 0.5) < 0.03, "Squeeze dragged to %.2f", plainOf (rig, kSqueeze));
            // a double-click puts it back to its default (40 %)
            win.click (kx, ky, 2);
            pump (0.05);
            CHECK (std::fabs (plainOf (rig, kSqueeze) - 0.4) < 1e-6, "Squeeze back to 40 %%: %.2f", plainOf (rig, kSqueeze));
            rig.param (kSqueeze, 0.75);

            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, music ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_smeezr.png"), "screenshot");
            // Classic, Wide and Classic again: knobs found and turned in each (Wide's screenshot)
            checkLayouts (rig, win, {(uint32_t)kSqueeze, (uint32_t)kMix, (uint32_t)smeezr::kOutput}, outDir + "/ui_smeezr_wide.png");
        }
        rig.stop ();
        return finish ("smeezr host test");
    }
}
