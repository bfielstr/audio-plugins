// End-to-end test of the built Perrera.vst3. usage: perrera_hosttest <Perrera.vst3> <output dir>
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/Editor.h"

#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace perrera;
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
            return finish ("perrera host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams + 1, "param count (+ hidden pitch bend)");
        CHECK (rig.component->getBusCount (kEvent, kInput) == 1, "event input bus");

        State st = baseState ();
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        CHECK (rig.processor->getLatencySamples () == 0, "no latency");

        // the notch between the defaults (high-pass 800, low-pass 200) at 400 Hz
        std::vector<float> out;
        rig.render (1.0, out, nullptr, tone (400.0, 0.25));
        const size_t a = 24000, b = 48000;
        const double notch = toneDb (out, 400.0, a, b) + 12.0;
        CHECK (notch < -8.0, "notch %.1f dB", notch);

        // a note an octave up moves the notch up an octave: 400 Hz passes, 800 Hz is notched
        rig.note (72, 1.0f);
        out.clear ();
        rig.render (1.0, out, nullptr, tone (800.0, 0.25));
        const double notchUp = toneDb (out, 800.0, a, b) + 12.0;
        CHECK (std::fabs (notchUp - notch) < 1.0, "tracked notch %.1f dB (root %.1f)", notchUp, notch);
        // the low-pass is at 400 Hz now, so two octaves below it passes
        out.clear ();
        rig.render (1.0, out, nullptr, tone (100.0, 0.25));
        CHECK (toneDb (out, 100.0, a, b) + 12.0 > -2.0, "100 Hz passes with the note up: %.1f", toneDb (out, 100.0, a, b) + 12.0);

        // state round trip
        rig.param (kSplit, toNormalized (kSplit, 7.0));
        out.clear ();
        rig.render (0.1, out, nullptr, tone (400.0, 0.25));
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::fabs (toPlain (kSplit, back.norm[kSplit]) - 7.0) < 1e-6, "split saved");
        rig.param (kSplit, toNormalized (kSplit, 0.0));
        out.clear ();
        rig.render (0.1, out, nullptr, tone (400.0, 0.25));

        // editor: screenshot while audio is flowing, then gestures
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, tone (110.0, 0.4));
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_perrera.png"), "screenshot");
            // the high-pass handle sits at its (tracked) cutoff: an octave up from 800 Hz
            auto xOfHz = [] (double hz) {
                return Editor::kViewLeft + std::log (hz / 20.0) / std::log (1000.0) * (Editor::kViewRight - Editor::kViewLeft);
            };
            auto yOfDb = [] (double db) {
                return Editor::kViewTop + (18.0 - db) / 54.0 * (Editor::kViewBottom - Editor::kViewTop - 16.0);
            };
            const double hx = xOfHz (1600.0), hy = yOfDb (-3.0);
            const double f0 = plainOf (rig, kHpFreq);
            win.drag (hx, hy, hx + 40, hy);
            CHECK (plainOf (rig, kHpFreq) > f0 * 1.3, "drag raises the high-pass: %.0f -> %.0f", f0, plainOf (rig, kHpFreq));
            const double hx2 = xOfHz (plainOf (rig, kHpFreq) * 2.0);
            win.drag (hx2, hy, hx2, hy - 40);
            CHECK (plainOf (rig, kHpRes) > 0.15, "drag up raises resonance: %.2f", plainOf (rig, kHpRes));
            win.click (xOfHz (plainOf (rig, kHpFreq) * 2.0), yOfDb (-3.0 + 12.0 * plainOf (rig, kHpRes)), 2);
            CHECK (std::fabs (plainOf (rig, kHpFreq) - 800.0) < 1e-6 && plainOf (rig, kHpRes) < 1e-6, "double-click resets");
        }
        rig.stop ();
        return finish ("perrera host test");
    }
}
