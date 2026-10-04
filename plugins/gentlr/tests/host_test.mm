// End-to-end test of the built Gentlr.vst3. usage: gentlr_hosttest <Gentlr.vst3> <output dir>
#include "Engine.h"
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/Editor.h"
#include "ui/GentlrView.h"

#include "public.sdk/source/common/memorystream.h"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstdint>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace gentlr;
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

// where the display puts things (GentlrView's layout, the display at its full width: Advanced off)
// What the layout check (pk::layoutReport, written when the first editor opened: HostRig) found: its
// lines other than the editor's heading
static int layoutFindings ()
{
    std::ifstream f (std::string ([NSTemporaryDirectory () UTF8String]) + "pk_layout_report.txt");
    int n = 0;
    for (std::string line; std::getline (f, line);)
        n += !line.empty () && line.rfind ("==", 0) != 0;
    return n;
}

static double xOfHz (double hz)
{
    const double l = Editor::kViewLeft, r = Editor::kViewRight;
    return l + std::log (hz / GentlrView::kMinHz) / std::log (GentlrView::kMaxHz / GentlrView::kMinHz) * (r - l);
}
static double yOfDb (double db)
{
    const double t = Editor::kViewTop + 8.0, b = Editor::kViewBottom - 16.0;
    return t + (GentlrView::kTopDb - db) / (GentlrView::kTopDb - GentlrView::kBottomDb) * (b - t);
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
            return finish ("gentlr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (rig.component->getBusCount (kEvent, kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "all automatable");
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets
        // the Sub and High bands have no On: a fresh instance has them at Range 0 (no cut)
        CHECK (plainOf (rig, kSubRange) == 0.0 && plainOf (rig, kHighRange) == 0.0, "Sub and High start at Range 0: %.1f / %.1f dB",
               plainOf (rig, kSubRange), plainOf (rig, kHighRange));
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

        // the Sub band: at Range 0 by default (a loud 40 Hz passes), with 8 dB it is cut by about that
        const double subOff = gainDb (rig, 40.0, 0.9);
        rig.param (kSubRange, toNormalized (kSubRange, 8.0));
        const double subOn = gainDb (rig, 40.0, 0.9);
        rig.param (kSubRange, 0.0);
        CHECK (std::fabs (subOff) < 0.3 && subOn < -5.0 && subOn > -9.0, "a loud 40 Hz: Sub at Range 0 %.2f dB, 8 dB %.2f dB", subOff, subOn);

        // state round trip
        rig.param (gentlr::kAdvanced, 1.0);
        rig.param (kStereo, toNormalized (kStereo, kMidSide));
        rig.param (bandParam (1, kFreq), toNormalized (bandParam (1, kFreq), 5000.0));
        rig.param (bandParam (0, kThreshold), toNormalized (bandParam (0, kThreshold), -30.0));
        rig.param (kSubRange, toNormalized (kSubRange, 10.0));
        rig.param (kSubFreq, toNormalized (kSubFreq, 70.0));
        rig.param (kSubThreshold, toNormalized (kSubThreshold, -36.0));
        out.clear ();
        rig.render (0.05, out, nullptr, tone (346.0, 0.1)); // (the processor takes the changes in its next block)
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (back.norm[gentlr::kAdvanced] >= 0.5, "Advanced saved");
        CHECK (std::lround (toPlain (kStereo, back.norm[kStereo])) == kMidSide, "the stereo mode saved");
        CHECK (std::fabs (toPlain (bandParam (1, kFreq), back.norm[bandParam (1, kFreq)]) - 5000.0) < 1.0, "band 2's frequency saved");
        CHECK (std::fabs (toPlain (bandParam (0, kThreshold), back.norm[bandParam (0, kThreshold)]) + 30.0) < 0.01, "band 1's threshold saved");
        CHECK (std::fabs (toPlain (kSubRange, back.norm[kSubRange]) - 10.0) < 0.01 && std::fabs (toPlain (kSubFreq, back.norm[kSubFreq]) - 70.0) < 0.1 &&
                   std::fabs (toPlain (kSubThreshold, back.norm[kSubThreshold]) + 36.0) < 0.01,
               "the Sub band saved");
        // and loaded back into a fresh state of the controller
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, back); }), "setState");
        CHECK (std::fabs (plainOf (rig, kSubFreq) - 70.0) < 0.1 && std::fabs (plainOf (rig, kSubRange) - 10.0) < 0.01, "the Sub band loaded");

        // back to the defaults for the screenshots, the Sub band at 8 dB
        for (uint32_t id : {(uint32_t)gentlr::kAdvanced, (uint32_t)kStereo, bandParam (1, kFreq), bandParam (0, kThreshold), (uint32_t)kSubFreq,
                            (uint32_t)kSubThreshold})
            rig.param (id, defaultNormalized (id));
        rig.param (kSubRange, toNormalized (kSubRange, 8.0));
        rig.param (gentlr::kAdvanced, 0.0); // (Advanced is on by default: off first, the display at its full width, see xOfHz)
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            CHECK (layoutFindings () == 0, "the layout check finds nothing overlapping, touching or spilling (printed above)");
            // every band cutting a mix with too much 45 Hz, 220 Hz and 3.2 kHz
            for (int i = 0; i < 24; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, harshMix ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_gentlr.png"), "screenshot");

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
            CHECK (std::fabs (plainOf (rig, kSubFreq) - 40.0) < 0.1 && plainOf (rig, kSubRange) == 0.0,
                   "a double-click resets the Sub band (to Range 0): %.1f Hz, %.1f dB", plainOf (rig, kSubFreq), plainOf (rig, kSubRange));
            rig.param (kSubRange, toNormalized (kSubRange, 8.0)); // (back at work for the screenshots below)

            // the High band, at Range 0 (its default): its handle sits at 0 dB; dragged left 60 px (its Freq
            // falls, down to 2 kHz) and down 30 px it gets a Range, and works
            pump (0.05);
            const double hf0 = plainOf (rig, kHighFreq), hr0 = plainOf (rig, kHighRange);
            CHECK (hr0 == 0.0, "the High band at Range 0: %.1f dB", hr0);
            const double hx0 = xOfHz (hf0), hy0 = yOfDb (-hr0);
            win.drag (hx0, hy0, hx0 - 60.0, hy0 + 30.0);
            pump (0.05);
            const double hf1 = plainOf (rig, kHighFreq), hr1 = plainOf (rig, kHighRange);
            CHECK (hf1 < hf0 - 500.0 && hf1 >= 2000.0 && hr1 > hr0 + 2.0, "dragging the High handle: Freq %.0f -> %.0f Hz, Range %.1f -> %.1f dB",
                   hf0, hf1, hr0, hr1);
            CHECK (bandWorks (kHigh, plainOf (rig, kHighOn), hr1), "and the High band works");
            CHECK (win.savePng (outDir + "/ui_gentlr_high.png"), "screenshot, the High band");
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
            CHECK (win.savePng (outDir + "/ui_gentlr_no_overlap.png"), "screenshot, No Overlap");
            for (uint32_t id : {(uint32_t)kNoOverlap, (uint32_t)kHighFreq, (uint32_t)kHighRange, bandParam (1, kFreq),
                                bandParam (1, kWidth)})
                rig.param (id, defaultNormalized (id));

            // glue on touch: band 1 (250 Hz, 2 octaves: 125 - 500 Hz) and band 2 at 1 kHz, an octave wide
            // (707 - 1414 Hz). Band 1's high edge dragged to 3 px short of band 2's low edge snaps onto it, and
            // when the drag ends the two are glued (the link icon on the border, the switch on)
            {
                rig.param (bandParam (1, kFreq), toNormalized (bandParam (1, kFreq), 1000.0));
                rig.param (bandParam (1, kWidth), toNormalized (bandParam (1, kWidth), 1.0));
                pump (0.05);
                auto edge1 = [&] { return plainOf (rig, bandParam (0, kFreq)) * std::exp2 (0.5 * plainOf (rig, bandParam (0, kWidth))); };
                auto edge2 = [&] { return plainOf (rig, bandParam (1, kFreq)) / std::exp2 (0.5 * plainOf (rig, bandParam (1, kWidth))); };
                const double ey = yOfDb (-20.0); // (under the readouts, away from the handles)
                CHECK (plainOf (rig, kGlue12) < 0.5, "nothing glued by default");
                win.drag (xOfHz (edge1 ()), ey, xOfHz (edge2 ()) - 3.0, ey);
                pump (0.05);
                CHECK (plainOf (rig, kGlue12) >= 0.5, "band 1's edge dragged onto band 2's: glued");
                CHECK (std::fabs (std::log2 (edge1 () / edge2 ())) < 1e-3, "and touching: %.1f / %.1f Hz", edge1 (), edge2 ());
                CHECK (win.savePng (outDir + "/ui_gentlr_glue.png"), "screenshot, glued");
                // band 1's handle moved left 40 px: band 2's low edge follows it (band 2 widens), band 2's top stays
                const double top2 = plainOf (rig, bandParam (1, kFreq)) * std::exp2 (0.5 * plainOf (rig, bandParam (1, kWidth)));
                const double hx = xOfHz (plainOf (rig, bandParam (0, kFreq))), hy = yOfDb (-plainOf (rig, bandParam (0, kRange)));
                win.drag (hx, hy, hx - 40.0, hy);
                pump (0.05);
                const double top2b = plainOf (rig, bandParam (1, kFreq)) * std::exp2 (0.5 * plainOf (rig, bandParam (1, kWidth)));
                CHECK (std::fabs (std::log2 (edge1 () / edge2 ())) < 1e-3 && edge2 () < 650.0 && std::fabs (top2b / top2 - 1.0) < 1e-3,
                       "band 1 moved: band 2's low edge with it (%.1f / %.1f Hz), its top stays (%.0f Hz)", edge1 (), edge2 (), top2b);
                // a click on the link icon (on the border, at the bottom of the display) detaches them; they stay put
                const double f1 = plainOf (rig, bandParam (0, kFreq)), f2 = plainOf (rig, bandParam (1, kFreq));
                win.click (xOfHz (edge1 ()), Editor::kViewBottom - 16.0 - 10.0);
                pump (0.05);
                CHECK (plainOf (rig, kGlue12) < 0.5, "the link icon clicked: detached");
                CHECK (plainOf (rig, bandParam (0, kFreq)) == f1 && plainOf (rig, bandParam (1, kFreq)) == f2, "and the bands stay where they were");
                // clicked again (they still touch): glued again
                win.click (xOfHz (edge1 ()), Editor::kViewBottom - 16.0 - 10.0);
                pump (0.05);
                CHECK (plainOf (rig, kGlue12) >= 0.5, "clicked again: glued");
                for (uint32_t id : {(uint32_t)kGlue12, bandParam (0, kFreq), bandParam (0, kWidth), bandParam (1, kFreq), bandParam (1, kWidth)})
                    rig.param (id, defaultNormalized (id));
            }

            // Advanced: a Threshold per band on the sliders at the right of the display, the region Drive
            rig.param (gentlr::kAdvanced, 1.0);
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
            CHECK (win.savePng (outDir + "/ui_gentlr_advanced.png"), "screenshot, Advanced");

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
        return finish ("gentlr host test");
    }
}
