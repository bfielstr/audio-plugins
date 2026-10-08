// End-to-end test of the built Moistr.vst3. usage: moistr_hosttest <Moistr.vst3> <output dir>
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
using namespace moistr;
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

// a 1 kHz sine at -12 dBFS, the same on both channels
static InputFn tone ()
{
    return [] (int, int, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
            buf[i] = (float)(0.25 * std::sin (2 * M_PI * 1000.0 * (double)(pos + i) / 48000.0));
    };
}

// a detuned saw pair at 55 Hz (a plain Reese), the same on both channels
static InputFn reese ()
{
    return [] (int, int, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
        {
            const double t = (double)(pos + i) / 48000.0;
            const double a = 55.0 * t, b = 55.0 * 1.012 * t + 0.3;
            buf[i] = (float)(0.25 * ((2.0 * (a - std::floor (a)) - 1.0) + (2.0 * (b - std::floor (b)) - 1.0)));
        }
    };
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
            return finish ("moistr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (rig.component->getBusCount (kEvent, kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets

        State st = baseState ();
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        const uint32 latency = rig.processor->getLatencySamples ();
        CHECK (latency < 200, "latency reported %u (the end saturator's)", latency);

        // Mix 0 %: the tone passes at its level
        rig.param (kMix, 0.0);
        std::vector<float> out, outR;
        rig.render (1.0, out, &outR, tone ());
        const size_t a = 24000, b = 48000;
        const double dry = toneDb (out, 1000.0, a, b);
        CHECK (std::fabs (dry + 12.04) < 0.1, "Mix 0 %%: 1 kHz at %.2f dB", dry);
        CHECK (allFinite (out), "finite");

        // the defaults on a Reese: about as loud, different from the input, finite
        rig.param (kMix, 1.0);
        out.clear ();
        outR.clear ();
        rig.render (2.0, out, &outR, reese ());
        const double level = dbfs (rms (out, 48000, out.size ()));
        CHECK (level > -30.0 && level < -3.0, "the defaults on a Reese: %.1f dBFS", level);
        CHECK (allFinite (out) && allFinite (outR), "finite");

        // the movement follows the song position: playing the same stretch twice renders it the same
        auto fromStart = [&] {
            rig.stop ();
            rig.position = 0;
            rig.ctx.projectTimeMusic = 0.0;
            rig.start ();
            std::vector<float> o;
            rig.render (1.5, o, nullptr, reese ());
            return o;
        };
        rig.param (kMovement, 1.0);
        rig.render (0.05, out, nullptr, reese ()); // (the processor takes the change with its next block)
        const auto first = fromStart (), second = fromStart ();
        CHECK (first == second, "the same song position renders the same");

        // state round trip
        rig.param (kSeed, toNormalized (kSeed, 42.0));
        rig.param (kBandCount, toNormalized (kBandCount, kBands4));
        rig.render (0.05, out, nullptr, reese ());
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::lround (toPlain (kSeed, back.norm[kSeed])) == 42, "Seed saved");
        CHECK (std::fabs (toPlain (kMovement, back.norm[kMovement]) - 1.0) < 1e-9, "Movement saved");
        CHECK (std::lround (toPlain (kBandCount, back.norm[kBandCount])) == kBands4, "Bands saved");
        rig.param (kBandCount, toNormalized (kBandCount, kBands3));

        // editor (Classic): the Bands, Passes and Sync switches, double-clicks on Mid X and Depth, then a
        // screenshot while the bands move
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            ViewRect r;
            CHECK (win.view () && win.view ()->getSize (&r) == kResultOk && r.getWidth () == (int32)Editor::kWidth &&
                       r.getHeight () == (int32)(Editor::kHeight + pk::EditorBase::kInfoHeight),
                   "editor size %d x %d", r.getWidth (), r.getHeight ());
            pump (0.1);
            // Bands in SPLIT: 4 Bands, then back to 3
            const double sy = Editor::kRow1 + Editor::kSwitchTop + Editor::kSwitchH / 2;
            win.click (Editor::kSplitLeft + Editor::kSwitchLeft + Editor::kSwitchW * 3 / 4, sy);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kBandCount)) == kBands4, "4 Bands clicked: %.0f", plainOf (rig, kBandCount));
            win.click (Editor::kSplitLeft + Editor::kSwitchLeft + Editor::kSwitchW / 4, sy);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kBandCount)) == kBands3, "3 Bands clicked: %.0f", plainOf (rig, kBandCount));
            // Passes in GLUE (row 1)
            win.click (Editor::kGlueLeft + Editor::kSwitchLeft + Editor::kSwitchW * 3 / 4, sy);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kPasses)) == kPasses2, "2 Passes clicked: %.0f", plainOf (rig, kPasses));
            win.click (Editor::kGlueLeft + Editor::kSwitchLeft + Editor::kSwitchW / 4, sy);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kPasses)) == kPasses1, "1 Pass clicked: %.0f", plainOf (rig, kPasses));
            // the shifter's On switch in SHIFT (row 1, a knob wide in the first knob's place)
            win.click (Editor::kShiftLeft + Editor::kKnobLeft + Editor::kShiftSwitchW / 2, sy);
            pump (0.05);
            CHECK (plainOf (rig, kShiftOn) >= 0.5, "Shift clicked on");
            win.click (Editor::kShiftLeft + Editor::kKnobLeft + Editor::kShiftSwitchW / 2, sy);
            pump (0.05);
            CHECK (plainOf (rig, kShiftOn) < 0.5, "Shift clicked off");
            // the SWEEP stage's switches: Sweep (SWEEP, sweep row 1) and High Shelf (HIGH SHELF, sweep row 2), both on
            // by default; and bell A's Sync (its compact column)
            const double swy = Editor::kSweepRow1 + Editor::kSwitchTop + Editor::kSwitchH / 2;
            win.click (Editor::kSweepLeft + Editor::kSwitchLeft + Editor::kSwitchW / 2, swy);
            pump (0.05);
            CHECK (plainOf (rig, kSweep) < 0.5, "Sweep clicked off");
            win.click (Editor::kSweepLeft + Editor::kSwitchLeft + Editor::kSwitchW / 2, swy);
            pump (0.05);
            CHECK (plainOf (rig, kSweep) >= 0.5, "Sweep clicked on");
            const double shy = Editor::kSweepRow2 + Editor::kSwitchTop + Editor::kSwitchH / 2;
            win.click (Editor::kShelfLeft + Editor::kSwitchLeft + Editor::kSwitchW / 2, shy);
            pump (0.05);
            CHECK (plainOf (rig, kShelf) < 0.5, "High Shelf clicked off");
            win.click (Editor::kShelfLeft + Editor::kSwitchLeft + Editor::kSwitchW / 2, shy);
            pump (0.05);
            CHECK (plainOf (rig, kShelf) >= 0.5, "High Shelf clicked on");
            win.click (Editor::kBellALeft + Editor::kSwitchLeft + Editor::kBellColW / 2, swy);
            pump (0.05);
            CHECK (plainOf (rig, kASync) >= 0.5, "bell A's Sync clicked on");
            win.click (Editor::kBellALeft + Editor::kSwitchLeft + Editor::kBellColW / 2, swy);
            pump (0.05);
            CHECK (plainOf (rig, kASync) < 0.5, "bell A's Sync clicked off");
            // the Sync switch in MOVEMENT (row 2)
            const double py = Editor::kRow2 + Editor::kSwitchTop + Editor::kSwitchH / 2;
            win.click (Editor::kMoveLeft + Editor::kSwitchLeft + Editor::kSwitchW / 2, py);
            pump (0.05);
            CHECK (plainOf (rig, kSync) >= 0.5, "Sync clicked on");
            win.click (Editor::kMoveLeft + Editor::kSwitchLeft + Editor::kSwitchW / 2, py);
            pump (0.05);
            CHECK (plainOf (rig, kSync) < 0.5, "Sync clicked off");
            // the Drop Out switch in EXTREME (row 3, centred with its two knobs)
            const double ex = Editor::kExtremeLeft + Editor::centredSwitchLeft (Editor::kExtremeRight - Editor::kExtremeLeft, 2) +
                              Editor::kSwitchW / 2;
            const double ey = Editor::kRow3 + Editor::kSwitchTop + Editor::kSwitchH / 2;
            win.click (ex, ey);
            pump (0.05);
            CHECK (plainOf (rig, kDropOut) >= 0.5, "Drop Out clicked on");
            win.click (ex, ey);
            pump (0.05);
            CHECK (plainOf (rig, kDropOut) < 0.5, "Drop Out clicked off");
            // Mid X's knob (the second beside SPLIT's switch): a double-click puts it back to its default
            const double midXDefault = toPlain (kXoverMid, defaultNormalized (kXoverMid));
            rig.param (kXoverMid, toNormalized (kXoverMid, 3000.0));
            pump (0.05);
            const double kx = Editor::kSplitLeft + Editor::kKnobBeside + Editor::kKnobStep + Editor::kKnobW / 2;
            const double ky1 = Editor::kRow1 + Editor::kKnobTop + Editor::kKnobH / 2;
            win.click (kx, ky1, 2);
            pump (0.05);
            CHECK (std::fabs (plainOf (rig, kXoverMid) - midXDefault) < 1.0, "Mid X back to %.0f Hz: %.0f", midXDefault,
                   plainOf (rig, kXoverMid));
            // Depth (the third knob of RISE / FALL in row 2, centred in its panel): the same
            const double depthDefault = toPlain (kDepth, defaultNormalized (kDepth));
            rig.param (kDepth, toNormalized (kDepth, 6.0));
            pump (0.05);
            const double dx = Editor::kShapeLeft + Editor::centredLeft (Editor::kShapeRight - Editor::kShapeLeft, 3) +
                              2 * Editor::kKnobStep + Editor::kKnobW / 2;
            win.click (dx, Editor::kRow2 + Editor::kKnobTop + Editor::kKnobH / 2, 2);
            pump (0.05);
            CHECK (std::fabs (plainOf (rig, kDepth) - depthDefault) < 1e-6, "Depth back to %.0f dB: %.2f", depthDefault,
                   plainOf (rig, kDepth));

            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, reese ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_moistr.png"), "screenshot");
            // Classic, Wide and Classic again: knobs found and turned in each (Wide's screenshot)
            checkLayouts (rig, win, {(uint32_t)kXoverMid, (uint32_t)kMovement, (uint32_t)kGlue, (uint32_t)kMix},
                          outDir + "/ui_moistr_wide.png");
        }
        rig.stop ();
        return finish ("moistr host test");
    }
}
