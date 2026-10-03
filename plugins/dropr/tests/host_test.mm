// End-to-end test of the built Dropr.vst3. usage: dropr_hosttest <Dropr.vst3> <output dir>
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
using namespace dropr;
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

// the input's amplitude at sample i: a 1 kHz tone at -40 dB that jumps to -6 dB at each hit (every
// 0.5 s from 0.25 s) and decays over 100 ms, the same on both channels
static double amplitude (long long i)
{
    const double t = (double)i / 48000.0;
    double a = 0.01;
    if (t >= 0.25)
        a += 0.5 * std::exp (-std::fmod (t - 0.25, 0.5) / 0.1);
    return a;
}

static void hits (int, int, float* buf, int n, long long pos)
{
    for (int i = 0; i < n; ++i)
        buf[i] = (float)(amplitude (pos + i) * std::sin (2.0 * M_PI * 1000.0 * (double)(pos + i) / 48000.0));
}

// the level (dB) of x over 1 ms from sample a, against the input's amplitude there
static double gainAt (const std::vector<float>& x, size_t a, size_t latency)
{
    const size_t n = 48;
    double s = 0.0;
    for (size_t i = a + latency; i < a + latency + n && i < x.size (); ++i)
        s += (double)x[i] * x[i];
    const double level = std::sqrt (2.0 * s / (double)n);
    return 20.0 * std::log10 (std::max (1e-9, level / amplitude ((long long)a)));
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
            return finish ("dropr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (rig.component->getBusCount (kEvent, kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");

        State st = baseState ();
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        const uint32 latency = rig.processor->getLatencySamples ();
        CHECK (latency >= 240 && latency < 400, "latency reported %u (the 5 ms look-ahead and the end saturator)", latency);

        // the defaults: each hit dropped by about the Depth (30 dB), back to 0 dB after the Length
        std::vector<float> out;
        rig.render (1.5, out, nullptr, hits);
        CHECK (allFinite (out), "finite");
        for (double onset : {0.75, 1.25})
        {
            const size_t a = (size_t)(onset * 48000.0) + 24;
            const double on = gainAt (out, a, latency), after = gainAt (out, a + 9600, latency);
            CHECK (on < -22.0 && on > -36.0, "the hit at %.2f s dropped by about the Depth: %.1f dB", onset, on);
            CHECK (std::fabs (after) < 0.5, "and back 200 ms later: %.2f dB", after);
        }

        // Depth 0: untouched
        rig.param (kDepth, toNormalized (kDepth, 0.0));
        out.clear ();
        rig.render (1.5, out, nullptr, hits);
        {
            const size_t a = (size_t)(0.75 * 48000.0) + 24;
            const double on = gainAt (out, a, latency);
            CHECK (std::fabs (on) < 0.1, "Depth 0 passes the hit untouched: %.2f dB", on);
        }

        // state round trip
        rig.param (kLength, toNormalized (kLength, 300.0));
        out.clear ();
        rig.render (0.05, out, nullptr, hits); // (the processor takes the change with its next block)
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::fabs (toPlain (kDepth, back.norm[kDepth])) < 1e-6, "depth saved");
        CHECK (std::fabs (toPlain (kLength, back.norm[kLength]) - 300.0) < 1e-3, "length saved");

        // editor: screenshot while audio is flowing (the shape running), with the default Depth back
        rig.param (kDepth, defaultNormalized (kDepth));
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            ViewRect r;
            CHECK (win.view () && win.view ()->getSize (&r) == kResultOk && r.getWidth () == (int32)Editor::kWidth &&
                       r.getHeight () == (int32)Editor::kHeight,
                   "editor size %d x %d", r.getWidth (), r.getHeight ());
            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, hits);
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_dropr.png"), "screenshot");
        }
        rig.stop ();
        return finish ("dropr host test");
    }
}
