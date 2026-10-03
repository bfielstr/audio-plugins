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

// a mix with too much sub (45 Hz), low mids (220 Hz) and upper mids (3.2 kHz), the rest of the
// spectrum and some noise, the same on both channels: every band has something to cut
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
            double v = 0.35 * swell * std::sin (2 * M_PI * 220.0 * s) + 0.25 * swell * std::sin (2 * M_PI * 3200.0 * s) +
                       0.3 * swell * std::sin (2 * M_PI * 45.0 * s);
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

        // the Sub band: off by default (a loud 40 Hz passes), on it is cut by about its Range (8 dB)
        const double subOff = gainDb (rig, 40.0, 0.9);
        rig.param (kSubOn, 1.0);
        const double subOn = gainDb (rig, 40.0, 0.9);
        rig.param (kSubOn, 0.0);
        CHECK (std::fabs (subOff) < 0.3 && subOn < -5.0 && subOn > -9.0, "a loud 40 Hz: Sub off %.2f dB, on %.2f dB", subOff, subOn);

        // state round trip
        rig.param (gently::kAdvanced, 1.0);
        rig.param (kStereo, toNormalized (kStereo, kMidSide));
        rig.param (bandParam (1, kFreq), toNormalized (bandParam (1, kFreq), 5000.0));
        rig.param (bandParam (0, kThreshold), toNormalized (bandParam (0, kThreshold), -30.0));
        rig.param (kSubOn, 1.0);
        rig.param (kSubFreq, toNormalized (kSubFreq, 70.0));
        rig.param (kSubThreshold, toNormalized (kSubThreshold, -36.0));
        out.clear ();
        rig.render (0.05, out, nullptr, tone (346.0, 0.1)); // (the processor takes the changes in its next block)
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (back.norm[gently::kAdvanced] >= 0.5, "Advanced saved");
        CHECK (std::lround (toPlain (kStereo, back.norm[kStereo])) == kMidSide, "the stereo mode saved");
        CHECK (std::fabs (toPlain (bandParam (1, kFreq), back.norm[bandParam (1, kFreq)]) - 5000.0) < 1.0, "band 2's frequency saved");
        CHECK (std::fabs (toPlain (bandParam (0, kThreshold), back.norm[bandParam (0, kThreshold)]) + 30.0) < 0.01, "band 1's threshold saved");
        CHECK (back.norm[kSubOn] >= 0.5 && std::fabs (toPlain (kSubFreq, back.norm[kSubFreq]) - 70.0) < 0.1 &&
                   std::fabs (toPlain (kSubThreshold, back.norm[kSubThreshold]) + 36.0) < 0.01,
               "the Sub band saved");
        // and loaded back into a fresh state of the controller
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, back); }), "setState");
        CHECK (std::fabs (plainOf (rig, kSubFreq) - 70.0) < 0.1 && plainOf (rig, kSubOn) >= 0.5, "the Sub band loaded");

        // back to the defaults for the screenshots, the Sub band on
        for (uint32_t id : {(uint32_t)gently::kAdvanced, (uint32_t)kStereo, bandParam (1, kFreq), bandParam (0, kThreshold), (uint32_t)kSubFreq,
                            (uint32_t)kSubThreshold})
            rig.param (id, defaultNormalized (id));
        rig.param (kSubOn, 1.0);
        rig.param (gently::kAdvanced, 0.0); // (Advanced is on by default: off first, the display at its full width, see xOfHz)
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            // every band cutting a mix with too much 45 Hz, 220 Hz and 3.2 kHz
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

            // drag the Sub band's handle right 60 px (its Freq rises, up to 100 Hz) and down 30 px (its Range grows)
            const double sf0 = plainOf (rig, kSubFreq), sr0 = plainOf (rig, kSubRange);
            const double sx0 = xOfHz (sf0), sy0 = yOfDb (-sr0);
            win.drag (sx0, sy0, sx0 + 60.0, sy0 + 30.0);
            pump (0.05);
            const double sf1 = plainOf (rig, kSubFreq), sr1 = plainOf (rig, kSubRange);
            CHECK (sf1 > sf0 + 5.0 && sf1 <= 100.0 && sr1 > sr0 + 2.0, "dragging the Sub handle: Freq %.1f -> %.1f Hz, Range %.1f -> %.1f dB",
                   sf0, sf1, sr0, sr1);
            // a double-click on it resets it
            win.click (xOfHz (sf1), yOfDb (-sr1), 2);
            pump (0.05);
            CHECK (std::fabs (plainOf (rig, kSubFreq) - 40.0) < 0.1 && std::fabs (plainOf (rig, kSubRange) - 8.0) < 0.01,
                   "a double-click resets the Sub band: %.1f Hz, %.1f dB", plainOf (rig, kSubFreq), plainOf (rig, kSubRange));

            // the High band (on from the host): its handle dragged left 60 px (its Freq falls, down to 2 kHz) and
            // down 30 px (its Range grows)
            rig.param (kHighOn, 1.0);
            pump (0.05);
            const double hf0 = plainOf (rig, kHighFreq), hr0 = plainOf (rig, kHighRange);
            const double hx0 = xOfHz (hf0), hy0 = yOfDb (-hr0);
            win.drag (hx0, hy0, hx0 - 60.0, hy0 + 30.0);
            pump (0.05);
            const double hf1 = plainOf (rig, kHighFreq), hr1 = plainOf (rig, kHighRange);
            CHECK (hf1 < hf0 - 500.0 && hf1 >= 2000.0 && hr1 > hr0 + 2.0, "dragging the High handle: Freq %.0f -> %.0f Hz, Range %.1f -> %.1f dB",
                   hf0, hf1, hr0, hr1);
            CHECK (win.savePng (outDir + "/ui_gently_high.png"), "screenshot, the High band");
            // it now reaches into band 2 (3 kHz, 2 octaves: up to 6 kHz); No Overlap switched on in the editor
            // splits them at the middle of the overlap, in their parameters
            auto band2Top = [&] { return plainOf (rig, bandParam (1, kFreq)) * std::exp2 (0.5 * plainOf (rig, bandParam (1, kWidth))); };
            CHECK (band2Top () > plainOf (rig, kHighFreq) * 1.01, "band 2 and High overlap: %.0f / %.0f Hz", band2Top (), plainOf (rig, kHighFreq));
            win.click (Editor::kNoOverlapLeft + 40.0, Editor::kRowTop + 9.0);
            pump (0.05);
            CHECK (plainOf (rig, kNoOverlap) >= 0.5, "No Overlap switched on from the editor");
            CHECK (band2Top () <= plainOf (rig, kHighFreq) * 1.001, "and they are apart: band 2 up to %.0f Hz, High from %.0f Hz", band2Top (),
                   plainOf (rig, kHighFreq));
            // band 2's handle dragged right 80 px: it pushes the High band's Freq up ahead of it
            {
                const double f0 = plainOf (rig, kHighFreq);
                const double bx = xOfHz (plainOf (rig, bandParam (1, kFreq))), by = yOfDb (-plainOf (rig, bandParam (1, kRange)));
                win.drag (bx, by, bx + 80.0, by);
                pump (0.05);
                CHECK (plainOf (rig, kHighFreq) > f0 * 1.2 && band2Top () <= plainOf (rig, kHighFreq) * 1.001,
                       "band 2 pushes High: %.0f -> %.0f Hz (band 2 up to %.0f Hz)", f0, plainOf (rig, kHighFreq), band2Top ());
            }
            CHECK (win.savePng (outDir + "/ui_gently_no_overlap.png"), "screenshot, No Overlap");
            for (uint32_t id : {(uint32_t)kNoOverlap, (uint32_t)kHighOn, (uint32_t)kHighFreq, (uint32_t)kHighRange, bandParam (1, kFreq),
                                bandParam (1, kWidth)})
                rig.param (id, defaultNormalized (id));

            // Advanced: a Threshold per band on the sliders at the right of the display, the region Drive
            rig.param (gently::kAdvanced, 1.0);
            rig.param (kDrive, 1.0);
            rig.param (bandParam (0, kThreshold), toNormalized (bandParam (0, kThreshold), -26.0));
            rig.param (bandParam (1, kThreshold), toNormalized (bandParam (1, kThreshold), -22.0));
            rig.param (kSubThreshold, toNormalized (kSubThreshold, -24.0));
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
            // and the Sub band's (the third slider) down 40 px: its threshold falls
            const double u0 = plainOf (rig, kSubThreshold);
            const double ux = sx + (smacheratr::ThresholdSlider::kWidth + smacheratr::ThresholdSlider::kGap);
            win.drag (ux, sy, ux, sy + 40.0);
            pump (0.05);
            const double u1 = plainOf (rig, kSubThreshold);
            CHECK (u1 < u0 - 5.0, "dragging the Sub Threshold slider down: %.1f -> %.1f dB", u0, u1);
        }
        return finish ("gently host test");
    }
}
