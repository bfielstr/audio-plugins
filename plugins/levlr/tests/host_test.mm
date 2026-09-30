// End-to-end test of the built Levlr.vst3. usage: levlr_hosttest <Levlr.vst3> <output dir>
#include "Crossover.h"
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/Editor.h"

#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace levlr;
#define CHECK PK_CHECK

static InputFn tone (double hz, double amp)
{
    return [hz, amp] (int, int, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
            buf[i] = (float)(amp * std::sin (2 * M_PI * hz * (double)(pos + i) / 48000.0));
    };
}

// a rough pink-ish noise (a mix of sines across the spectrum and some white noise), the same on both channels
static InputFn music ()
{
    return [] (int, int, float* buf, int n, long long pos) {
        static const double freqs[] = {55.0, 110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0, 7040.0, 12000.0};
        for (int i = 0; i < n; ++i)
        {
            const long long t = pos + i;
            double v = 0.0, a = 0.2;
            for (double f : freqs)
            {
                v += a * std::sin (2 * M_PI * f * (double)t / 48000.0);
                a *= 0.75;
            }
            uint32_t s = (uint32_t)(t * 2654435761u);
            s ^= s >> 13;
            s *= 0x5bd1e995u;
            s ^= s >> 15;
            v += 0.03 * ((double)(s & 0xFFFF) / 32768.0 - 1.0);
            buf[i] = (float)v;
        }
    };
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
            return finish ("levlr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (rig.component->getBusCount (kEvent, kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "all automatable");
        CHECK (rig.start (), "start");

        // at the defaults (every band at 0 dB, saturator off) a tone keeps its level
        std::vector<float> out;
        rig.render (0.5, out, nullptr, tone (346.0, 0.1));
        const double flat = dbfs (rms (out, out.size () / 2, out.size ())) - dbfs (0.1 / std::sqrt (2.0));
        CHECK (std::fabs (flat) < 0.2, "flat at 0 dB: %.2f dB", flat);
        CHECK (allFinite (out), "finite");

        // band 2 (120 Hz .. 1 kHz) at +12 dB: the same tone rises 12 dB; one in band 4 doesn't
        rig.param (bandParam (1, kGain), toNormalized (bandParam (1, kGain), 12.0));
        out.clear ();
        rig.render (0.5, out, nullptr, tone (346.0, 0.1));
        const double up = dbfs (rms (out, out.size () / 2, out.size ())) - dbfs (0.1 / std::sqrt (2.0));
        out.clear ();
        rig.render (0.5, out, nullptr, tone (12000.0, 0.1));
        const double other = dbfs (rms (out, out.size () / 2, out.size ())) - dbfs (0.1 / std::sqrt (2.0));
        CHECK (std::fabs (up - 12.0) < 0.6 && std::fabs (other) < 0.6, "band 2 +12: %.2f dB; band 4: %.2f dB", up, other);

        // band 2 muted: the tone goes
        rig.param (bandParam (1, kMute), 1.0);
        out.clear ();
        rig.render (0.5, out, nullptr, tone (346.0, 0.1));
        const double muted = dbfs (rms (out, out.size () / 2, out.size ())) - dbfs (0.1 / std::sqrt (2.0));
        CHECK (muted < -20.0, "muted: %.1f dB", muted);
        rig.param (bandParam (1, kMute), 0.0);

        // state round trip
        rig.param (kSlope, toNormalized (kSlope, kSlope48));
        rig.param (xoverParam (1), toNormalized (xoverParam (1), 2500.0));
        out.clear ();
        rig.render (0.05, out, nullptr, tone (346.0, 0.1)); // (the processor takes the changes in its next block)
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::lround (toPlain (kSlope, back.norm[kSlope])) == kSlope48, "the slope saved");
        CHECK (std::fabs (toPlain (xoverParam (1), back.norm[xoverParam (1)]) - 2500.0) < 1.0, "crossover 2 saved");
        CHECK (std::fabs (toPlain (bandParam (1, kGain), back.norm[bandParam (1, kGain)]) - 12.0) < 0.01, "band 2's gain saved");

        // editor screenshot: a signal playing, the bands at different levels, the saturator on
        rig.param (kSlope, toNormalized (kSlope, kSlope24));
        rig.param (xoverParam (1), toNormalized (xoverParam (1), 1000.0));
        const double gains[kBands] = {6.0, -4.0, 3.0, -9.0};
        for (int b = 0; b < kBands; ++b)
            rig.param (bandParam (b, kGain), toNormalized (bandParam (b, kGain), gains[b]));
        rig.param (kTailBase + pk::kTailOn, 1.0);
        rig.param (kTailBase + pk::kTailDrive, toNormalized (kTailBase + pk::kTailDrive, 9.0));
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, music ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_levlr.png"), "screenshot");

            // drag band 3 up 40 px in the display: its gain rises
            const double before = plainOf (rig, bandParam (2, kGain));
            const double x = Editor::kViewLeft + (Editor::kViewRight - Editor::kViewLeft) * 0.78; // about 4.4 kHz: band 3
            const double y = 0.5 * (Editor::kViewTop + Editor::kViewBottom) + 50.0;
            win.drag (x, y, x, y - 40.0);
            pump (0.05);
            const double after = plainOf (rig, bandParam (2, kGain));
            CHECK (after > before + 3.0, "dragging band 3 up raised it: %.1f -> %.1f dB", before, after);

            // the end saturator's Gently with Advanced on: the Threshold sliders and the region Drive at
            // the right of its colour display
            rig.param (kTailExtBase + pk::kTailExtClarity, 1.0);
            rig.param (kTailExt2Base + pk::kTailExt2Advanced, 1.0);
            rig.param (kTailExt2Base + pk::kTailExt2Drive, 1.0);
            rig.param (kTailExt2Base + pk::kTailExt2Threshold,
                       toNormalized (kTailExt2Base + pk::kTailExt2Threshold, -30.0));
            for (int i = 0; i < 10; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, music ());
                pump (0.03);
            }
            CHECK (plainOf (rig, kTailExt2Base + pk::kTailExt2Advanced) >= 0.5, "the end saturator's Gently: Advanced on");
            CHECK (win.savePng (outDir + "/ui_levlr_gently_advanced.png"), "screenshot, Gently Advanced in the end saturator");
        }
        return finish ("levlr host test");
    }
}
