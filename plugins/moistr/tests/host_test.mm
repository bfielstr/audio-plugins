// End-to-end test of the built Moistr.vst3. usage: moistr_hosttest <Moistr.vst3> <output dir>
#include "Engine.h"
#include "Params.h"
#include "plugin/Controller.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "pluginkit/ui/BasicView.h"
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
    // the end saturator as a new instance had it up to 0.24 (off, its Gentlr off, 12 / 12): the checks are
    // about moistr's own sound (a new instance has it on: checkNewInstanceGentlr)
    st.norm[kTailBase + pk::kTailOn] = 0.0;
    st.norm[kTailExtBase + pk::kTailExtClarity] = 0.0;
    st.norm[kTailExt3Base + pk::kTailExt3Slope] = 0.0;
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
        CHECK (rig.component->getBusCount (kEvent, Steinberg::Vst::kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets
        checkNewInstanceGentlr (rig.controller, kTailBase + pk::kTailOn, kTailExtBase + pk::kTailExtClarity, kTailExt3Base + pk::kTailExt3Slope);
        // a new instance keeps the defaults (the Ocean sound, the LAB empty); with the Neuro recipe (the LAB on) it reports
        // the LAB's latency with the end saturator's, as the engine has it with those settings
        {
            int differ = 0;
            for (uint32_t id = 0; id < kNumParams; ++id)
                if (id != kTailBase + pk::kTailOn && id != kTailExtBase + pk::kTailExtClarity && id != kTailExt3Base + pk::kTailExt3Slope &&
                    id != kTailExt2Base + pk::kTailExt2Advanced)
                    differ += std::fabs (rig.controller->getParamNormalized (id) - defaultNormalized (id)) > 1e-9;
            CHECK (differ == 0, "a new instance: the defaults, the Ocean sound (%d values differ)", differ);
            State neuro;
            for (uint32_t id = 0; id < kNumParams; ++id)
            {
                neuro.norm[id] = defaultNormalized (id);
                neuro.has[id] = true;
            }
            for (const auto& [id, n] : neuroRecipe ())
                neuro.norm[id] = n;
            CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, neuro); }), "setState: Neuro");
            Engine ref;
            for (const auto& [id, n] : neuroRecipe ())
                ref.setParam (id, toPlain (id, n));
            ref.prepare (48000.0, 512);
            CHECK (rig.start (), "start");
            const uint32 neuroLatency = rig.processor->getLatencySamples ();
            CHECK ((int)neuroLatency == ref.latency () && ref.labLatency () > 0, "Neuro's latency %u: the engine's %d (the LAB's %d)",
                   neuroLatency, ref.latency (), ref.labLatency ());
            rig.stop ();
        }

        State st = baseState ();
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        const uint32 latency = rig.processor->getLatencySamples ();
        CHECK (latency < 200, "latency reported %u (the end saturator's: the LAB empty)", latency);

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

        // the gestures: a user gesture in slot 1 on Mid Level through the state (version 6) plays and comes back
        // with getState; the render moves (a gate) and stays finite and mono
        {
            State g = baseState ();
            g.norm[gestureId (0, kGestureTarget)] = toNormalized (gestureId (0, kGestureTarget), kTargetMidLevel);
            g.norm[gestureId (0, kGestureChoice)] = toNormalized (gestureId (0, kGestureChoice), kUserGesture);
            g.user[0].name = "Host Gate";
            g.user[0].length = 1.0;
            g.user[0].points = {{0.0, 1.0}, {0.5, 1.0}, {0.5, 0.0}, {1.0, 0.0}};
            CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, g); }), "setState with a user gesture");
            std::vector<float> gl, gr;
            rig.render (0.1, gl, nullptr, tone ()); // (the processor takes the state with its next block)
            gl.clear ();
            rig.render (2.0, gl, &gr, tone ());
            CHECK (allFinite (gl) && gl == gr, "a gesture: finite, mono in mono out");
            double lo = 1e9, hi = 0.0;
            for (size_t w = 24000; w + 2400 <= gl.size (); w += 2400)
            {
                const double r = rms (gl, w, w + 2400);
                lo = std::min (lo, r);
                hi = std::max (hi, r);
            }
            CHECK (hi > 3.0 * lo, "the user gate on the Mid band (the 1 kHz tone) moves the level (%.4f .. %.4f RMS)", lo, hi);
            MemoryStream back;
            CHECK (rig.component->getState (&back) == kResultOk, "getState with a user gesture");
            back.seek (0, IBStream::kIBSeekSet, nullptr);
            State b;
            CHECK (readState (&back, b) && b.user[0].name == "Host Gate" && b.user[0].points == g.user[0].points && b.user[1].empty (),
                   "the user gesture saved with the state");
            CHECK (std::lround (plainOf (rig, gestureId (0, kGestureTarget))) == kTargetMidLevel, "the controller has slot 1's Target");
            CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "back to the base state");
        }

        // the one gesture (0.30): a user gesture of two lanes (a gate on Mid Level, Close) through the state (version
        // 7) plays and comes back with getState
        {
            State g = baseState ();
            g.norm[kScene] = toNormalized (kScene, kSceneUser);
            g.scene.name = "Host Scene";
            g.scene.length = 1.0;
            SceneLaneData gate, close;
            gate.target = "Mid Level";
            gate.points = {{0.0, 1.0}, {0.5, 1.0}, {0.5, 0.0}, {1.0, 0.0}};
            close.target = "Close";
            close.hasMin = true;
            close.min = 2000.0;
            close.points = {{0.0, 1.0}, {1.0, 0.5}};
            g.scene.lanes = {gate, close};
            CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, g); }), "setState with a gesture of lanes");
            std::vector<float> gl, gr;
            rig.render (0.1, gl, nullptr, tone ());
            gl.clear ();
            rig.render (2.0, gl, &gr, tone ());
            CHECK (allFinite (gl) && gl == gr, "the gesture: finite, mono in mono out");
            double lo = 1e9, hi = 0.0;
            for (size_t w = 24000; w + 2400 <= gl.size (); w += 2400)
            {
                const double r = rms (gl, w, w + 2400);
                lo = std::min (lo, r);
                hi = std::max (hi, r);
            }
            CHECK (hi > 3.0 * lo, "the gesture's gate on the Mid band moves the level (%.4f .. %.4f RMS)", lo, hi);
            MemoryStream back;
            CHECK (rig.component->getState (&back) == kResultOk, "getState with a gesture of lanes");
            back.seek (0, IBStream::kIBSeekSet, nullptr);
            State b;
            CHECK (readState (&back, b) && b.scene.name == "Host Scene" && b.scene.lanes.size () == 2 && b.scene.lanes[0].points == gate.points,
                   "the gesture saved with the state");
            CHECK (std::lround (plainOf (rig, kScene)) == kSceneUser, "the controller has Gesture User");
            CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "back to the base state");
        }

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
            // the SWEEP stage's switches: Sweep (SWEEP, sweep row 1; on by default) and High Shelf (HIGH SHELF, sweep
            // row 2; off by default since 0.26); in BELLS, bell C picked and switched off and on in the On strip, and
            // the picked bell's Sync (C's, then A's again)
            const double swy = Editor::kSweepRow1 + Editor::kSwitchTop + Editor::kSwitchH / 2;
            win.click (Editor::kSweepLeft + Editor::kSwitchLeft + Editor::kSweepColW / 2, swy);
            pump (0.05);
            CHECK (plainOf (rig, kSweep) < 0.5, "Sweep clicked off");
            win.click (Editor::kSweepLeft + Editor::kSwitchLeft + Editor::kSweepColW / 2, swy);
            pump (0.05);
            CHECK (plainOf (rig, kSweep) >= 0.5, "Sweep clicked on");
            const double shy = Editor::kSweepRow2 + Editor::kSwitchTop + Editor::kSwitchH / 2;
            win.click (Editor::kShelfLeft + Editor::kSwitchLeft + Editor::kSwitchW / 2, shy);
            pump (0.05);
            CHECK (plainOf (rig, kShelf) >= 0.5, "High Shelf clicked on");
            win.click (Editor::kShelfLeft + Editor::kSwitchLeft + Editor::kSwitchW / 2, shy);
            pump (0.05);
            CHECK (plainOf (rig, kShelf) < 0.5, "High Shelf clicked off");
            auto cellX = [] (int b) { return Editor::kBellsLeft + Editor::kSwitchLeft + Editor::kBellCellW * b + Editor::kBellCellW / 2 - 1; };
            const double pickY = Editor::kSweepRow1 + Editor::kBellPickTop + Editor::kBellRowH / 2;
            const double onY = Editor::kSweepRow1 + Editor::kBellOnTop + Editor::kBellRowH / 2;
            const double syncX = Editor::kBellsLeft + Editor::kSwitchLeft + Editor::kBellSyncW / 2;
            const double syncY = Editor::kSweepRow1 + Editor::kBellSyncTop + Editor::kSwitchH / 2;
            win.click (cellX (2), onY);
            pump (0.05);
            CHECK (plainOf (rig, kCOn) < 0.5, "bell C clicked off in the On strip");
            win.click (cellX (2), onY);
            pump (0.05);
            CHECK (plainOf (rig, kCOn) >= 0.5, "bell C clicked on in the On strip");
            win.click (cellX (2), pickY); // (bell C picked: its Sync in the picked bell's place)
            pump (0.05);
            win.click (syncX, syncY);
            pump (0.05);
            CHECK (plainOf (rig, kCSync) >= 0.5 && plainOf (rig, kASync) < 0.5, "bell C's Sync clicked on (A's untouched)");
            win.click (syncX, syncY);
            pump (0.05);
            CHECK (plainOf (rig, kCSync) < 0.5, "bell C's Sync clicked off");
            win.click (cellX (0), pickY);
            pump (0.05);
            win.click (syncX, syncY);
            pump (0.05);
            CHECK (plainOf (rig, kASync) >= 0.5, "bell A's Sync clicked on");
            win.click (syncX, syncY);
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
            // GESTURE (row 4): with a gesture picked, its Mode clicked to Walk and back to Loop (the 0.27 slots untouched)
            rig.param (kScene, toNormalized (kScene, kSceneReeseCell + 1));
            pump (0.05);
            const double modeY = Editor::kRow4 + Editor::kGestureTop + Editor::kGestureRowH / 2;
            const double walkX = Editor::kGesturesLeft + Editor::kGestureColLeft + (Editor::kGestureColRight - Editor::kGestureColLeft) * 3 / 4;
            const double loopX = Editor::kGesturesLeft + Editor::kGestureColLeft + (Editor::kGestureColRight - Editor::kGestureColLeft) / 4;
            win.click (walkX, modeY);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kSceneMode)) == kModeWalk && std::lround (plainOf (rig, gestureId (0, kGestureMode))) == kModeLoop,
                   "the gesture's Mode clicked to Walk (the slots untouched)");
            win.click (loopX, modeY);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kSceneMode)) == kModeLoop, "the gesture's Mode back to Loop");
            rig.param (kScene, toNormalized (kScene, kSceneNone));
            pump (0.05);
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
            // (0.30) Loop Lock in LOOP LOCK and Sub Guard in SUB GUARD (row 6): on and off again, off and on again
            const double ly = Editor::kRow6 + Editor::kSwitchTop + Editor::kSwitchH / 2;
            const double lx = Editor::kLoopLeft + Editor::kSwitchLeft + Editor::kSwitchW / 2, gx = Editor::kGuardLeft + Editor::kSwitchLeft + Editor::kSwitchW / 2;
            const bool lock0 = plainOf (rig, kLoopLock) >= 0.5, guard0 = plainOf (rig, kSubGuard) >= 0.5;
            win.click (lx, ly);
            pump (0.05);
            win.click (gx, ly);
            pump (0.05);
            CHECK ((plainOf (rig, kLoopLock) >= 0.5) != lock0 && (plainOf (rig, kSubGuard) >= 0.5) != guard0, "Loop Lock and Sub Guard clicked");
            win.click (lx, ly);
            win.click (gx, ly);
            pump (0.05);
            CHECK ((plainOf (rig, kLoopLock) >= 0.5) == lock0 && (plainOf (rig, kSubGuard) >= 0.5) == guard0, "and back");

            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, reese ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_moistr.png"), "screenshot");
            // Classic, Wide and Classic again: knobs found and turned in each (Wide's screenshot)
            checkLayouts (rig, win, {(uint32_t)kXoverMid, (uint32_t)kMovement, (uint32_t)kGlue, (uint32_t)kMix, (uint32_t)kIntensity},
                          outDir + "/ui_moistr_wide.png");
        }
        // the Basic page (pluginkit/ui/BasicView.h): Input, Drive, Movement, Loop Lock, Position, Sub Guard, Mix and Output
        // found where it puts them, the rest not; Input turns there; the capture band's buffer has every frame; the
        // Advanced switch shows every control
        {
            EditorWindow win (rig.controller, "default", "Classic", false);
            CHECK (win.ok (), "editor (Basic)");
            CHECK (std::fabs (win.width () - pk::basic::kWidth) < 1, "the Basic page's width: %.0f", win.width ());
            ControlRect r;
            for (uint32_t id : {(uint32_t)moistr::kInput, (uint32_t)kSweepDrive, (uint32_t)kMovement, (uint32_t)kLoopLock, (uint32_t)kLoopPosition,
                                (uint32_t)kSubGuard, (uint32_t)kMix, (uint32_t)moistr::kOutput})
                CHECK (findControl (rig.controller, id, r) && r.right <= win.width () + 0.5 && r.bottom <= win.height () + 0.5,
                       "Basic: the control of parameter %u is shown", id);
            CHECK (!findControl (rig.controller, kXoverMid, r) && !findControl (rig.controller, kParaLpFreq, r), "Basic: the Advanced view's controls are not");
            if (findControl (rig.controller, moistr::kInput, r))
            {
                rig.param (moistr::kInput, toNormalized (moistr::kInput, -12.0));
                win.drag (r.cx (), r.cy (), r.cx (), r.cy () - 30.0);
                pump (0.05);
                CHECK (plainOf (rig, moistr::kInput) > -11.5, "Basic: Input turns (%.1f dB)", plainOf (rig, moistr::kInput));
                rig.param (moistr::kInput, toNormalized (moistr::kInput, 0.0));
            }
            if (auto* c = static_cast<Controller*> (rig.controller.get ()); c->getShared ()) // (static_cast: the plug-in is a bundle here)
            {
                const uint64_t before = c->getShared ()->capture.written ();
                std::vector<float> cap;
                rig.render (0.5, cap, nullptr, reese ());
                CHECK (c->getShared ()->capture.written () - before == cap.size (), "the capture buffer has every frame (%llu of %zu)",
                       (unsigned long long)(c->getShared ()->capture.written () - before), cap.size ());
                pump (0.1);
            }
            CHECK (win.savePng (outDir + "/ui_moistr_basic.png"), "screenshot, Basic");
            const auto hr = pk::basic::headerRight (pk::basic::kWidth);
            win.click (hr.advanced.getCenter ().x, hr.advanced.getCenter ().y);
            pump (0.2);
            CHECK (findControl (rig.controller, kXoverMid, r) && findControl (rig.controller, kParaLpFreq, r), "the Advanced switch: every control");
        }
        rig.stop ();
        return finish ("moistr host test");
    }
}
