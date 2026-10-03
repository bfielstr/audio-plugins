// End-to-end test of the built Smacheratr.vst3. usage: smacheratr_hosttest <Smacheratr.vst3> <output dir>
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/Editor.h"
#include "ui/ThresholdSlider.h"

#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace smacheratr;
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

static double toneDb (const std::vector<float>& x, double f, size_t a, size_t b)
{
    double s = 0, c = 0;
    for (size_t i = a; i < b; ++i)
    {
        s += x[i] * std::sin (2.0 * M_PI * f * i / 48000.0);
        c += x[i] * std::cos (2.0 * M_PI * f * i / 48000.0);
    }
    return 20.0 * std::log10 (std::max (1e-12, 2.0 * std::sqrt (s * s + c * c) / (double)(b - a)));
}

static InputFn tone (double hz, double amp)
{
    return [hz, amp] (int, int, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
            buf[i] = (float)(amp * std::sin (2 * M_PI * hz * (double)(pos + i) / 48000.0));
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
            return finish ("smacheratr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");

        State st = baseState ();
        st.norm[kDrive] = toNormalized (kDrive, 0.0); // the defaults are a preset; start neutral
        st.norm[kColorOn] = 0.0;
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        const uint32 latency = rig.processor->getLatencySamples ();
        CHECK (latency > 0 && latency < 200, "latency reported %u", latency);

        // transparent at drive 0: output == input delayed by the reported latency
        std::vector<float> out;
        rig.render (1.0, out, nullptr, tone (1000.0, 0.2));
        std::vector<float> ref (48000);
        tone (1000.0, 0.2) (0, 0, ref.data (), 48000, 0);
        double err = 0;
        for (size_t i = latency + 4800; i < 48000; ++i)
            err = std::max (err, (double)std::fabs (out[i] - ref[i - latency]));
        CHECK (err < 2e-3, "passthrough error %g", err);

        // drive through the plug-in: harmonics appear, the curve holds the peak at 0 dB
        rig.param (kDrive, toNormalized (kDrive, 18.0));
        out.clear ();
        rig.render (1.0, out, nullptr, tone (1000.0, 0.25));
        const size_t a = 24000, b = 48000;
        CHECK (toneDb (out, 3000.0, a, b) > -30.0, "third harmonic %.1f dB", toneDb (out, 3000.0, a, b));
        {
            // the curve holds 1.0; Hi-Quality's downsampling filter rings a little past it
            float pk = 0.0f;
            for (size_t i = a; i < b; ++i)
                pk = std::max (pk, std::fabs (out[i]));
            CHECK (pk <= 1.05f, "peak %f", pk);
        }

        // state round trip
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::fabs (toPlain (kDrive, back.norm[kDrive]) - 18.0) < 1e-6, "drive saved");

        // editor: screenshot while audio is flowing, then gestures
        rig.param (kColorOn, toNormalized (kColorOn, 1.0));
        rig.param (kColorLo, toNormalized (kColorLo, -0.3));
        rig.param (kColorHi, toNormalized (kColorHi, 0.25));
        rig.param (kPreLimit, 0.0); // (on by default: the click below turns it back on)
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, tone (110.0, 0.4));
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_smacheratr.png"), "screenshot");

            // the Pre-Limit toggle above the curve
            win.click (Editor::kShaperLeft + 40, 51);
            CHECK (plainOf (rig, kPreLimit) >= 0.5, "pre-limit switched on from the editor");
            CHECK (win.savePng (outDir + "/ui_smacheratr_prelimit.png"), "pre-limit screenshot");

            // Gently (called Clarity before): its band appears in the colour display
            const double gentlyX = Editor::kGentlyButtonX, gentlyY = Editor::kGentlyTop + 40;
            win.click (gentlyX, gentlyY);
            CHECK (plainOf (rig, kClarity) >= 0.5, "Gently switched on from the editor");
            CHECK (win.savePng (outDir + "/ui_smacheratr_gently.png"), "gently screenshot");
            win.click (gentlyX, gentlyY);
            CHECK (plainOf (rig, kClarity) < 0.5, "and off again");
            rig.param (kClarity, 1.0);
            rig.param (kClarity2Range, toNormalized (kClarity2Range, 8.0));
            CHECK (plainOf (rig, kClarity) >= 0.5 && plainOf (rig, kClarity2Range) > 7.9, "both Gently bands on from the host");
            CHECK (win.savePng (outDir + "/ui_smacheratr_gently_host.png"), "gently screenshot, both bands (set by the host)");
            {
                // a window opened with both bands on: its first picture is drawn from scratch
                EditorWindow both (rig.controller);
                CHECK (both.ok () && both.savePng (outDir + "/ui_smacheratr_gently_both.png"), "both bands, fresh window");
            }

            // Advanced: the Threshold sliders at the right of the colour display (with the bands' levels
            // next to them) and the region Drive in the GENTLY panel
            win.click (Editor::kGentlyAdvancedX, gentlyY);
            CHECK (plainOf (rig, kClarityAdvanced) >= 0.5, "Advanced switched on from the editor");
            rig.param (kClarityDrive, 1.0);
            rig.param (kClarityThreshold, toNormalized (kClarityThreshold, -30.0));
            for (int i = 0; i < 10; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, tone (250.0, 0.4));
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_smacheratr_gently_advanced.png"), "gently advanced screenshot");
            {
                // band 1's slider: dragging it up raises its Threshold
                const double sliderX = Editor::kColorLeft + Editor::kColorViewWidth - ThresholdSlider::kStripWidth +
                                       ThresholdSlider::kGap + ThresholdSlider::kWidth / 2;
                const double sliderY = Editor::kColorTop + Editor::kColorViewHeight / 2;
                const double t0 = plainOf (rig, kClarityThreshold);
                win.drag (sliderX, sliderY, sliderX, sliderY - 60);
                CHECK (plainOf (rig, kClarityThreshold) > t0 + 5.0, "drag up raises the Threshold: %.1f -> %.1f", t0,
                       plainOf (rig, kClarityThreshold));
            }
            win.click (Editor::kGentlyAdvancedX, gentlyY);
            CHECK (plainOf (rig, kClarityAdvanced) < 0.5, "and Advanced off again");
            rig.param (kClarityDrive, 0.0);
            rig.param (kClarityThreshold, toNormalized (kClarityThreshold, -18.0));

            // the Sub and High bands have no button: a fresh instance has them at Range 0 (no cut), their
            // handles flat at 0 dB; pulling the High band's handle (7 kHz) down gives it a Range, and it cuts
            CHECK (plainOf (rig, kClaritySubRange) == 0.0 && plainOf (rig, kClarityHighRange) == 0.0, "Sub and High start at Range 0");
            {
                const double gx = Editor::kColorLeft + std::log (7000.0 / 20.0) / std::log (1000.0) * Editor::kColorViewWidth;
                const double gy = Editor::kColorTop + Editor::kColorViewHeight / 2; // (0 dB)
                win.drag (gx, gy, gx, gy + 30);
                pump (0.05);
                CHECK (plainOf (rig, kClarityHighRange) > 2.0 && clarityHighOn (plainOf (rig, kClarity), plainOf (rig, kClarityHighRange)),
                       "dragging the High handle down: Range %.1f dB, the band works", plainOf (rig, kClarityHighRange));
            }

            // the High band (rose, from 7 kHz up) over band 2 (5 kHz, 2 octaves: up to 10 kHz); No Overlap,
            // switched on in the GENTLY panel, splits them at the middle of the overlap
            rig.param (kClarityHighRange, toNormalized (kClarityHighRange, 6.0));
            rig.param (kClarityHighFreq, toNormalized (kClarityHighFreq, 7000.0));
            rig.param (kClarity2Range, toNormalized (kClarity2Range, 8.0));
            rig.param (kClarity2Freq, toNormalized (kClarity2Freq, 5000.0));
            rig.param (kClarity2Width, toNormalized (kClarity2Width, 2.0));
            pump (0.05);
            CHECK (win.savePng (outDir + "/ui_smacheratr_gently_high.png"), "gently high band screenshot");
            auto band2Top = [&] { return plainOf (rig, kClarity2Freq) * std::exp2 (0.5 * plainOf (rig, kClarity2Width)); };
            win.click (Editor::kNoOverlapX, gentlyY);
            pump (0.05);
            CHECK (plainOf (rig, kClarityNoOverlap) >= 0.5, "No Overlap switched on from the editor");
            CHECK (band2Top () <= plainOf (rig, kClarityHighFreq) * 1.001 && plainOf (rig, kClarityHighFreq) > 7000.0,
                   "band 2 and High apart: band 2 up to %.0f Hz, High from %.0f Hz", band2Top (), plainOf (rig, kClarityHighFreq));
            for (uint32_t id : {(uint32_t)kClarityNoOverlap, (uint32_t)kClarityHighRange, (uint32_t)kClarityHighFreq, (uint32_t)kClarity2Freq,
                                (uint32_t)kClarity2Width, (uint32_t)kClarity2Range})
                rig.param (id, defaultNormalized (id));
            rig.param (kClarity, 0.0);
            rig.param (kClarity2Range, 0.0);

            // shaper display: drag down lowers Drive, double-click resets it
            const double sx = Editor::kShaperLeft + Editor::kShaperWidth / 2, sy = Editor::kShaperTop + Editor::kShaperHeight / 2;
            const double d0 = plainOf (rig, kDrive);
            win.drag (sx, sy, sx, sy + 60);
            CHECK (plainOf (rig, kDrive) < d0 - 6.0, "drag down lowers drive: %.1f -> %.1f", d0, plainOf (rig, kDrive));
            win.click (sx, sy, 2);
            CHECK (std::fabs (plainOf (rig, kDrive)) < 1e-6, "double-click resets drive to the default: %.2f", plainOf (rig, kDrive));

            // colour display: the right handle sits at Freq / Amt Hi; dragging it up raises Amt Hi
            auto xOfHz = [] (double hz) {
                return Editor::kColorLeft + std::log (hz / 20.0) / std::log (1000.0) * Editor::kColorViewWidth;
            };
            auto yOfDb = [] (double db) {
                return Editor::kColorTop + Editor::kColorViewHeight / 2 - db / 24.0 * (Editor::kColorViewHeight / 2 - 12.0);
            };
            const double hx = xOfHz (plainOf (rig, kColorFreq)), hy = yOfDb (24.0 * plainOf (rig, kColorHi));
            win.drag (hx, hy, hx, hy - 30);
            CHECK (plainOf (rig, kColorHi) > 0.25 + 0.15, "drag up raises Amt Hi: %.2f", plainOf (rig, kColorHi));
            const double f0 = plainOf (rig, kColorFreq);
            const double hy2 = yOfDb (24.0 * plainOf (rig, kColorHi));
            win.drag (hx, hy2, hx + 40, hy2);
            CHECK (plainOf (rig, kColorFreq) > f0 * 1.5, "sideways drag raises Freq: %.0f -> %.0f", f0, plainOf (rig, kColorFreq));
            const double lx = xOfHz (50.0), ly = yOfDb (24.0 * plainOf (rig, kColorLo));
            win.click (lx, ly, 2);
            CHECK (std::fabs (plainOf (rig, kColorLo)) < 1e-6, "double-click resets Amt Lo: %.2f", plainOf (rig, kColorLo));
        }
        rig.stop ();
        return finish ("smacheratr host test");
    }
}
