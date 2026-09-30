// End-to-end test of the built Detonatr.vst3. usage: detonatr_hosttest <Detonatr.vst3> <output dir>
#include "Params.h"
#include "multidyn/src/core/Crossover.h"
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

// the centre of the stage box at a position in the strip
static double boxW () { return (Editor::kStripRight - 8.0 - 18.0 * (kNumStages - 1)) / kNumStages; }
static double boxLeft (int pos) { return 8.0 + pos * (boxW () + 18.0); }
static double boxX (int pos) { return boxLeft (pos) + boxW () / 2; }
static const double kBoxY = (Editor::kStripTop + Editor::kStripBottom) / 2 + 8;

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

        // the defaults on hits: sound comes out, finite and sane
        std::vector<float> out;
        rig.render (2.0, out, nullptr, hits ());
        CHECK (allFinite (out), "finite");
        const double level = dbfs (rms (out, 0, out.size ()));
        CHECK (level > -40.0 && level < 6.0, "the defaults sound: %.1f dBFS rms", level);

        // an old state (version 1): the Multiband stage's gains move to where they keep its sound (Live's
        // OTT gain staging is baked in since version 2); the new Multiband parameters are where it was
        {
            State old;
            for (uint32_t id = 0; id < kNumParams; ++id)
            {
                old.norm[id] = defaultNormalized (id);
                old.has[id] = id < kMb2Base; // saved before the second Multiband block
            }
            auto roundTrip = [&] (int32 version) {
                MemoryStream s;
                writeState (&s, old, version);
                s.seek (0, IBStream::kIBSeekSet, nullptr);
                State got;
                readState (&s, got);
                return got;
            };
            const State v1 = roundTrip (1), v2 = roundTrip (kStateVersion);
            auto plain = [] (const State& st, uint32_t id) { return toPlain (id, st.norm[id]); };
            const uint32_t lowOut = mbParam (multidyn::bandParam (0, multidyn::kBandOutput));
            CHECK (std::fabs (plain (v1, lowOut) - 13.7) < 1e-6 && std::fabs (plain (v1, mbParam (multidyn::kOutput)) + 7.0) < 1e-6 &&
                       std::fabs (plain (v1, mbParam (multidyn::bandParam (1, multidyn::kBandInput))) - 5.2) < 1e-6,
                   "version 1 migrated: low band Output %.2f dB", plain (v1, lowOut));
            CHECK (std::fabs (plain (v2, lowOut)) < 1e-9, "version 2 loads as it is");
            CHECK (std::lround (plain (v1, mbParam (multidyn::kXoverSlope))) == multidyn::kXover24 && plain (v1, mbParam (multidyn::kSubOn)) < 0.5 &&
                       plain (v1, mbParam (multidyn::kSoftenColor)) < 0.5,
                   "the new Multiband parameters where an old project was");
            CHECK (std::fabs (plain (v1, detonatr::kOutput) - plain (old, detonatr::kOutput)) < 1e-12, "Detonatr's own Output untouched");
            CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, old, 1); }), "old state applied");
            CHECK (std::fabs (plainOf (rig, lowOut) - 13.7) < 1e-6, "controller: low band Output %.2f", plainOf (rig, lowOut));
            State fresh;
            for (uint32_t id = 0; id < kNumParams; ++id)
            {
                fresh.norm[id] = defaultNormalized (id);
                fresh.has[id] = true;
            }
            rig.applyState ([&] (IBStream* s) { return writeState (s, fresh); });
        }

        // a recording in slot 1, set through the state; it comes back out of the state
        {
            State st;
            for (uint32_t id = 0; id < kNumParams; ++id)
            {
                st.norm[id] = defaultNormalized (id);
                st.has[id] = true;
            }
            auto c = std::make_shared<Carrier> ();
            c->frames = 24000;
            c->sampleRate = 48000.0;
            for (int ch = 0; ch < 2; ++ch)
            {
                c->ch[ch].resize (24000);
                for (int i = 0; i < 24000; ++i)
                    c->ch[ch][(size_t)i] = (float)(0.5 * std::sin (2 * M_PI * 440.0 * i / 48000.0) * (ch == 0 ? 1.0 : 0.8));
            }
            st.recordings[0] = {c, "pot.wav"};
            MemoryStream in;
            CHECK (writeState (&in, st), "writeState");
            in.seek (0, IBStream::kIBSeekSet, nullptr);
            CHECK (rig.component->setState (&in) == kResultOk, "setState with a recording");
            out.clear ();
            rig.render (0.5, out, nullptr, hits ());
            CHECK (allFinite (out), "finite with a recording");
            MemoryStream saved;
            CHECK (rig.component->getState (&saved) == kResultOk, "getState");
            saved.seek (0, IBStream::kIBSeekSet, nullptr);
            State back;
            CHECK (readState (&saved, back), "readState");
            const auto& r = back.recordings[0];
            CHECK (back.hasRecordings && r.audio && r.audio->frames == 24000 && r.name == "pot.wav", "the recording saved");
            if (r.audio && r.audio->frames == 24000)
            {
                double err = 0.0;
                for (int i = 0; i < 24000; ++i)
                    err = std::max (err, (double)std::fabs (r.audio->ch[1][(size_t)i] - c->ch[1][(size_t)i]));
                CHECK (err < 1e-3, "its audio kept (16 bits): %.2g", err);
            }
            CHECK (!back.recordings[1].audio, "the other slots empty");
        }

        // the editor: every stage's page, from clicking its box in the strip
        const char* pages[kNumStages] = {"clean", "tone", "multiband", "transient", "saturator"};
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            for (int pos = 0; pos < kNumStages; ++pos)
            {
                win.click (boxX (pos), kBoxY);
                for (int i = 0; i < 8; ++i)
                {
                    out.clear ();
                    rig.render (0.08, out, nullptr, hits ());
                    pump (0.03);
                }
                CHECK (win.savePng (outDir + "/ui_detonatr_" + pages[pos] + ".png"), "screenshot %s", pages[pos]);
            }
            // the Multiband page's new controls: the Sub band's On (its lane appears) and Soften's Color;
            // the Slope (a menu) set here, shown in the screenshot
            {
                win.click (boxX (2), kBoxY); // Multiband, 3rd
                const double px = 8.0, py = Editor::kStageTop; // the page's origin
                win.click (px + 632, py + 233);
                CHECK (plainOf (rig, mbParam (multidyn::kSubOn)) > 0.5, "Multiband Sub band on");
                win.click (px + 804, py + 211);
                CHECK (plainOf (rig, mbParam (multidyn::kSoftenColor)) > 0.5, "Multiband Soften Color on");
                const double f0 = plainOf (rig, mbParam (multidyn::kSubFreq));
                win.drag (px + 690, py + 233, px + 690, py + 193);
                CHECK (plainOf (rig, mbParam (multidyn::kSubFreq)) > f0 * 1.1, "Multiband Sub frequency drag: %.1f -> %.1f Hz", f0,
                       plainOf (rig, mbParam (multidyn::kSubFreq)));
                rig.param (mbParam (multidyn::kXoverSlope), detonatr::toNormalized (mbParam (multidyn::kXoverSlope), multidyn::kXover48));
                for (int i = 0; i < 8; ++i)
                {
                    out.clear ();
                    rig.render (0.08, out, nullptr, hits ());
                    pump (0.03);
                }
                CHECK (allFinite (out), "finite with the Sub band and Color");
                CHECK (win.savePng (outDir + "/ui_detonatr_multiband_sub.png"), "screenshot Multiband with the Sub band");
                win.click (px + 632, py + 233);
                CHECK (plainOf (rig, mbParam (multidyn::kSubOn)) < 0.5, "Multiband Sub band off again");
            }
            // drag Transient (4th) to the front
            win.drag (boxX (3), kBoxY, boxX (0) - 30, kBoxY);
            pump (0.1);
            CHECK (std::lround (plainOf (rig, kOrderBase)) == kStageTransient, "dragged to the front: stage 1 is %ld",
                   std::lround (plainOf (rig, kOrderBase)));
            CHECK (std::lround (plainOf (rig, kOrderBase + 1)) == kStageClean, "the others move along: stage 2 is %ld",
                   std::lround (plainOf (rig, kOrderBase + 1)));
            // the light turns a stage off
            win.click (boxLeft (1) + 15, Editor::kStripTop + 15);
            pump (0.1);
            CHECK (plainOf (rig, kCleanOn) < 0.5, "the light turned Clean off");
            CHECK (win.savePng (outDir + "/ui_detonatr_reordered.png"), "screenshot reordered");
        }
        // a fresh window (the first capture of a window is the reliable one)
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor again");
            win.click (boxX (2), kBoxY); // Tone, now 3rd
            for (int i = 0; i < 8; ++i)
            {
                out.clear ();
                rig.render (0.08, out, nullptr, hits ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_detonatr.png"), "screenshot");
        }
        return finish ("detonatr host test");
    }
}
