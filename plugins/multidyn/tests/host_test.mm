// End-to-end test of the built Multidyn.vst3: audio (with the side-chain bus), state, the editor
// and its mouse gestures. usage: multidyn_hosttest <Multidyn.vst3> <output dir>
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"

#include "public.sdk/source/common/memorystream.h"

#include <chrono>
#include <cmath>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace multidyn;
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

static void set (State& st, uint32_t id, double plain) { st.norm[id] = toNormalized (id, plain); }

static bool apply (Rig& rig, const State& st)
{
    return rig.applyState ([&] (IBStream* s) { return writeState (s, st); });
}

static InputFn tones (double mainDb, double scDb)
{
    return [mainDb, scDb] (int bus, int, float* buf, int n, long long pos) {
        const double a = std::pow (10.0, (bus == 0 ? mainDb : scDb) / 20.0);
        if (a <= 0.0)
            return;
        for (int i = 0; i < n; ++i)
            buf[i] = (float)(a * std::sin (2.0 * M_PI * 1000.0 * (double)(pos + i) / 48000.0));
    };
}

static double peakDb (const std::vector<float>& x, size_t a, size_t b)
{
    double p = 0;
    for (size_t i = a; i < std::min (b, x.size ()); ++i)
        p = std::max (p, (double)std::fabs (x[i]));
    return dbfs (p);
}

static double plainOf (Rig& rig, uint32_t id) { return toPlain (id, rig.controller->getParamNormalized (id)); }

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
            return finish ("multidyn host test");

        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count %d", rig.controller->getParameterCount ());
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");
        CHECK (rig.component->getBusCount (kAudio, kInput) == 2, "main + side-chain inputs");

        // --- downward compression through the plug-in (single band, peak, hard knee) ---
        State st = baseState ();
        set (st, kLowOn, 0);
        set (st, kHighOn, 0);
        set (st, kSoftKnee, 0);
        set (st, kDetector, kPeak);
        set (st, bandParam (kMid, kAboveThresh), -20.0);
        set (st, bandParam (kMid, kAboveRatio), 4.0);
        set (st, bandParam (kMid, kRelease), 500.0);
        CHECK (apply (rig, st), "setState");
        CHECK (rig.start (), "start");
        std::vector<float> out;
        rig.render (2.0, out, nullptr, tones (-6.0, -100.0));
        CHECK (allFinite (out), "finite");
        const double comp = peakDb (out, 72000, 96000);
        CHECK (std::fabs (comp - (-20.0 + 14.0 / 4.0)) < 0.6, "compressed peak %.2f dB (want -16.5)", comp);

        // --- side-chain: quiet main keyed by a loud side-chain ---
        rig.param (bandParam (kMid, kAboveRatio), toNormalized (bandParam (kMid, kAboveRatio), 10.0));
        rig.param (kScOn, 1.0);
        out.clear ();
        rig.render (2.0, out, nullptr, tones (-30.0, -6.0));
        const double keyed = peakDb (out, 72000, 96000);
        CHECK (std::fabs (keyed - (-30.0 - 12.6)) < 1.0, "keyed by side-chain: %.2f dB (want -42.6)", keyed);
        rig.param (kScListen, 1.0);
        out.clear ();
        rig.render (0.5, out, nullptr, tones (-30.0, -6.0));
        CHECK (std::fabs (peakDb (out, 12000, 24000) + 6.0) < 0.3, "listen = side-chain: %.2f", peakDb (out, 12000, 24000));
        rig.param (kScListen, 0.0);
        rig.param (kScOn, 0.0);

        // --- CPU with all three bands ---
        rig.param (kLowOn, 1.0);
        rig.param (kHighOn, 1.0);
        const auto t0 = std::chrono::steady_clock::now ();
        out.clear ();
        rig.render (5.0, out, nullptr, tones (-12.0, -12.0));
        const double cpu = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count () / 5.0 * 100.0;
        std::printf ("  CPU through the plug-in: %.2f%% of one core\n", cpu);
        CHECK (cpu < 10.0, "too slow %f", cpu);

        // --- state round trip ---
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::fabs (back.norm[bandParam (kMid, kAboveRatio)] - toNormalized (bandParam (kMid, kAboveRatio), 10.0)) < 1e-9,
               "automated ratio saved");
        CHECK (back.norm[kLowOn] == 1.0, "band switch saved");
        rig.stop ();

        // --- editor: screenshot and the display's gestures ---
        State ui = baseState ();
        for (int b = 0; b < kNumBands; ++b)
        {
            set (ui, bandParam (b, kAboveRatio), b == kHigh ? 3.0 : 2.0);
            set (ui, bandParam (b, kBelowRatio), b == kLow ? 0.6 : 1.5);
        }
        apply (rig, ui);
        rig.start ();
        out.clear ();
        rig.render (0.5, out, nullptr, [] (int bus, int, float* buf, int n, long long pos) {
            if (bus != 0)
                return;
            uint32_t seed = (uint32_t)pos * 2654435761u + 7;
            for (int i = 0; i < n; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                buf[i] = 0.3f * (float)(((seed >> 8) & 0xFFFF) / 32768.0 - 1.0) +
                         0.4f * (float)std::sin (2.0 * M_PI * 80.0 * (double)(pos + i) / 48000.0);
            }
        });
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor attach");
            pump (0.5); // let the meters settle
            CHECK (win.savePng (outDir + "/ui_multidyn.png"), "screenshot");

            // display geometry: x = 168 + (dB + 70) / 76 * 472, lanes High/Mid/Low from y = 42
            auto xOf = [] (double db) { return 168.0 + (db + 70.0) / 76.0 * 472.0; };
            const double midY = 42.0 + (306.0 - 16.0) / 3.0 * 1.5;
            const double highY = 42.0 + (306.0 - 16.0) / 3.0 * 0.5;
            const uint32_t midAbove = bandParam (kMid, kAboveThresh);

            // 1. drag the mid band's above threshold 10 dB lower
            const double x0 = xOf (plainOf (rig, midAbove));
            win.drag (x0, midY, x0 - 10.0 / 76.0 * 472.0, midY);
            CHECK (std::fabs (plainOf (rig, midAbove) - (-22.0)) < 0.3, "threshold drag -> %.2f (want -22)", plainOf (rig, midAbove));

            // 2. Shift = fine: the same drag moves it only a fifth as far
            const double before = plainOf (rig, midAbove);
            const double x1 = xOf (before);
            win.drag (x1, midY, x1 + 10.0 / 76.0 * 472.0, midY, kShift);
            CHECK (std::fabs (plainOf (rig, midAbove) - before - 2.0) < 0.3, "fine drag moved %.2f dB (want 2)",
                   plainOf (rig, midAbove) - before);

            // 3. Cmd: move every band's below threshold together
            const double lowBelow0 = plainOf (rig, bandParam (kLow, kBelowThresh));
            const double xb = xOf (plainOf (rig, bandParam (kMid, kBelowThresh)));
            win.drag (xb, midY, xb + 6.0 / 76.0 * 472.0, midY, kCmd);
            for (int b = 0; b < kNumBands; ++b)
                CHECK (std::fabs (plainOf (rig, bandParam (b, kBelowThresh)) - (-34.0)) < 0.4, "cmd drag band %d -> %.2f", b,
                       plainOf (rig, bandParam (b, kBelowThresh)));
            (void)lowBelow0;

            // 4. drag down inside the mid above block: quieter = higher ratio
            const uint32_t midRatio = bandParam (kMid, kAboveRatio);
            const double r0 = plainOf (rig, midRatio);
            win.drag (620, midY, 620, midY + 40);
            CHECK (plainOf (rig, midRatio) > r0 * 1.3, "ratio drag down: %.2f -> %.2f", r0, plainOf (rig, midRatio));
            win.drag (620, midY, 620, midY - 160);
            CHECK (plainOf (rig, midRatio) < 1.0, "drag up past 1:1 gives upward expansion: %.2f", plainOf (rig, midRatio));

            // 5. Alt: above and below ratios of the high band move together
            const double ha = plainOf (rig, bandParam (kHigh, kAboveRatio)), hb = plainOf (rig, bandParam (kHigh, kBelowRatio));
            win.drag (620, highY, 620, highY + 30, kAlt);
            CHECK (plainOf (rig, bandParam (kHigh, kAboveRatio)) > ha && plainOf (rig, bandParam (kHigh, kBelowRatio)) > hb,
                   "alt drag: above %.2f->%.2f below %.2f->%.2f", ha, plainOf (rig, bandParam (kHigh, kAboveRatio)), hb,
                   plainOf (rig, bandParam (kHigh, kBelowRatio)));

            // 6. double-click a block resets its ratio to 1:1
            win.click (620, midY, 2);
            CHECK (std::fabs (plainOf (rig, midRatio) - 1.0) < 1e-6, "double-click reset -> %.3f", plainOf (rig, midRatio));

            // 7. band switch through the UI
            win.click (50, 42 + 14); // "High" toggle
            CHECK (plainOf (rig, kHighOn) < 0.5, "high band off");
            win.click (50, 42 + 14);

            // 8. the A tab shows the above column (screenshot for eyeballing)
            win.click (716 + 2 * 58 + 20, 17);
            CHECK (win.savePng (outDir + "/ui_multidyn_above.png"), "screenshot A");
        }
        rig.stop ();
        return finish ("multidyn host test");
    }
}
