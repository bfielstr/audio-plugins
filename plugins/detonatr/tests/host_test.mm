// End-to-end test of the built Detonatr.vst3. usage: detonatr_hosttest <Detonatr.vst3> <output dir>
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/Editor.h"

#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace detonatr;
#define CHECK PK_CHECK

// a noisy hit with a low tone in it, every 0.6 s
static InputFn hits ()
{
    return [] (int ch, int, float* buf, int n, long long pos) {
        static std::mt19937 rng (5);
        std::normal_distribution<float> noise (0.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double)((pos + i) % 28800) / 48000.0;
            const double env = std::exp (-t / 0.18) * std::min (1.0, t / 0.002);
            buf[i] = (float)(env * (0.3 * noise (rng) + 0.4 * std::sin (2 * M_PI * 70.0 * t))) * (ch == 0 ? 1.0f : 0.9f);
        }
    };
}

static double plainOf (Rig& rig, uint32_t id) { return toPlain (id, rig.controller->getParamNormalized (id)); }

// the strip: ten stage boxes and the Smacheratr's, 8 px apart
static double boxW () { return (Editor::kStripRight - Editor::kStripLeft - 8.0 * (int)kNumStages) / ((int)kNumStages + 1); }
static double boxLeft (int pos) { return Editor::kStripLeft + pos * (boxW () + 8.0); }
static double boxX (int pos) { return boxLeft (pos) + boxW () / 2; }
static const double kBoxY = (Editor::kStripTop + Editor::kStripBottom) / 2 + 8;
// a point on the shown page (its own coordinates)
static double pageX (double x) { return Editor::kPageLeft + x; }
static double pageY (double y) { return Editor::kStageTop + y; }

static void settle (Rig& rig, std::vector<float>& out)
{
    for (int i = 0; i < 8; ++i)
    {
        out.clear ();
        rig.render (0.08, out, nullptr, hits ());
        pump (0.03);
    }
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
            return finish ("detonatr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (countNonAutomatable (rig.controller) == 0, "every parameter automatable");
        CHECK (rig.start (), "start");

        // the defaults on hits: sound comes out, finite, under the last limiter's 0 dBTP ceiling
        std::vector<float> out, outR;
        rig.render (3.0, out, &outR, hits ());
        CHECK (allFinite (out) && allFinite (outR), "finite");
        const double level = dbfs (rms (out, out.size () / 3, out.size ()));
        CHECK (level > -40.0 && level < 0.0, "the defaults sound: %.1f dBFS rms", level);
        float pk = 0.0f;
        for (size_t i = 0; i < out.size (); ++i)
            pk = std::max ({pk, std::fabs (out[i]), std::fabs (outR[i])});
        CHECK (pk <= 1.0f, "never over 0 dBFS: peak %.4f", pk);

        // an old Detonatr's state (versions 1 and 2, the old stages): every parameter at its default
        {
            State old;
            for (uint32_t id = 0; id < kNumParams; ++id)
            {
                old.norm[id] = 0.9;
                old.has[id] = true;
            }
            for (int32 version : {1, 2})
            {
                rig.param (kVocBands, 0.0);
                CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, old, version); }), "version %d state applied", version);
                bool defaults = true;
                for (uint32_t id = 0; id < kNumParams; ++id)
                    defaults = defaults && std::fabs (rig.controller->getParamNormalized (id) - defaultNormalized (id)) < 1e-9;
                CHECK (defaults, "version %d: the controller at the defaults", version);
                CHECK (std::lround (plainOf (rig, kVocBands)) == 32, "32 vocoder bands: %ld", std::lround (plainOf (rig, kVocBands)));
            }
        }
        // the state, there and back
        {
            rig.param (kTr1Base + kTrRatio, 0.8);
            rig.param (kMotOrbs, toNormalized (kMotOrbs, 11.0));
            MemoryStream saved;
            CHECK (rig.component->getState (&saved) == kResultOk, "getState");
            saved.seek (0, IBStream::kIBSeekSet, nullptr);
            State back;
            CHECK (readState (&saved, back), "readState");
            CHECK (std::fabs (back.norm[kTr1Base + kTrRatio] - 0.8) < 1e-12 && std::lround (toPlain (kMotOrbs, back.norm[kMotOrbs])) == 11,
                   "the parameters saved");
            State fresh;
            for (uint32_t id = 0; id < kNumParams; ++id)
            {
                fresh.norm[id] = defaultNormalized (id);
                fresh.has[id] = true;
            }
            rig.applyState ([&] (IBStream* s) { return writeState (s, fresh); });
        }

        // the editor: every page, from clicking its box in the strip (the ten stages, then the Smacheratr)
        const char* pages[kNumStages + 1] = {"vocoder", "spike", "motion", "transient1", "limiter1", "transient2",
                                             "comp1",   "comp2", "tape",   "limiter2",   "smacheratr"};
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            for (int pos = 0; pos <= kNumStages; ++pos)
            {
                win.click (boxX (pos), kBoxY);
                settle (rig, out);
                CHECK (win.savePng (outDir + "/ui_detonatr_" + pages[pos] + ".png"), "screenshot %s", pages[pos]);
            }
            // a page's controls: Limiter 1's True Peak (5th) and Comp 1's Auto Threshold (7th)
            win.click (boxX (kStageLimiter1), kBoxY);
            win.click (pageX (120), pageY (32));
            CHECK (plainOf (rig, kLim1Base + kLimTruePeak) < 0.5, "Limiter 1's True Peak off");
            win.click (pageX (120), pageY (32));
            CHECK (plainOf (rig, kLim1Base + kLimTruePeak) > 0.5, "and on again");
            win.click (boxX (kStageComp1), kBoxY);
            win.click (pageX (110), pageY (32));
            CHECK (plainOf (rig, kComp1Base + kCompAutoThreshold) < 0.5, "Comp 1's Auto Threshold off");
            // a knob: the Vocoder's Bands (its first knob), dragged up
            win.click (boxX (kStageVocoder), kBoxY);
            const double bands0 = plainOf (rig, kVocBands);
            win.drag (pageX (Editor::kKnobX + 28), pageY (Editor::kKnobY + 30), pageX (Editor::kKnobX + 28), pageY (Editor::kKnobY - 10));
            CHECK (plainOf (rig, kVocBands) > bands0, "Bands dragged up: %.0f -> %.0f", bands0, plainOf (rig, kVocBands));
            // the Motion page with more orbs, in the screenshot
            rig.param (kMotOrbs, toNormalized (kMotOrbs, 12.0));
            win.click (boxX (kStageMotion), kBoxY);
            settle (rig, out);
            CHECK (allFinite (out), "finite with twelve orbs");
            CHECK (win.savePng (outDir + "/ui_detonatr_motion12.png"), "screenshot Motion with twelve orbs");
            // drag Tape (9th) to the front
            win.drag (boxX (kStageTape), kBoxY, boxX (0) - 20, kBoxY);
            pump (0.1);
            CHECK (std::lround (plainOf (rig, kOrderBase)) == kStageTape, "dragged to the front: stage 1 is %ld",
                   std::lround (plainOf (rig, kOrderBase)));
            CHECK (std::lround (plainOf (rig, kOrderBase + 1)) == kStageVocoder, "the others move along: stage 2 is %ld",
                   std::lround (plainOf (rig, kOrderBase + 1)));
            // the light turns a stage off (the Vocoder, now 2nd), and the Smacheratr on
            win.click (boxLeft (1) + 12, Editor::kStripTop + 12);
            win.click (boxLeft (kNumStages) + 12, Editor::kStripTop + 12);
            pump (0.1);
            CHECK (plainOf (rig, kVocOn) < 0.5, "the light turned the Vocoder off");
            CHECK (plainOf (rig, kTailBase + pk::kTailOn) > 0.5, "the Smacheratr's light turned it on");
            // the Smacheratr's box does not move
            win.drag (boxX (kNumStages), kBoxY, boxX (0), kBoxY);
            pump (0.1);
            CHECK (std::lround (plainOf (rig, kOrderBase)) == kStageTape, "the Smacheratr stays at the end");
            settle (rig, out);
            CHECK (allFinite (out), "finite reordered, with the Smacheratr on");
            CHECK (win.savePng (outDir + "/ui_detonatr_reordered.png"), "screenshot reordered");
        }
        // a fresh window (the first capture of a window is the reliable one)
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor again");
            win.click (boxX (7), kBoxY); // Comp 1, 8th since Tape moved to the front
            settle (rig, out);
            CHECK (win.savePng (outDir + "/ui_detonatr.png"), "screenshot");
        }
        return finish ("detonatr host test");
    }
}
