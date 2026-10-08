// End-to-end test of the built Smacheratr.vst3. usage: smacheratr_hosttest <Smacheratr.vst3> <output dir>
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/Editor.h"
#include "ui/ThresholdSlider.h"

#include "public.sdk/source/common/memorystream.h"

#import <Foundation/Foundation.h>

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace smacheratr;
#define CHECK PK_CHECK

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

static State baseState ()
{
    State st;
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        st.norm[id] = defaultNormalized (id);
        st.has[id] = true;
    }
    // Gentlr as a new instance had it up to 0.24 (off, 12 / 12): the checks are about the curve and the
    // pre-limiter (a new instance has it on: checkNewInstanceGentlr)
    st.norm[kClarity] = 0.0;
    st.norm[kClaritySlope] = 0.0;
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
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets
        checkNewInstanceGentlr (rig.controller, -1, kClarity, kClaritySlope);

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

        // Oversampling: Off and 2x report less latency once the processor has the setting (4x, the default,
        // the most), and the output is the input that much later
        for (int mode : {(int)kOsOff, (int)kOs2x, (int)kOs4x})
        {
            rig.param (kOversampling, toNormalized (kOversampling, mode));
            std::vector<float> o;
            rig.render (0.05, o, nullptr, tone (1000.0, 0.2)); // (the setting arrives with this block)
            const uint32 lat = rig.processor->getLatencySamples ();
            CHECK (mode == kOs4x ? lat == latency : lat < latency, "Oversampling %d: latency %u (4x %u)", mode, lat, latency);
            o.clear ();
            long long first = -1; // (where this render starts: the input's position)
            rig.render (1.0, o, nullptr, [&first] (int b, int c, float* buf, int n, long long pos) {
                if (first < 0)
                    first = pos;
                tone (1000.0, 0.2) (b, c, buf, n, pos);
            });
            double e = 0.0;
            for (size_t i = 4800; i + lat < 48000; ++i)
                e = std::max (e, std::fabs (o[i + lat] - 0.2 * std::sin (2 * M_PI * 1000.0 * (double)(first + (long long)i) / 48000.0)));
            CHECK (e < 2e-3, "Oversampling %d: the input %u samples later (error %g)", mode, lat, e);
        }

        // drive through the plug-in: harmonics appear, the curve holds the peak at 0 dB
        rig.param (kDrive, toNormalized (kDrive, 18.0));
        out.clear ();
        rig.render (1.0, out, nullptr, tone (1000.0, 0.25));
        const size_t a = 24000, b = 48000;
        CHECK (toneDb (out, 3000.0, a, b) > -30.0, "third harmonic %.1f dB", toneDb (out, 3000.0, a, b));
        {
            // the curve holds 1.0; the 4x oversampling's downsampling filter rings a little past it
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
            CHECK (layoutFindings () == 0, "the layout check finds nothing overlapping, touching or spilling (printed above)");
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

            // Gentlr (called Clarity before): its band appears in the colour display, with Gentlr's layer in front
            // (the Color | Gentlr switch above the display: Gentlr by default; Color has it faint behind)
            const double layerY = Editor::kLayerTop + 9.0;
            const double colorLayerX = Editor::kLayerLeft + Editor::kLayerW * 0.25, gentlrLayerX = Editor::kLayerLeft + Editor::kLayerW * 0.75;
            const double gentlrX = Editor::kGentlrButtonX, gentlrY = Editor::kGentlrTop + 40;
            {
                auto* ctl = static_cast<pk::ControllerBase*> (rig.controller.get ()); // (always a ControllerBase here; dynamic_cast needs typeinfo the macOS link does not export)
                CHECK (ctl && ctl->uiColorLayer == 1, "a new instance: Gentlr's layer in front");
            }
            win.click (gentlrX, gentlrY);
            CHECK (plainOf (rig, kClarity) >= 0.5, "Gentlr switched on from the editor");
            win.click (gentlrLayerX, layerY);
            CHECK (win.savePng (outDir + "/ui_smacheratr_gentlr.png"), "gentlr screenshot");
            win.click (gentlrX, gentlrY);
            CHECK (plainOf (rig, kClarity) < 0.5, "and off again");
            rig.param (kClarity, 1.0);
            rig.param (kClarity2Range, toNormalized (kClarity2Range, 8.0));
            CHECK (plainOf (rig, kClarity) >= 0.5 && plainOf (rig, kClarity2Range) > 7.9, "both Gentlr bands on from the host");
            CHECK (win.savePng (outDir + "/ui_smacheratr_gentlr_host.png"), "gentlr screenshot, both bands (set by the host)");
            {
                // a window opened with both bands on: its first picture is drawn from scratch
                EditorWindow both (rig.controller);
                CHECK (both.ok () && both.savePng (outDir + "/ui_smacheratr_gentlr_both.png"), "both bands, fresh window");
            }

            // Advanced: the Threshold sliders at the right of the colour display (with the bands' levels
            // next to them) and the region Drive in the GENTLR panel
            win.click (Editor::kGentlrAdvancedX, gentlrY);
            CHECK (plainOf (rig, kClarityAdvanced) >= 0.5, "Advanced switched on from the editor");
            rig.param (kClarityDrive, 1.0);
            rig.param (kClarityThreshold, toNormalized (kClarityThreshold, -30.0));
            for (int i = 0; i < 10; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, tone (250.0, 0.4));
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_smacheratr_gentlr_advanced.png"), "gentlr advanced screenshot");
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
            win.click (Editor::kGentlrAdvancedX, gentlrY);
            CHECK (plainOf (rig, kClarityAdvanced) < 0.5, "and Advanced off again");
            rig.param (kClarityDrive, 0.0);
            rig.param (kClarityThreshold, toNormalized (kClarityThreshold, -18.0));

            // the Sub and High bands have no button: a fresh instance has them at Range 0 (no cut), their
            // handles flat at 0 dB; pulling the High band's handle (7 kHz) down gives it a Range, and it cuts.
            // With Color in front Gentlr's handles cannot be grabbed: the same drag does nothing
            CHECK (plainOf (rig, kClaritySubRange) == 0.0 && plainOf (rig, kClarityHighRange) == 0.0, "Sub and High start at Range 0");
            {
                const double gx = Editor::kColorLeft + std::log (7000.0 / 20.0) / std::log (1000.0) * Editor::kColorViewWidth;
                const double gy = Editor::kColorTop + Editor::kColorViewHeight / 2; // (0 dB)
                win.click (colorLayerX, layerY);
                win.drag (gx, gy, gx, gy + 30);
                pump (0.05);
                CHECK (plainOf (rig, kClarityHighRange) == 0.0, "Color in front: Gentlr's handles stay put");
                win.click (gentlrLayerX, layerY);
                win.drag (gx, gy, gx, gy + 30);
                pump (0.05);
                CHECK (plainOf (rig, kClarityHighRange) > 2.0 && clarityHighOn (plainOf (rig, kClarity), plainOf (rig, kClarityHighRange)),
                       "dragging the High handle down: Range %.1f dB, the band works", plainOf (rig, kClarityHighRange));
            }

            // the High band (rose, from 7 kHz up) over band 2 (5 kHz, 2 octaves: up to 10 kHz); No Overlap,
            // switched on in the GENTLR panel, splits them at the middle of the overlap
            rig.param (kClarityHighRange, toNormalized (kClarityHighRange, 6.0));
            rig.param (kClarityHighFreq, toNormalized (kClarityHighFreq, 7000.0));
            rig.param (kClarity2Range, toNormalized (kClarity2Range, 8.0));
            rig.param (kClarity2Freq, toNormalized (kClarity2Freq, 5000.0));
            rig.param (kClarity2Width, toNormalized (kClarity2Width, 2.0));
            pump (0.05);
            CHECK (win.savePng (outDir + "/ui_smacheratr_gentlr_high.png"), "gentlr high band screenshot");
            auto band2Top = [&] { return plainOf (rig, kClarity2Freq) * std::exp2 (0.5 * plainOf (rig, kClarity2Width)); };
            win.click (Editor::kNoOverlapX, Editor::kNoOverlapY);
            pump (0.05);
            CHECK (plainOf (rig, kClarityNoOverlap) >= 0.5, "No Overlap switched on from the editor");
            CHECK (band2Top () <= plainOf (rig, kClarityHighFreq) * 1.001 && plainOf (rig, kClarityHighFreq) > 7000.0,
                   "band 2 and High apart: band 2 up to %.0f Hz, High from %.0f Hz", band2Top (), plainOf (rig, kClarityHighFreq));
            for (uint32_t id : {(uint32_t)kClarityNoOverlap, (uint32_t)kClarityHighRange, (uint32_t)kClarityHighFreq, (uint32_t)kClarity2Freq,
                                (uint32_t)kClarity2Width, (uint32_t)kClarity2Range})
                rig.param (id, defaultNormalized (id));

            // glue on touch in the colour display: band 1 (250 Hz, 2 octaves: 125 - 500 Hz) and band 2 at 1 kHz,
            // an octave wide (707 - 1414 Hz). Band 1's high edge dragged to 3 px short of band 2's low edge
            // snaps onto it and glues them when the drag ends; the link icon on the border detaches them
            {
                auto gx = [] (double hz) { return Editor::kColorLeft + std::log (hz / 20.0) / std::log (1000.0) * Editor::kColorViewWidth; };
                const double gy = Editor::kColorTop + Editor::kColorViewHeight / 2 + 0.75 * (Editor::kColorViewHeight / 2 - 12.0); // (-18 dB)
                const double linkY = Editor::kColorTop + Editor::kColorViewHeight - 26.0;
                rig.param (kClarity2Range, toNormalized (kClarity2Range, 6.0));
                rig.param (kClarity2Freq, toNormalized (kClarity2Freq, 1000.0));
                rig.param (kClarity2Width, toNormalized (kClarity2Width, 1.0));
                pump (0.05);
                auto edge1 = [&] { return plainOf (rig, kClarityFreq) * std::exp2 (0.5 * plainOf (rig, kClarityWidth)); };
                auto edge2 = [&] { return plainOf (rig, kClarity2Freq) / std::exp2 (0.5 * plainOf (rig, kClarity2Width)); };
                CHECK (plainOf (rig, kClarityGlue12) < 0.5, "nothing glued by default");
                win.drag (gx (edge1 ()), gy, gx (edge2 ()) - 3.0, gy);
                pump (0.05);
                CHECK (plainOf (rig, kClarityGlue12) >= 0.5, "band 1's edge dragged onto band 2's: glued");
                CHECK (std::fabs (std::log2 (edge1 () / edge2 ())) < 1e-3, "and touching: %.1f / %.1f Hz", edge1 (), edge2 ());
                CHECK (win.savePng (outDir + "/ui_smacheratr_glue.png"), "glue screenshot");
                const double f1 = plainOf (rig, kClarityFreq), f2 = plainOf (rig, kClarity2Freq);
                win.click (gx (edge1 ()), linkY);
                pump (0.05);
                CHECK (plainOf (rig, kClarityGlue12) < 0.5 && plainOf (rig, kClarityFreq) == f1 && plainOf (rig, kClarity2Freq) == f2,
                       "the link icon clicked: detached, the bands where they were");
                for (uint32_t id : {(uint32_t)kClarityFreq, (uint32_t)kClarityWidth, (uint32_t)kClarity2Freq, (uint32_t)kClarity2Width})
                    rig.param (id, defaultNormalized (id));
            }
            rig.param (kClarity, 0.0);
            rig.param (kClarity2Range, 0.0);

            // band 2's Threshold slider (Advanced) grabbed with Color in front: Gentlr's layer comes to the front and
            // band 2 is selected (its knobs under the display)
            {
                rig.param (kClarity, 1.0);
                rig.param (kClarityAdvanced, 1.0);
                pump (0.05);
                win.click (colorLayerX, layerY);
                const double s2x = Editor::kColorLeft + Editor::kColorViewWidth - ThresholdSlider::kStripWidth + ThresholdSlider::kGap +
                                   ThresholdSlider::kWidth * 1.5 + ThresholdSlider::kGap;
                win.click (s2x, Editor::kColorTop + Editor::kColorViewHeight / 2);
                pump (0.05);
                CHECK (win.savePng (outDir + "/ui_smacheratr_band_select.png"), "band 2 selected by its Threshold slider");
                // its Range knob (the third under the display) turned up with the mouse: band 2's Range moves
                const double r0 = plainOf (rig, kClarity2Range);
                const double kx = Editor::kLayerKnobsLeft + 2 * Editor::kLayerKnobStep + 28.0, ky = Editor::kLayerKnobsTop + 34.0;
                win.drag (kx, ky, kx, ky - 40);
                CHECK (plainOf (rig, kClarity2Range) > r0 + 1.0, "band 2's Range knob shown and turned: %.1f -> %.1f dB", r0,
                       plainOf (rig, kClarity2Range));
                rig.param (kClarity2Range, 0.0);
                rig.param (kClarityAdvanced, 0.0);
                rig.param (kClarity, 0.0);
                win.click (colorLayerX, layerY);
            }

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
