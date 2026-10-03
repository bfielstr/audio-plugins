// End-to-end test of the built Multidyn.vst3: audio (with the side-chain bus), state, the editor
// and its mouse gestures. usage: multidyn_hosttest <Multidyn.vst3> <output dir>
#include "Crossover.h"
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

// The defaults are Live's OTT preset in OTT style; audio checks start from a neutral device in
// Character style (the curves they measure are Character's).
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
        set (st, bandParam (b, kBandInput), -kBakedInputDb); // cancel the baked gains
        set (st, bandParam (b, kBandOutput), -kBakedOutputDb[b]);
        set (st, bandParam (b, kAboveRatio), 1.0);
        set (st, bandParam (b, kBelowRatio), 1.0);
        set (st, bandParam (b, kAboveThresh), -12.0);
        set (st, bandParam (b, kBelowThresh), -40.0);
    }
    set (st, kSatOn, 0.0);
    set (st, multidyn::kOutput, -kBakedMasterDb);
    set (st, kPreLimit, 0.0);
    set (st, kMode, kBase);
    set (st, kStyle, kStyleCharacter);
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
                   std::fabs (plainOf (rig, bandParam (0, kBandOutput))) < 1e-6 &&
                   std::fabs (plainOf (rig, bandParam (2, kBelowRatio)) - 4.17) < 1e-6 && std::fabs (plainOf (rig, multidyn::kOutput)) < 1e-6,
               "preset defaults");
        CHECK (std::lround (plainOf (rig, kXoverSlope)) == kXover24 && plainOf (rig, kSoftenColor) < 0.5 && plainOf (rig, kSubOn) < 0.5 &&
                   std::fabs (plainOf (rig, kSubFreq) - 40.0) < 1e-6,
               "24 dB crossovers, Soften Color off, the Sub band off at 40 Hz");
        CHECK (std::lround (plainOf (rig, kStyle)) == kStyleOtt, "a fresh instance is in OTT style");

        // --- an old state (version 3: the gains around the old baked ones) sounds the same: its gains move ---
        {
            State old;
            for (uint32_t id = 0; id < kNumParams; ++id)
            {
                old.norm[id] = defaultNormalized (id);
                old.has[id] = id < kXoverSlope; // saved before the Slope existed
            }
            set (old, bandParam (1, kBandOutput), 2.0);
            auto roundTrip = [&] (int32 version) {
                MemoryStream s;
                writeState (&s, old, version);
                s.seek (0, IBStream::kIBSeekSet, nullptr);
                State got;
                readState (&s, got);
                return got;
            };
            const State v3 = roundTrip (3), v4 = roundTrip (4), v5 = roundTrip (kStateVersion);
            auto plain = [] (const State& st, uint32_t id) { return toPlain (id, st.norm[id]); };
            CHECK (std::fabs (plain (v3, bandParam (0, kBandOutput)) - 13.7) < 1e-6 && std::fabs (plain (v3, bandParam (1, kBandOutput)) - 5.4) < 1e-6 &&
                       std::fabs (plain (v3, bandParam (2, kBandInput)) - 5.2) < 1e-6 && std::fabs (plain (v3, multidyn::kOutput) + 7.0) < 1e-6,
                   "version 3 migrated: band 1 Output %.2f, band 2 Output %.2f, Output %.2f", plain (v3, bandParam (0, kBandOutput)),
                   plain (v3, bandParam (1, kBandOutput)), plain (v3, multidyn::kOutput));
            CHECK (std::fabs (plain (v4, bandParam (0, kBandOutput))) < 1e-9 && std::fabs (plain (v4, bandParam (1, kBandOutput)) - 2.0) < 1e-9,
                   "version 4 loads as it is");
            CHECK (std::fabs (plain (v5, bandParam (0, kBandOutput))) < 1e-9 && std::fabs (plain (v5, bandParam (1, kBandOutput)) - 2.0) < 1e-9,
                   "version %d loads as it is", (int)kStateVersion);
            // Style: before version 5 Multidyn's own sound, Character; now as saved (here not saved: the default, OTT)
            CHECK (std::lround (plain (v3, kStyle)) == kStyleCharacter && std::lround (plain (v4, kStyle)) == kStyleCharacter,
                   "versions 3 and 4 load in Character style");
            CHECK (std::lround (plain (v5, kStyle)) == kStyleOtt, "version %d: Style as saved (OTT)", (int)kStateVersion);
            CHECK (std::lround (plain (v3, kXoverSlope)) == kXover24 && plain (v3, kSubOn) < 0.5 && plain (v3, kSoftenColor) < 0.5,
                   "the new parameters where an old project was");
            // through the plug-in: the controller shows the migrated value
            CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, old, 3); }), "old state applied");
            CHECK (std::fabs (plainOf (rig, bandParam (0, kBandOutput)) - 13.7) < 1e-6, "controller: band 1 Output %.2f",
                   plainOf (rig, bandParam (0, kBandOutput)));
            CHECK (std::lround (plainOf (rig, kStyle)) == kStyleCharacter, "controller: an old state in Character style");
        }

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
        CHECK (rig.processor->getLatencySamples () > 48, "look-ahead + saturator latency %u", rig.processor->getLatencySamples ());
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
        CHECK (std::lround (toPlain (kStyle, back.norm[kStyle])) == kStyleCharacter, "Style saved");

        // --- the Sub band and Soften's Color through the plug-in: the latency stays; a loud 30 Hz tone
        // is turned down by the Sub band, the new settings are saved ---
        {
            const uint32 latency = rig.processor->getLatencySamples ();
            auto low = [] (int bus, int, float* buf, int n, long long pos) {
                if (bus != 0)
                    return;
                for (int i = 0; i < n; ++i)
                    buf[i] = (float)(0.5 * std::sin (2.0 * M_PI * 30.0 * (double)(pos + i) / 48000.0));
            };
            rig.param (kScOn, 0.0);
            rig.param (kSubFreq, toNormalized (kSubFreq, 80.0));
            rig.param (kSubThresh, toNormalized (kSubThresh, -30.0));
            rig.param (kSubRatio, toNormalized (kSubRatio, 1.0));
            rig.param (kSubOn, 1.0);
            rig.param (kSoftenColor, 1.0);
            rig.param (kXoverSlope, toNormalized (kXoverSlope, kXover48));
            out.clear ();
            rig.render (1.5, out, nullptr, low);
            const double flat = rms (out, 48000, 72000);
            rig.param (kSubRatio, toNormalized (kSubRatio, 8.0));
            out.clear ();
            rig.render (1.5, out, nullptr, low);
            const double squashed = rms (out, 48000, 72000);
            CHECK (dbfs (squashed) < dbfs (flat) - 10.0, "the Sub band at 1:8: %.1f dB (1:1: %.1f dB)", dbfs (squashed), dbfs (flat));
            CHECK (rig.processor->getLatencySamples () == latency, "the latency stays: %u vs %u", rig.processor->getLatencySamples (), latency);
            MemoryStream s2;
            rig.component->getState (&s2);
            s2.seek (0, IBStream::kIBSeekSet, nullptr);
            State got;
            CHECK (readState (&s2, got) && got.norm[kSubOn] > 0.5 && got.norm[kSoftenColor] > 0.5 &&
                       std::lround (toPlain (kXoverSlope, got.norm[kXoverSlope])) == kXover48,
                   "Sub band, Color and Slope saved");
        }
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

            // 3b. the thresholds cannot cross: dragging the above threshold 5 dB below the below
            // threshold pushes the below threshold down with it
            {
                const uint32_t midBelow = bandParam (kMid, kBelowThresh);
                const double above = plainOf (rig, midAbove), below = plainOf (rig, midBelow);
                const double xa = xOf (above);
                win.drag (xa, midY, xa - (above - below + 5.0) * pxPerDb, midY);
                CHECK (std::fabs (plainOf (rig, midAbove) - (below - 5.0)) < 0.4 &&
                           std::fabs (plainOf (rig, midBelow) - plainOf (rig, midAbove)) < 0.05,
                       "above %.2f pushes below %.2f (was %.2f)", plainOf (rig, midAbove), plainOf (rig, midBelow), below);
            }

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

            // 9. the Sub band: its On in the second row under the display; on, it gets a lane at the
            // bottom (four lanes now), whose threshold edge drags like a band's
            win.click (Editor::kSubOnLeft + 24, Editor::kRow2Top + 10);
            CHECK (plainOf (rig, kSubOn) > 0.5, "Sub band on");
            pump (0.3);
            CHECK (win.savePng (outDir + "/ui_multidyn_sub.png"), "screenshot with the Sub band");
            const double laneH4 = (Editor::kDisplayBottom - lanesTop - DynDisplay::kScaleHeight) / 4.0;
            const double subY = lanesTop + laneH4 * 3.5;
            const double s0 = plainOf (rig, kSubThresh);
            win.drag (xOf (s0), subY, xOf (s0) - 8.0 * pxPerDb, subY);
            CHECK (std::fabs (plainOf (rig, kSubThresh) - (s0 - 8.0)) < 0.3, "Sub threshold drag -> %.2f (want %.2f)", plainOf (rig, kSubThresh),
                   s0 - 8.0);
            win.drag (inAbove, subY, inAbove, subY + 40);
            CHECK (plainOf (rig, kSubRatio) > 4.0 * 1.3, "Sub ratio drag down: %.2f", plainOf (rig, kSubRatio));
            win.click (inAbove, subY, 2);
            CHECK (std::fabs (plainOf (rig, kSubRatio) - 1.0) < 1e-6, "double-click: the Sub band at 1:1");
            // its Attack field in the lane, its Frequency in the second row
            const double sfx = Editor::kDisplayRight - DynDisplay::kRightCol + 84 + 34;
            const double sa0 = plainOf (rig, kSubAttack);
            win.drag (sfx, subY - 11, sfx, subY - 51);
            CHECK (plainOf (rig, kSubAttack) > sa0 * 1.2, "Sub attack field drag: %.1f -> %.1f ms", sa0, plainOf (rig, kSubAttack));
            const double f0 = plainOf (rig, kSubFreq);
            win.drag (Editor::kSubFreqLeft + 30, Editor::kRow2Top + 10, Editor::kSubFreqLeft + 30, Editor::kRow2Top - 30);
            CHECK (plainOf (rig, kSubFreq) > f0 * 1.1, "Sub frequency field drag: %.1f -> %.1f Hz", f0, plainOf (rig, kSubFreq));
            // 10. Soften's Color under the Soften knob; the Slope (a menu: set here, shown in the screenshot)
            win.click (Editor::kGlobalColLeft + 30, Editor::kColorTop + 9);
            CHECK (plainOf (rig, kSoftenColor) > 0.5, "Soften Color on");
            rig.param (kXoverSlope, toNormalized (kXoverSlope, kXoverBrickwall));
            pump (0.2);
            CHECK (win.savePng (outDir + "/ui_multidyn_sub_color_brickwall.png"), "screenshot: Color on, Brickwall");
            // the Sub band off again: its lane goes
            win.click (Editor::kSubOnLeft + 24, Editor::kRow2Top + 10);
            CHECK (plainOf (rig, kSubOn) < 0.5, "Sub band off");
            // 11. the Style in the top bar (OTT | Character): OTT greys the Character-only controls
            const double styleSeg = (Editor::kStyleRight - Editor::kStyleLeft) / 2.0;
            win.click (Editor::kStyleLeft + styleSeg * 0.5, Editor::kStyleTop + 10);
            CHECK (std::lround (plainOf (rig, kStyle)) == kStyleOtt, "OTT style selected");
            pump (0.3);
            CHECK (win.savePng (outDir + "/ui_multidyn_ott_style.png"), "screenshot: OTT style");
            win.click (Editor::kStyleLeft + styleSeg * 1.5, Editor::kStyleTop + 10);
            CHECK (std::lround (plainOf (rig, kStyle)) == kStyleCharacter, "Character style selected");
        }
        rig.stop ();
        return finish ("multidyn host test");
    }
}
