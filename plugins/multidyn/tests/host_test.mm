// End-to-end test of the built Multidyn.vst3: audio (with the side-chain bus), state, the editor
// and its mouse gestures. usage: multidyn_hosttest <Multidyn.vst3> <output dir>
#include "Params.h"
#include "plugin/State.h"
#include "ui/DynDisplay.h"
#include "ui/Editor.h"
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

enum { kLow = 0, kMid = 1, kHigh = 2 }; // with three bands

static void set (State& st, uint32_t id, double plain);

// The defaults are Live's OTT preset; audio checks start from a neutral device.
static State baseState ()
{
    State st;
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        st.norm[id] = defaultNormalized (id);
        st.has[id] = true;
    }
    for (int b = 0; b < kMaxBands; ++b)
    {
        set (st, bandParam (b, kBandInput), 0.0);
        set (st, bandParam (b, kBandOutput), 0.0);
        set (st, bandParam (b, kAboveRatio), 1.0);
        set (st, bandParam (b, kBelowRatio), 1.0);
        set (st, bandParam (b, kAboveThresh), -12.0);
        set (st, bandParam (b, kBelowThresh), -40.0);
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
        // a fresh instance is the four-band upward-compression preset
        CHECK (std::lround (plainOf (rig, kBands)) == 2 && std::fabs (plainOf (rig, kXover1) - 88.3) < 1e-6 &&
                   std::fabs (plainOf (rig, bandParam (1, kAboveRatio)) - 66.7) < 1e-6 &&
                   std::fabs (plainOf (rig, bandParam (0, kBandOutput)) - 24.0) < 1e-6 &&
                   plainOf (rig, bandParam (2, kBelowRatio)) >= kRatioInf * 0.999 && std::fabs (plainOf (rig, multidyn::kOutput) + 7.0) < 1e-6,
               "preset defaults");

        // --- downward compression through the plug-in (single band, peak, hard knee) ---
        State st = baseState ();
        set (st, kBands, 0); // single band: band 0
        set (st, kSoftKnee, 0);
        set (st, kDetector, kPeak);
        set (st, kMode, kBase);
        set (st, bandParam (0, kAboveThresh), -20.0);
        set (st, bandParam (0, kAboveRatio), 4.0);
        set (st, bandParam (0, kRelease), 500.0);
        CHECK (apply (rig, st), "setState");
        CHECK (rig.start (), "start");
        CHECK (rig.processor->getLatencySamples () == 48, "look-ahead latency %u", rig.processor->getLatencySamples ());
        std::vector<float> out;
        rig.render (2.0, out, nullptr, tones (-6.0, -100.0));
        CHECK (allFinite (out), "finite");
        const double comp = peakDb (out, 72000, 96000);
        CHECK (std::fabs (comp - (-20.0 + 14.0 / 4.0)) < 0.6, "compressed peak %.2f dB (want -16.5)", comp);

        // --- side-chain: quiet main keyed by a loud side-chain ---
        rig.param (bandParam (0, kAboveRatio), toNormalized (bandParam (0, kAboveRatio), 10.0));
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

        // --- CPU with all four bands ---
        rig.param (kBands, toNormalized (kBands, 3)); // four bands
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
        CHECK (std::fabs (back.norm[bandParam (0, kAboveRatio)] - toNormalized (bandParam (0, kAboveRatio), 10.0)) < 1e-9,
               "automated ratio saved");
        CHECK (std::fabs (back.norm[kBands] - toNormalized (kBands, 3)) < 1e-9, "band count saved");
        rig.stop ();

        // --- editor with the untouched OTT defaults while audio plays (docs screenshot) ---
        {
            State ott;
            for (uint32_t id = 0; id < kNumParams; ++id)
            {
                ott.norm[id] = defaultNormalized (id);
                ott.has[id] = true;
            }
            apply (rig, ott);
            rig.start ();
            EditorWindow win (rig.controller);
            for (int i = 0; i < 12; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, [] (int bus, int, float* buf, int n, long long pos) {
                    if (bus != 0)
                        return;
                    uint32_t seed = (uint32_t)pos * 2654435761u + 7;
                    for (int k = 0; k < n; ++k)
                    {
                        seed = seed * 1664525u + 1013904223u;
                        const double t = (double)(pos + k) / 48000.0;
                        const double env = std::fmod (t, 0.5) < 0.1 ? 1.0 : 0.15; // loud hits, quiet tails
                        buf[k] = (float)(env * (0.15 * (((seed >> 8) & 0xFFFF) / 32768.0 - 1.0) + 0.3 * std::sin (2.0 * M_PI * 60.0 * t)));
                    }
                });
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_multidyn_preset.png"), "preset screenshot");
            CHECK (allFinite (out), "preset output finite");
        }
        rig.stop ();

        // --- editor: screenshot and the display's gestures ---
        State ui = baseState ();
        set (ui, kBands, 2); // three bands: band 2 on top
        set (ui, kMode, kBase);
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

            // display graph: x 242..604 = -80..0 dB, lanes from y 54 to 320; three lanes, band 3 on top
            const double gl = Editor::kDisplayLeft + DynDisplay::kLeftCol, gw = Editor::kDisplayRight - DynDisplay::kRightCol - gl;
            auto xOf = [=] (double db) { return gl + (db + 80.0) / 80.0 * gw; };
            const double pxPerDb = gw / 80.0;
            const double lanesTop = Editor::kDisplayTop + DynDisplay::kHeader;
            const double laneH = (Editor::kDisplayBottom - lanesTop - DynDisplay::kScaleHeight) / 3.0;
            const double midY = lanesTop + laneH * 1.5;
            const double highY = lanesTop + laneH * 0.5;
            const double inAbove = gl + gw - 12.0; // inside the above block
            const uint32_t midAbove = bandParam (kMid, kAboveThresh);

            // 1. drag the mid band's above threshold 10 dB lower
            const double a0 = plainOf (rig, midAbove);
            const double x0 = xOf (a0);
            win.drag (x0, midY, x0 - 10.0 * pxPerDb, midY);
            CHECK (std::fabs (plainOf (rig, midAbove) - (a0 - 10.0)) < 0.3, "threshold drag -> %.2f (want %.2f)", plainOf (rig, midAbove), a0 - 10.0);

            // 2. Shift = fine: the same drag moves it only a fifth as far
            const double before = plainOf (rig, midAbove);
            const double x1 = xOf (before);
            win.drag (x1, midY, x1 + 10.0 * pxPerDb, midY, kShift);
            CHECK (std::fabs (plainOf (rig, midAbove) - before - 2.0) < 0.3, "fine drag moved %.2f dB (want 2)",
                   plainOf (rig, midAbove) - before);

            // 3. Cmd: move every band's below threshold by the same amount
            double below0[3];
            for (int b = 0; b < 3; ++b)
                below0[b] = plainOf (rig, bandParam (b, kBelowThresh));
            const double xb = xOf (below0[kMid]);
            win.drag (xb, midY, xb + 6.0 * pxPerDb, midY, kCmd);
            for (int b = 0; b < 3; ++b)
                CHECK (std::fabs (plainOf (rig, bandParam (b, kBelowThresh)) - (below0[b] + 6.0)) < 0.4, "cmd drag band %d -> %.2f", b,
                       plainOf (rig, bandParam (b, kBelowThresh)));

            // 4. drag down inside the mid above block: quieter = higher ratio
            const uint32_t midRatio = bandParam (kMid, kAboveRatio);
            const double r0 = plainOf (rig, midRatio);
            win.drag (inAbove, midY, inAbove, midY + 40);
            CHECK (plainOf (rig, midRatio) > r0 * 1.3, "ratio drag down: %.2f -> %.2f", r0, plainOf (rig, midRatio));
            win.drag (inAbove, midY, inAbove, midY - 160);
            CHECK (plainOf (rig, midRatio) < 1.0, "drag up past 1:1 gives upward expansion: %.2f", plainOf (rig, midRatio));

            // 5. Alt: both blocks of the high band get quieter together (drag down): the Above ratio
            // rises (more compression), the Below ratio falls (towards downward expansion)
            const double ha = plainOf (rig, bandParam (kHigh, kAboveRatio)), hb = plainOf (rig, bandParam (kHigh, kBelowRatio));
            win.drag (inAbove, highY, inAbove, highY + 30, kAlt);
            CHECK (plainOf (rig, bandParam (kHigh, kAboveRatio)) > ha && plainOf (rig, bandParam (kHigh, kBelowRatio)) < hb,
                   "alt drag: above %.2f->%.2f below %.2f->%.2f", ha, plainOf (rig, bandParam (kHigh, kAboveRatio)), hb,
                   plainOf (rig, bandParam (kHigh, kBelowRatio)));

            // 6. double-click a block resets its ratio to 1:1
            win.click (inAbove, midY, 2);
            CHECK (std::fabs (plainOf (rig, midRatio) - 1.0) < 1e-6, "double-click reset -> %.3f", plainOf (rig, midRatio));

            // 7. band count through the UI (segments "1".."4" at x 210..330)
            win.click (210 + 30 * 3.5, 17);
            CHECK (std::lround (plainOf (rig, kBands)) == 3, "4 bands selected (%f)", plainOf (rig, kBands));
            CHECK (win.savePng (outDir + "/ui_multidyn_4bands.png"), "screenshot 4 bands");
            win.click (210 + 30 * 0.5, 17);
            CHECK (std::lround (plainOf (rig, kBands)) == 0, "1 band selected");
            CHECK (win.savePng (outDir + "/ui_multidyn_1band.png"), "screenshot 1 band");
            win.click (210 + 30 * 2.5, 17);

            // 8. the value fields: dragging the mid band's attack field up raises it
            const uint32_t midAttack = bandParam (kMid, kAttack);
            const double at0 = plainOf (rig, midAttack);
            const double fx = Editor::kDisplayRight - DynDisplay::kRightCol + 84 + 34, fy = midY - 11;
            win.drag (fx, fy, fx, fy - 40);
            CHECK (plainOf (rig, midAttack) > at0 * 1.2, "attack field drag: %.1f -> %.1f ms", at0, plainOf (rig, midAttack));
            win.click (fx, fy, 2);
            CHECK (std::fabs (plainOf (rig, midAttack) - toPlain (midAttack, defaultNormalized (midAttack))) < 1e-6,
                   "double-click resets the field");
        }
        rig.stop ();
        return finish ("multidyn host test");
    }
}
