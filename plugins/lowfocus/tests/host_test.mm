// End-to-end test of the built Lowfocus.vst3. usage: lowfocus_hosttest <Lowfocus.vst3> <output dir>
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"

#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace lowfocus;
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

static InputFn bass ()
{
    return [] (int, int, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
        {
            const double t = (double)(pos + i) / 48000.0;
            buf[i] = (float)(0.5 * std::sin (2 * M_PI * 55 * t) + 0.05 * std::sin (2 * M_PI * 80 * t) +
                             0.2 * std::sin (2 * M_PI * 110 * t) + 0.05 * std::sin (2 * M_PI * 3000 * t));
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
            return finish ("lowfocus host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");

        State st = baseState ();
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        const uint32 latency = rig.processor->getLatencySamples ();
        CHECK (latency == 4096, "latency reported %u", latency);

        // transparent at contrast 0: output == input delayed by the reported latency
        std::vector<float> out;
        rig.render (1.0, out, nullptr, bass ());
        std::vector<float> ref;
        {
            std::vector<float> tmp (48000);
            bass () (0, 0, tmp.data (), 48000, 0);
            ref = tmp;
        }
        double err = 0;
        for (size_t i = latency + 8192; i < 48000; ++i)
            err = std::max (err, (double)std::fabs (out[i] - ref[i - latency]));
        CHECK (err < 1e-4, "passthrough error %g", err);

        // contrast through the plug-in
        rig.param (kContrast, toNormalized (kContrast, 1.0));
        out.clear ();
        rig.render (4.0, out, nullptr, bass ());
        const size_t a = 2 * 48000, b = 4 * 48000;
        const double weak = toneDb (out, 80.0, a, b) - toneDb (out, 55.0, a, b);
        CHECK (weak < -26.0, "80 Hz relative to 55 Hz after +100%% contrast: %.2f dB (was -20)", weak);
        CHECK (std::fabs (toneDb (out, 3000.0, a, b) + 26.0) < 0.1, "3 kHz untouched: %.2f", toneDb (out, 3000.0, a, b));

        // state round trip
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::fabs (back.norm[kContrast] - 1.0) < 1e-9, "contrast saved");

        // editor: screenshot while audio is flowing, then gestures
        rig.param (kContrast, toNormalized (kContrast, 0.6));
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, bass ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_lowfocus.png"), "screenshot");

            // spectrum x = 8 + log(hz/20)/log(100) * 744
            auto xOf = [] (double hz) { return 8.0 + std::log (hz / 20.0) / std::log (100.0) * 744.0; };
            const double y = 150;
            win.drag (xOf (300.0), y, xOf (200.0), y);
            CHECK (std::fabs (plainOf (rig, kHighFreq) - 200.0) < 8.0, "high edge drag -> %.1f Hz", plainOf (rig, kHighFreq));
            win.drag (xOf (30.0), y, xOf (40.0), y);
            CHECK (std::fabs (plainOf (rig, kLowFreq) - 40.0) < 2.0, "low edge drag -> %.1f Hz", plainOf (rig, kLowFreq));
            const double c0 = plainOf (rig, kContrast);
            win.drag (xOf (90.0), y, xOf (90.0), y + 60);
            CHECK (plainOf (rig, kContrast) < c0 - 0.3, "drag down lowers contrast: %.2f -> %.2f", c0, plainOf (rig, kContrast));
            const double lo0 = plainOf (rig, kLowFreq), hi0 = plainOf (rig, kHighFreq);
            win.drag (xOf (90.0), y, xOf (180.0), y);
            CHECK (std::fabs (plainOf (rig, kLowFreq) / lo0 - 2.0) < 0.1 && std::fabs (plainOf (rig, kHighFreq) / hi0 - 2.0) < 0.1,
                   "sideways drag moves the range: %.1f..%.1f", plainOf (rig, kLowFreq), plainOf (rig, kHighFreq));
            win.click (xOf (plainOf (rig, kLowFreq) * 1.5), y, 2);
            CHECK (std::fabs (plainOf (rig, kContrast)) < 1e-6, "double-click resets contrast");
        }
        rig.stop ();
        return finish ("lowfocus host test");
    }
}
