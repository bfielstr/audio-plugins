// End-to-end test of the built Gently.vst3. usage: gently_hosttest <Gently.vst3> <output dir>
#include "Engine.h"
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/Editor.h"
#include "ui/GentlyView.h"

#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace gently;
#define CHECK PK_CHECK

static InputFn tone (double hz, double amp)
{
    return [hz, amp] (int, int, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
            buf[i] = (float)(amp * std::sin (2 * M_PI * hz * (double)(pos + i) / 48000.0));
    };
}

// a mix with too much of the low mids (220 Hz) and of the upper mids (3.2 kHz), the rest of the
// spectrum and some noise, the same on both channels: both bands have something to cut
static InputFn harshMix ()
{
    return [] (int, int, float* buf, int n, long long pos) {
        static const double freqs[] = {55.0, 110.0, 440.0, 880.0, 1760.0, 7040.0, 12000.0};
        for (int i = 0; i < n; ++i)
        {
            const long long t = pos + i;
            const double s = (double)t / 48000.0;
            // the loud parts swell and fall back, so the cuts move
            const double swell = 0.6 + 0.4 * std::sin (2 * M_PI * 0.7 * s);
            double v = 0.35 * swell * std::sin (2 * M_PI * 220.0 * s) + 0.25 * swell * std::sin (2 * M_PI * 3200.0 * s);
            double a = 0.12;
            for (double f : freqs)
            {
                v += a * std::sin (2 * M_PI * f * s);
                a *= 0.7;
            }
            uint32_t h = (uint32_t)(t * 2654435761u);
            h ^= h >> 13;
            h *= 0x5bd1e995u;
            h ^= h >> 15;
            v += 0.03 * ((double)(h & 0xFFFF) / 32768.0 - 1.0);
            buf[i] = (float)v;
        }
    };
}

static double plainOf (Rig& rig, uint32_t id) { return toPlain (id, rig.controller->getParamNormalized (id)); }

// where the display puts things (GentlyView's layout, the display at its full width: Advanced off)
static double xOfHz (double hz)
{
    const double l = Editor::kViewLeft, r = Editor::kViewRight;
    return l + std::log (hz / GentlyView::kMinHz) / std::log (GentlyView::kMaxHz / GentlyView::kMinHz) * (r - l);
}
static double yOfDb (double db)
{
    const double t = Editor::kViewTop + 8.0, b = Editor::kViewBottom - 16.0;
    return t + (GentlyView::kTopDb - db) / (GentlyView::kTopDb - GentlyView::kBottomDb) * (b - t);
}

static double gainDb (Rig& rig, double hz, double amp)
{
    std::vector<float> out;
    rig.render (0.8, out, nullptr, tone (hz, amp));
    return dbfs (rms (out, out.size () / 2, out.size ())) - dbfs (amp / std::sqrt (2.0));
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
            return finish ("gently host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (rig.component->getBusCount (kEvent, kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "all automatable");
        CHECK (rig.start (), "start");
        {
            Engine ref;
            ref.prepare (48000.0, 480);
            CHECK ((int)rig.processor->getLatencySamples () == ref.latency (), "latency %d (%d)", (int)rig.processor->getLatencySamples (),
                   ref.latency ());
        }

        // at the defaults a quiet tone passes as it is
        std::vector<float> out;
        const double quiet = gainDb (rig, 346.0, 0.03);
        CHECK (std::fabs (quiet) < 0.1, "a quiet tone untouched: %.2f dB", quiet);

        // a loud tone in band 1 (250 Hz) is turned down by about its Range (8 dB); with band 1 off it
        // passes (band 2, at 3 kHz, is far from it)
        const double cut = gainDb (rig, 250.0, 0.9);
        rig.param (bandParam (0, kOn), 0.0);
        const double off = gainDb (rig, 250.0, 0.9);
        rig.param (bandParam (0, kOn), 1.0);
        CHECK (cut < -5.0 && cut > -9.0 && std::fabs (off) < 0.2, "loud in band 1: %.2f dB; band 1 off: %.2f dB", cut, off);
        out.clear ();
        rig.render (0.2, out, nullptr, tone (250.0, 0.9));
        CHECK (allFinite (out), "finite");

        // state round trip
        rig.param (kAdvanced, 1.0);
        rig.param (kStereo, toNormalized (kStereo, kMidSide));
        rig.param (bandParam (1, kFreq), toNormalized (bandParam (1, kFreq), 5000.0));
        rig.param (bandParam (0, kThreshold), toNormalized (bandParam (0, kThreshold), -30.0));
        out.clear ();
        rig.render (0.05, out, nullptr, tone (346.0, 0.1)); // (the processor takes the changes in its next block)
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (back.norm[kAdvanced] >= 0.5, "Advanced saved");
        CHECK (std::lround (toPlain (kStereo, back.norm[kStereo])) == kMidSide, "the stereo mode saved");
        CHECK (std::fabs (toPlain (bandParam (1, kFreq), back.norm[bandParam (1, kFreq)]) - 5000.0) < 1.0, "band 2's frequency saved");
        CHECK (std::fabs (toPlain (bandParam (0, kThreshold), back.norm[bandParam (0, kThreshold)]) + 30.0) < 0.01, "band 1's threshold saved");

        // back to the defaults for the screenshots
        for (uint32_t id : {(uint32_t)kAdvanced, (uint32_t)kStereo, bandParam (1, kFreq), bandParam (0, kThreshold)})
            rig.param (id, defaultNormalized (id));
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            // both bands cutting a mix with too much 220 Hz and 3.2 kHz
            for (int i = 0; i < 24; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, harshMix ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_gently.png"), "screenshot");

            // drag band 1's handle down 30 px: its Range grows
            const double before = plainOf (rig, bandParam (0, kRange));
            const double hx = xOfHz (plainOf (rig, bandParam (0, kFreq))), hy = yOfDb (-before);
            win.drag (hx, hy, hx, hy + 30.0);
            pump (0.05);
            const double after = plainOf (rig, bandParam (0, kRange));
            CHECK (after > before + 2.0, "dragging band 1's handle down: Range %.1f -> %.1f dB", before, after);
            rig.param (bandParam (0, kRange), defaultNormalized (bandParam (0, kRange)));

            // Advanced: a Threshold per band on the sliders at the right of the display, the region Drive
            rig.param (kAdvanced, 1.0);
            rig.param (kDrive, 1.0);
            rig.param (bandParam (0, kThreshold), toNormalized (bandParam (0, kThreshold), -26.0));
            rig.param (bandParam (1, kThreshold), toNormalized (bandParam (1, kThreshold), -22.0));
            for (int i = 0; i < 16; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, harshMix ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_gently_advanced.png"), "screenshot, Advanced");

            // drag band 2's Threshold slider up 40 px: its threshold rises
            const double t0 = plainOf (rig, bandParam (1, kThreshold));
            const double sx = Editor::kViewRight - smacheratr::ThresholdSlider::kStripWidth + smacheratr::ThresholdSlider::kGap +
                              (smacheratr::ThresholdSlider::kWidth + smacheratr::ThresholdSlider::kGap) + 12.0;
            const double sy = 0.5 * (Editor::kViewTop + Editor::kViewBottom);
            win.drag (sx, sy, sx, sy - 40.0);
            pump (0.05);
            const double t1 = plainOf (rig, bandParam (1, kThreshold));
            CHECK (t1 > t0 + 5.0, "dragging band 2's Threshold slider up: %.1f -> %.1f dB", t0, t1);
        }
        return finish ("gently host test");
    }
}
