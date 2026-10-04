// End-to-end test of the built Smoothr.vst3. usage: smoothr_hosttest <Smoothr.vst3> <output dir>
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/Editor.h"

#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace smoothr;
#define CHECK PK_CHECK

static InputFn tone (double hz, double amp)
{
    return [hz, amp] (int, int, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
            buf[i] = (float)(amp * std::sin (2 * M_PI * hz * (double)(pos + i) / 48000.0));
    };
}

// a loud little mix: a 55 Hz bass, a kick (a falling thump and a click) every half second, a snare-ish
// noise burst on the off-beats, a low-mid chord and some hats; `gain` scales it all
static InputFn mix (double gain)
{
    return [gain] (int, int ch, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
        {
            const long long t = pos + i;
            const double s = (double)t / 48000.0;
            const double beat = std::fmod (s, 0.5), off = std::fmod (s + 0.25, 0.5);
            double v = 0.35 * std::sin (2 * M_PI * 55.0 * s);
            v += 0.6 * std::exp (-beat * 25.0) * std::sin (2 * M_PI * (45.0 * beat + 100.0 * (1.0 - std::exp (-beat * 30.0)) / 30.0));
            if (beat < 0.004)
                v += 0.4 * std::sin (M_PI * beat / 0.004) * std::sin (2 * M_PI * 3500.0 * s);
            uint32_t h = (uint32_t)(t * 2654435761u) ^ (uint32_t)(ch * 0x9E3779B9u);
            h ^= h >> 13;
            h *= 0x5bd1e995u;
            h ^= h >> 15;
            const double noise = (double)(h & 0xFFFF) / 32768.0 - 1.0;
            v += 0.3 * std::exp (-off * 30.0) * noise;
            v += 0.05 * noise * std::exp (-std::fmod (s, 0.125) * 60.0);
            for (double f : {196.0, 247.0, 294.0})
                v += 0.06 * std::sin (2 * M_PI * f * s + ch);
            buf[i] = (float)(v * gain);
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
            return finish ("smoothr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (rig.component->getBusCount (kEvent, smoothr::kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "all automatable");
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets
        CHECK (rig.start (), "start");
        const uint32 latency = rig.processor->getLatencySamples ();
        CHECK (latency > 0 && latency < 48000 / 20, "latency %u samples", (unsigned)latency);

        // at the defaults a quiet tone passes at its level (the saturator is linear down there, the
        // dip and the limiter do nothing)
        std::vector<float> out;
        rig.render (0.5, out, nullptr, tone (346.0, 0.1));
        const double flat = dbfs (rms (out, out.size () / 2, out.size ())) - dbfs (0.1 / std::sqrt (2.0));
        CHECK (std::fabs (flat) < 0.2, "a quiet tone passes: %.2f dB", flat);
        CHECK (allFinite (out), "finite");

        // pushed 12 dB into it: never over the ceiling (-1 dB)
        rig.param (smoothr::kInput, toNormalized (smoothr::kInput, 12.0));
        out.clear ();
        std::vector<float> outR;
        rig.render (2.0, out, &outR, mix (1.0));
        double peak = 0.0;
        for (size_t i = 0; i < out.size (); ++i)
            peak = std::max ({peak, (double)std::fabs (out[i]), (double)std::fabs (outR[i])});
        CHECK (dbfs (peak) <= -1.0 + 1e-4, "peak %.3f dBFS under the -1 dB ceiling", dbfs (peak));
        const double loud = dbfs (rms (out, out.size () / 2, out.size ()));
        CHECK (loud > -12.0, "and loud: %.1f dBFS rms", loud);
        CHECK (rig.processor->getLatencySamples () == latency, "the latency stays");

        // state round trip
        rig.param (kCeiling, toNormalized (kCeiling, -3.0));
        rig.param (kSmooth, toNormalized (kSmooth, 0.8));
        rig.param (kCharacter, toNormalized (kCharacter, 0.6));
        rig.param (kRelease, toNormalized (kRelease, 200.0));
        rig.param (kAutoRelease, 0.0);
        out.clear ();
        rig.render (0.05, out, nullptr, tone (346.0, 0.1)); // (the processor takes the changes in its next block)
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::fabs (toPlain (kCeiling, back.norm[kCeiling]) + 3.0) < 0.01, "the ceiling saved");
        CHECK (std::fabs (toPlain (kSmooth, back.norm[kSmooth]) - 0.8) < 0.01, "Smooth saved");
        CHECK (std::fabs (toPlain (kCharacter, back.norm[kCharacter]) - 0.6) < 0.01, "Character saved");
        CHECK (std::fabs (toPlain (kRelease, back.norm[kRelease]) - 200.0) < 0.5, "the release saved");
        CHECK (back.norm[kAutoRelease] < 0.5, "Auto off saved");

        // editor screenshot: the loud mix pushed 9 dB in (the history full of gain reduction, the lows'
        // line smooth under the highs'), the defaults otherwise, the saturator a little harder
        for (uint32_t id : {kCeiling, kSmooth, kCharacter, kRelease, kAutoRelease})
            rig.param (id, defaultNormalized (id));
        rig.param (smoothr::kInput, toNormalized (smoothr::kInput, 9.0));
        rig.param (kTailBase + pk::kTailDrive, toNormalized (kTailBase + pk::kTailDrive, 3.0));
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            for (int i = 0; i < 110; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, mix (1.0));
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_smoothr.png"), "screenshot");

            // a click on the meters clears the holds (and nothing breaks)
            win.click (Editor::kViewRight - 60.0, 0.5 * (Editor::kViewTop + Editor::kViewBottom));
            pump (0.05);
            CHECK (plainOf (rig, smoothr::kInput) > 8.9, "the input stays at +9 dB: %.1f", plainOf (rig, smoothr::kInput));
        }
        return finish ("smoothr host test");
    }
}
