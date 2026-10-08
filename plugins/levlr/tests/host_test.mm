// End-to-end test of the built Levlr.vst3. usage: levlr_hosttest <Levlr.vst3> <output dir>
#include "Crossover.h"
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "smacheratr/src/core/TailExt.h"
#include "ui/Editor.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <array>
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
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets
        checkNewInstanceGentlr (rig.controller, kTailBase + pk::kTailOn, kTailExtBase + pk::kTailExtClarity, kTailExt3Base + pk::kTailExt3Slope);
        // the end saturator as a new instance had it up to 0.24 (off, its Gentlr off, 12 / 12), before it
        // starts: the checks are about levlr's own sound
        {
            State st;
            for (uint32_t id = 0; id < kNumParams; ++id)
            {
                st.norm[id] = defaultNormalized (id);
                st.has[id] = true;
            }
            st.norm[kTailBase + pk::kTailOn] = 0.0;
            st.norm[kTailExtBase + pk::kTailExtClarity] = 0.0;
            st.norm[kTailExt3Base + pk::kTailExt3Slope] = 0.0;
            CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState: the end saturator as it was");
        }
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

        // the drives' latency is reported, and it stays whatever the drives and Bands do
        const uint32 lat0 = rig.processor->getLatencySamples ();
        CHECK (lat0 > 0, "the latency is reported: %u", (unsigned)lat0);

        // Bands 2: band 2 is everything above crossover 1, so a tone in band 4's old range rises with it
        rig.param (kBandCount, toNormalized (kBandCount, 1.0));
        out.clear ();
        rig.render (0.5, out, nullptr, tone (12000.0, 0.1));
        const double two = dbfs (rms (out, out.size () / 2, out.size ())) - dbfs (0.1 / std::sqrt (2.0));
        CHECK (std::fabs (two - 12.0) < 0.6, "Bands 2, band 2 +12: %.2f dB at 12 kHz", two);
        CHECK (rig.processor->getLatencySamples () == lat0, "Bands keeps the latency");

        // band 2's drive (Hard Clip, 18 dB) on a loud tone: harmonics; the level stays near (auto gain)
        rig.param (bandParam (1, kGain), toNormalized (bandParam (1, kGain), 0.0));
        rig.param (driveParam (1, kDriveDb), toNormalized (driveParam (1, kDriveDb), 18.0));
        rig.param (driveParam (1, kDriveType), toNormalized (driveParam (1, kDriveType), (double)kDriveHard));
        out.clear ();
        rig.render (0.5, out, nullptr, tone (346.0, 0.25));
        const double driven = dbfs (rms (out, out.size () / 2, out.size ())) - dbfs (0.25 / std::sqrt (2.0));
        double peak = 0.0;
        for (size_t i = out.size () / 2; i < out.size (); ++i)
            peak = std::max (peak, (double)std::fabs (out[i]));
        CHECK (allFinite (out) && std::fabs (driven) < 4.0, "driven: %.2f dB", driven);
        CHECK (peak / std::pow (10.0, (driven + dbfs (0.25 / std::sqrt (2.0))) / 20.0) < 1.3, "hard-clipped: a flat top (crest %.2f)",
               peak / std::pow (10.0, (driven + dbfs (0.25 / std::sqrt (2.0))) / 20.0));
        CHECK (rig.processor->getLatencySamples () == lat0, "a drive keeps the latency");
        // the drives' Oversampling: Off takes their latency away (once the processor has it), 4x (the
        // default) brings it back
        rig.param (kDriveOversampling, toNormalized (kDriveOversampling, kDriveOsOff));
        out.clear ();
        rig.render (0.05, out, nullptr, tone (346.0, 0.25));
        CHECK (rig.processor->getLatencySamples () < lat0, "Oversampling Off: less latency (%u vs %u)",
               (unsigned)rig.processor->getLatencySamples (), (unsigned)lat0);
        rig.param (kDriveOversampling, toNormalized (kDriveOversampling, kDriveOs4x));
        out.clear ();
        rig.render (0.05, out, nullptr, tone (346.0, 0.25));
        CHECK (rig.processor->getLatencySamples () == lat0, "Oversampling 4x: the latency it had");
        rig.param (driveParam (1, kDriveDb), 0.0);
        rig.param (kBandCount, 1.0);
        rig.param (bandParam (1, kGain), toNormalized (bandParam (1, kGain), 12.0));

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
        CHECK (back.has[kBandCount] && back.has[driveParam (3, kDriveType)], "Bands and the drives saved");

        // a 0.6.0 state (version 2, without Bands and the drives): four bands, no drive
        rig.param (kBandCount, toNormalized (kBandCount, 0.0));
        rig.param (driveParam (2, kDriveDb), toNormalized (driveParam (2, kDriveDb), 20.0));
        CHECK (rig.applyState ([] (IBStream* stream) {
                   // (the values a 0.6.0 instance had: the end saturator and its Gentlr off, 12 / 12)
                   std::array<double, kNumParams> norm {};
                   std::array<bool, kNumParams> has {};
                   for (uint32 id = 0; id < kNumParams; ++id)
                       norm[id] = defaultNormalized (id);
                   smacheratr::tailOldDefaults (norm, has, kTailBase, kTailExtBase, kTailExt3Base);
                   IBStreamer s (stream, kLittleEndian);
                   bool ok = s.writeInt32 (0x4C45564C) && s.writeInt32 (2) && s.writeInt32 ((int32)kBandCount);
                   for (uint32 id = 0; ok && id < kBandCount; ++id)
                       ok = s.writeInt32u (id) && s.writeDouble (norm[id]);
                   return ok;
               }),
               "a 0.6.0 state loads");
        CHECK (bandsOf (plainOf (rig, kBandCount)) == 4 && plainOf (rig, driveParam (2, kDriveDb)) == 0.0,
               "0.6.0: four bands (%d), band 3's drive off (%.1f dB)", bandsOf (plainOf (rig, kBandCount)),
               plainOf (rig, driveParam (2, kDriveDb)));
        out.clear ();
        rig.render (0.5, out, nullptr, tone (346.0, 0.1));
        const double old = dbfs (rms (out, out.size () / 2, out.size ())) - dbfs (0.1 / std::sqrt (2.0));
        CHECK (std::fabs (old) < 0.2, "0.6.0's defaults: flat (%.2f dB)", old);

        // the end saturator's section folds: a new instance (the saturator off) shows its two strips only (this
        // host keeps the window's size, so the space under them stays); a click on the saturator's strip opens
        // it (its Pre-Limit can be clicked), another folds it; switching it on in its strip opens it, and a new
        // window opens as it was left. Its Color | Gentlr switch: with Gentlr in front the row under the
        // displays holds the selected band's values
        {
            const double tx = Editor::kViewLeft, ty = Editor::kTailTop, tw = Editor::kViewRight - Editor::kViewLeft;
            const double stripX = tx + 300.0, stripY = ty + 11.0, onX = tx + 42.0;
            const double preX = tx + 45.0, preY = ty + smacheratr::TailPanel::kRowA + 9.0;
            const uint32_t pre = kTailBase + pk::kTailPreLimit;
            CHECK (plainOf (rig, kTailBase + pk::kTailOn) < 0.5 && plainOf (rig, pre) >= 0.5, "the saturator off, its Pre-Limit on");
            {
                EditorWindow win (rig.controller);
                CHECK (win.ok (), "editor");
                CHECK (win.savePng (outDir + "/ui_levlr_tail_folded.png"), "screenshot, the end saturator folded");
                win.click (preX, preY);
                pump (0.05);
                CHECK (plainOf (rig, pre) >= 0.5, "folded: its Pre-Limit is not there to click");
                win.click (stripX, stripY);
                pump (0.05);
                win.click (preX, preY);
                pump (0.05);
                CHECK (plainOf (rig, pre) < 0.5, "a click on the strip opens it: Pre-Limit clicked off");
                win.click (stripX, stripY);
                pump (0.05);
                win.click (preX, preY);
                pump (0.05);
                CHECK (plainOf (rig, pre) < 0.5, "another click folds it again");
                win.click (onX, stripY);
                pump (0.05);
                CHECK (plainOf (rig, kTailBase + pk::kTailOn) >= 0.5, "switched on in its strip");
                win.click (preX, preY);
                pump (0.05);
                CHECK (plainOf (rig, pre) >= 0.5, "and opened: Pre-Limit clicked on again");
            }
            {
                EditorWindow win (rig.controller);
                CHECK (win.ok (), "editor");
                win.click (preX, preY);
                pump (0.05);
                CHECK (plainOf (rig, pre) < 0.5, "a new window: open as it was left");
                win.click (preX, preY);
                pump (0.05);
                // Gentlr in front: band 1's Range where the colour Width is with Color in front
                const uint32_t range1 = kTailExtBase + pk::kTailExtClarityRange, cWidth = kTailExtBase + pk::kTailExtColorWidth;
                const double r0 = plainOf (rig, range1), w0 = plainOf (rig, cWidth);
                const double boxX = tx + 225.0, boxY = ty + smacheratr::TailPanel::kRowB + 9.0;
                win.click (tx + tw - 10.0 - 35.0, stripY); // (the switch's Gentlr half)
                pump (0.05);
                win.drag (boxX, boxY, boxX, boxY - 30.0);
                pump (0.05);
                CHECK (plainOf (rig, range1) > r0 + 1.0 && plainOf (rig, cWidth) == w0, "Gentlr in front: band 1's Range dragged (%.1f -> %.1f dB)",
                       r0, plainOf (rig, range1));
                win.click (tx + tw - 10.0 - 105.0, stripY); // (Color again)
                pump (0.05);
                rig.param (range1, toNormalized (range1, r0));
            }
            rig.param (kTailBase + pk::kTailOn, 0.0);
        }

        // editor screenshot: a signal playing, the bands at different levels, the saturator on
        rig.param (kSlope, toNormalized (kSlope, kSlope24));
        rig.param (xoverParam (1), toNormalized (xoverParam (1), 1000.0));
        const double gains[kBands] = {6.0, -4.0, 3.0, -9.0};
        for (int b = 0; b < kBands; ++b)
            rig.param (bandParam (b, kGain), toNormalized (bandParam (b, kGain), gains[b]));
        // bands 1 and 3 driven (Tube, Fold)
        rig.param (driveParam (0, kDriveDb), toNormalized (driveParam (0, kDriveDb), 9.0));
        rig.param (driveParam (0, kDriveType), toNormalized (driveParam (0, kDriveType), (double)kDriveTube));
        rig.param (driveParam (2, kDriveDb), toNormalized (driveParam (2, kDriveDb), 14.0));
        rig.param (driveParam (2, kDriveType), toNormalized (driveParam (2, kDriveType), (double)kDriveFold));
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

            // Bands: click 3 (band 4's column and its part of the display go)
            const double segW = (Editor::kBandsRight - Editor::kBandsLeft) / 4.0;
            win.click (Editor::kBandsLeft + 2.5 * segW, Editor::kBandsTop + 10.0);
            pump (0.05);
            CHECK (bandsOf (plainOf (rig, kBandCount)) == 3, "Bands 3 clicked: %d", bandsOf (plainOf (rig, kBandCount)));
            for (int i = 0; i < 10; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, music ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_levlr_3_bands.png"), "screenshot, 3 bands, two driven");

            // band 1's Drive knob: double-click resets it (off)
            win.click (Editor::kViewLeft + 28.0, Editor::kDriveTop + 30.0, 2);
            pump (0.05);
            CHECK (plainOf (rig, driveParam (0, kDriveDb)) == 0.0, "band 1's drive reset: %.1f dB", plainOf (rig, driveParam (0, kDriveDb)));
            // band 2's drive dragged up
            win.drag (Editor::kViewLeft + Editor::kColumnW + 28.0, Editor::kDriveTop + 30.0, Editor::kViewLeft + Editor::kColumnW + 28.0,
                      Editor::kDriveTop - 20.0);
            pump (0.05);
            CHECK (plainOf (rig, driveParam (1, kDriveDb)) > 3.0, "band 2's drive dragged up: %.1f dB", plainOf (rig, driveParam (1, kDriveDb)));
            win.click (Editor::kBandsLeft + 3.5 * segW, Editor::kBandsTop + 10.0);
            pump (0.05);
            CHECK (bandsOf (plainOf (rig, kBandCount)) == 4, "Bands 4 again");

            // the end saturator's Gentlr with Advanced on: the Threshold sliders and the region Drive at
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
            CHECK (plainOf (rig, kTailExt2Base + pk::kTailExt2Advanced) >= 0.5, "the end saturator's Gentlr: Advanced on");
            CHECK (win.savePng (outDir + "/ui_levlr_gentlr_advanced.png"), "screenshot, Gentlr Advanced in the end saturator");
        }
        return finish ("levlr host test");
    }
}
