// End-to-end test of the built Smatcheratr.vst3. usage: smatcheratr_hosttest <Smatcheratr.vst3> <output dir>
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
using namespace smatcheratr;
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
            return finish ("smatcheratr host test");
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

        // drive through the plug-in: harmonics appear, the clip holds the peak at 0 dB
        rig.param (kCurve, toNormalized (kCurve, kMediumCurve));
        rig.param (kDrive, toNormalized (kDrive, 18.0));
        out.clear ();
        rig.render (1.0, out, nullptr, tone (1000.0, 0.25));
        const size_t a = 24000, b = 48000;
        CHECK (toneDb (out, 3000.0, a, b) > -30.0, "third harmonic %.1f dB", toneDb (out, 3000.0, a, b));
        CHECK (std::fabs (out[a + 100]) <= 1.0001, "peak");

        // state round trip
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::fabs (toPlain (kDrive, back.norm[kDrive]) - 18.0) < 1e-6, "drive saved");
        CHECK (std::lround (toPlain (kCurve, back.norm[kCurve])) == kMediumCurve, "curve saved");

        // editor: screenshot while audio is flowing, then gestures
        rig.param (kColorOn, toNormalized (kColorOn, 1.0));
        rig.param (kColorLo, toNormalized (kColorLo, -0.3));
        rig.param (kColorHi, toNormalized (kColorHi, 0.25));
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, tone (110.0, 0.4));
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_smatcheratr.png"), "screenshot");

            // shaper display: drag down lowers Drive, double-click resets it
            const double sx = Editor::kShaperLeft + Editor::kShaperWidth / 2, sy = Editor::kShaperTop + Editor::kShaperHeight / 2;
            const double d0 = plainOf (rig, kDrive);
            win.drag (sx, sy, sx, sy + 60);
            CHECK (plainOf (rig, kDrive) < d0 - 6.0, "drag down lowers drive: %.1f -> %.1f", d0, plainOf (rig, kDrive));
            win.click (sx, sy, 2);
            CHECK (std::fabs (plainOf (rig, kDrive)) < 1e-6, "double-click resets drive: %.2f", plainOf (rig, kDrive));

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
        return finish ("smatcheratr host test");
    }
}
