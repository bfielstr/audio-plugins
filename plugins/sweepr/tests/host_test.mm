// End-to-end test of the built Sweepr.vst3. usage: sweepr_hosttest <Sweepr.vst3> <output dir>
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
using namespace sweepr;
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

// where the display (Classic) puts a band's handle: CurveView's scales
static double displayX (double hz)
{
    const double left = 8.0, w = Editor::kWidth - 16.0;
    return left + std::log (hz / 20.0) / std::log (1000.0) * w;
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
            return finish ("sweepr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (rig.component->getBusCount (kEvent, kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets
        // a new instance: the clips at the end off
        CHECK (rig.controller->getParamNormalized (kSoftClip) == 0.0 && rig.controller->getParamNormalized (kHardClip) == 0.0,
               "a new instance: Soft Clip and Hard Clip off");
        CHECK (std::fabs (plainOf (rig, bandParam (0, kFreq)) - 110.0) < 1e-6 && std::fabs (plainOf (rig, bandParam (1, kGain)) + 15.0) < 1e-6 &&
                   std::fabs (plainOf (rig, bandParam (0, kLfoDepth)) - 1.0) < 1e-9,
               "a new instance: the defaults (A 110 Hz, B -15 dB, Depth +1 octave)");

        // Mix 0 from the start: the input comes out bit for bit, delayed by the latency
        State st = baseState ();
        st.norm[kMix] = 0.0;
        st.norm[sweepr::kOutput] = toNormalized (sweepr::kOutput, 0.0);
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

        // the defaults on the bass: finite, the tape holding it at about -13 dBFS RMS
        rig.param (kMix, 1.0);
        rig.param (sweepr::kOutput, defaultNormalized (sweepr::kOutput));
        out.clear ();
        outR.clear ();
        rig.render (2.0, out, &outR, bass ());
        CHECK (allFinite (out) && allFinite (outR), "finite");
        const double level = dbfs (rms (out, 48000, out.size ()));
        CHECK (level > -18.0 && level < -8.0, "the defaults: %.1f dBFS RMS", level);
        // and moving: with Tape off (the tape holds the level), the level changes from beat to beat (the LFOs
        // at work)
        rig.param (kTapeOn, 0.0);
        out.clear ();
        rig.render (2.0, out, nullptr, bass ());
        double lo = 1e9, hi = -1e9;
        for (size_t a = 24000; a + 2400 <= out.size (); a += 2400) // 50 ms windows
        {
            const double l = dbfs (rms (out, a, a + 2400));
            lo = std::min (lo, l);
            hi = std::max (hi, l);
        }
        CHECK (hi - lo > 1.0, "the sweep moves the level: %.1f .. %.1f dBFS", lo, hi);
        rig.param (kTapeOn, 1.0);

        // state round trip
        rig.param (bandParam (0, kLfoShape), toNormalized (bandParam (0, kLfoShape), kSmoothRandom));
        rig.param (kLink, 1.0);
        rig.render (0.05, out, nullptr, bass ());
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::lround (toPlain (bandParam (0, kLfoShape), back.norm[bandParam (0, kLfoShape)])) == kSmoothRandom, "Shape saved");
        CHECK (back.norm[kLink] == 1.0, "Link saved");
        rig.param (kLink, 0.0);
        rig.param (bandParam (0, kLfoShape), toNormalized (bandParam (0, kLfoShape), kSine));

        // editor (Classic): Sync clicked off and on, the A handle dragged, a double-click resets it, then a screenshot
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            ViewRect r;
            CHECK (win.view () && win.view ()->getSize (&r) == kResultOk && r.getWidth () == (int32)Editor::kWidth &&
                       r.getHeight () == (int32)(Editor::kHeight + pk::EditorBase::kInfoHeight),
                   "editor size %d x %d", r.getWidth (), r.getHeight ());
            pump (0.1);
            const double sx = Editor::kBandLeft[0] + Editor::kColLeft + Editor::kColW / 2;
            const double sy = Editor::kRowTop + Editor::kLfoTop + Editor::kRowH / 2;
            win.click (sx, sy);
            pump (0.05);
            CHECK (plainOf (rig, bandParam (0, kLfoSync)) < 0.5, "Sync clicked off");
            win.click (sx, sy);
            pump (0.05);
            CHECK (plainOf (rig, bandParam (0, kLfoSync)) >= 0.5, "Sync clicked on");

            // the A handle: 100 px to the right (Freq up by 1000^(100 / 884)) and 21 px up (+ about 4.2 dB)
            const double hx = displayX (110.0), hy = displayY (13.0);
            win.drag (hx, hy, hx + 100.0, hy - 21.0);
            pump (0.05);
            const double wantHz = 110.0 * std::pow (1000.0, 100.0 / (Editor::kWidth - 16.0));
            CHECK (std::fabs (std::log (plainOf (rig, bandParam (0, kFreq)) / wantHz)) < 0.03, "A dragged to %.1f Hz (want %.1f)",
                   plainOf (rig, bandParam (0, kFreq)), wantHz);
            CHECK (plainOf (rig, bandParam (0, kGain)) > 15.5 && plainOf (rig, bandParam (0, kGain)) <= 18.0, "A's Gain dragged up to %.1f dB",
                   plainOf (rig, bandParam (0, kGain)));
            // a double-click on it resets the band
            win.click (displayX (plainOf (rig, bandParam (0, kFreq))), displayY (plainOf (rig, bandParam (0, kGain))), 2);
            pump (0.05);
            CHECK (std::fabs (plainOf (rig, bandParam (0, kFreq)) - 110.0) < 1e-6 && std::fabs (plainOf (rig, bandParam (0, kGain)) - 13.0) < 1e-6,
                   "A back to 110 Hz, +13 dB: %.1f Hz, %.1f dB", plainOf (rig, bandParam (0, kFreq)), plainOf (rig, bandParam (0, kGain)));

            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, bass ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_sweepr.png"), "screenshot");
            // Classic, Wide and Classic again: knobs found and turned in each (Wide's screenshot)
            checkLayouts (rig, win, {bandParam (0, kGain), (uint32_t)kDrive, (uint32_t)kMix}, outDir + "/ui_sweepr_wide.png");
        }
        rig.stop ();
        return finish ("sweepr host test");
    }
}
