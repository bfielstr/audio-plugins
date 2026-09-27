// End-to-end test of the built Stretchr.vst3. usage: stretchr_hosttest <Stretchr.vst3> <output dir>
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
using namespace stretchr;
#define CHECK PK_CHECK

constexpr double kSr = 48000.0;

static InputFn tone (double f, double amp = 0.5)
{
    return [f, amp] (int, int ch, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
            buf[i] = (float)(amp * std::sin (2.0 * M_PI * f * (double)(pos + i) / kSr) * (ch == 0 ? 1.0 : 0.8));
    };
}

// Fundamental by normalised autocorrelation over [a, b).
static double pitchOf (const std::vector<float>& x, size_t a, size_t b)
{
    const int maxLag = (int)(kSr / 60.0), minLag = (int)(kSr / 2000.0);
    const size_t n = b - a - (size_t)maxLag;
    std::vector<double> c ((size_t)maxLag + 2, 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = a; i < a + n; ++i)
        {
            xy += (double)x[i] * x[i + (size_t)lag];
            xx += (double)x[i] * x[i];
            yy += (double)x[i + (size_t)lag] * x[i + (size_t)lag];
        }
        c[(size_t)lag] = xy / std::sqrt (xx * yy + 1e-20);
    }
    double best = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
        best = std::max (best, c[(size_t)lag]);
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (c[(size_t)lag] >= 0.8 * best && c[(size_t)lag] >= c[(size_t)lag - 1] && c[(size_t)lag] >= c[(size_t)lag + 1])
        {
            const double y0 = c[(size_t)lag - 1], y1 = c[(size_t)lag], y2 = c[(size_t)lag + 1];
            const double den = y0 - 2 * y1 + y2;
            return kSr / (lag + (std::fabs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0));
        }
    return 0.0;
}

static void setParam (Rig& rig, uint32_t id, double plain) { rig.param (id, toNormalized (id, plain)); }

static void playFrom (Rig& rig, long long pos)
{
    rig.position = pos;
    rig.ctx.state |= ProcessContext::kPlaying;
}

static void stopTransport (Rig& rig) { rig.ctx.state &= ~ProcessContext::kPlaying; }

// Processes until the worker caught up (it renders in the background).
static void settle (Rig& rig, double seconds = 0.4)
{
    std::vector<float> scratch;
    const long long keep = rig.position;
    const uint32 st = rig.ctx.state;
    stopTransport (rig);
    for (int i = 0; i < (int)(seconds / 0.02); ++i)
    {
        rig.render (0.01, scratch);
        pump (0.01);
    }
    rig.position = keep;
    rig.ctx.state = st;
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
            return finish ("stretchr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");
        CHECK (rig.start (), "start");
        CHECK (rig.processor->getLatencySamples () == 0, "no latency");

        // --- no clip: transparent ------------------------------------------------------
        std::vector<float> out, outR;
        playFrom (rig, 0);
        rig.render (0.2, out, nullptr, tone (330.0));
        {
            double err = 0.0;
            for (size_t i = 0; i < out.size (); ++i)
                err = std::max (err, std::fabs (out[i] - 0.5 * std::sin (2.0 * M_PI * 330.0 * i / kSr)));
            CHECK (err < 1e-6, "pass-through without a clip (err %g)", err);
        }

        EditorWindow ed (rig.controller);
        CHECK (ed.ok (), "editor attached");

        // --- capture 1.5 s of a 220 Hz tone at 1.0 s into the project -------------------
        stopTransport (rig);
        ed.click (56, 52); // Capture
        pump (0.1);
        settle (rig, 0.1);
        playFrom (rig, 48000);
        out.clear ();
        rig.render (1.5, out, nullptr, tone (220.0));
        CHECK (rms (out, 0, out.size ()) > 0.3, "input passes through while capturing");
        stopTransport (rig);
        settle (rig, 0.6);
        ed.savePng (outDir + "/captured.png");

        // --- playback: the clip replaces the input inside its range ---------------------
        playFrom (rig, 0);
        out.clear ();
        outR.clear ();
        rig.render (3.0, out, &outR, tone (523.0, 0.25)); // a different input to tell them apart
        const double pIn = pitchOf (out, 12000, 36000), pClip = pitchOf (out, 60000, 100000),
                     pAfter = pitchOf (out, 130000, 140000);
        CHECK (std::fabs (pIn - 523.0) < 3.0, "before the clip: input %.1f Hz", pIn);
        CHECK (std::fabs (pClip - 220.0) < 1.5, "inside the clip: captured tone %.1f Hz", pClip);
        CHECK (std::fabs (pAfter - 523.0) < 3.0, "after the clip: input %.1f Hz", pAfter);
        CHECK (std::fabs (outR[80000] / out[80000] - 0.8) < 0.02 || std::fabs (out[80000]) < 0.05, "stereo kept");

        // --- pitch +12 and 50 % speed (params from the host) ----------------------------
        setParam (rig, kPitch, 12.0);
        setParam (rig, kSpeed, 0.5);
        settle (rig, 0.8);
        playFrom (rig, 0);
        out.clear ();
        rig.render (4.5, out, nullptr, tone (523.0, 0.25));
        const double pUp = pitchOf (out, 100000, 150000), pLate = pitchOf (out, 170000, 185000),
                     pAfter2 = pitchOf (out, 200000, 210000);
        CHECK (std::fabs (pUp - 440.0) < 3.0, "pitched clip %.1f Hz", pUp);
        CHECK (std::fabs (pLate - 440.0) < 3.0, "stretched clip still playing at 3.7 s: %.1f Hz", pLate);
        CHECK (std::fabs (pAfter2 - 523.0) < 3.0, "input after the stretched clip %.1f Hz", pAfter2);

        // --- Outside = Mute --------------------------------------------------------------
        setParam (rig, kOutside, 1.0);
        playFrom (rig, 0);
        out.clear ();
        rig.render (0.5, out, nullptr, tone (523.0, 0.25));
        CHECK (rms (out, 0, out.size ()) < 1e-6, "input muted outside the clip");
        setParam (rig, kOutside, 0.0);

        // --- editor: add a stretch marker and drag it -----------------------------------
        // Fit view: [-0.02 L, 1.04 L] over x 9..971 with L = 3 s (1.5 s at 50 %).
        auto xAt = [] (double t) { return 9.0 + (t + 0.06) / 3.18 * 962.0; };
        ed.click (xAt (1.5), 250, 2); // double-click: marker at 1.5 s (source 0.75 s)
        pump (0.05);
        ed.drag (xAt (1.5), 250, xAt (2.0), 250); // pin source 0.75 s to 2.0 s
        pump (0.05);
        settle (rig, 0.6);
        ed.savePng (outDir + "/marker.png");
        // the clip still ends at 3 s; the first part is slower, the second faster
        playFrom (rig, 0);
        out.clear ();
        rig.render (4.5, out, nullptr, tone (523.0, 0.25));
        CHECK (std::fabs (pitchOf (out, 150000, 170000) - 440.0) < 3.0, "clip continues after the marker");
        CHECK (std::fabs (pitchOf (out, 200000, 210000) - 523.0) < 3.0, "clip still ends at 4 s");

        // Pitch mode: a +7 st point lifts the whole clip (single point = constant offset)
        ed.click (501, 52); // Pitch mode
        pump (0.05);
        {
            // y for +7 st: plot 88..376, mid 232, half 136
            const double y = 232.0 - 7.0 / 24.0 * 136.0;
            ed.click (xAt (1.0), y, 2);
        }
        pump (0.05);
        settle (rig, 0.6);
        ed.savePng (outDir + "/pitch.png");
        playFrom (rig, 0);
        out.clear ();
        rig.render (2.5, out, nullptr, tone (523.0, 0.25));
        const double p7 = pitchOf (out, 60000, 100000);
        CHECK (std::fabs (p7 / (440.0 * std::pow (2.0, 7.0 / 12.0)) - 1.0) < 0.01, "pitch envelope +7: %.1f Hz", p7);

        // --- state roundtrip into a fresh instance --------------------------------------
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        int64 savedSize = 0;
        saved.seek (0, IBStream::kIBSeekEnd, &savedSize);
        CHECK (savedSize > 1.5 * kSr * 2 * 3, "state holds the audio (%lld bytes)", (long long)savedSize);
        playFrom (rig, 0);
        std::vector<float> ref;
        rig.render (4.0, ref, nullptr, tone (523.0, 0.25));

        Rig rig2;
        rig2.processMode = kOffline; // like a bounce: must wait for the render
        CHECK (rig2.load (argv[1]), "load 2");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        CHECK (rig2.component->setState (&saved) == kResultOk, "setState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        CHECK (rig2.controller->setComponentState (&saved) == kResultOk, "setComponentState");
        CHECK (rig2.start (), "start 2");
        playFrom (rig2, 0);
        std::vector<float> copy;
        rig2.render (4.0, copy, nullptr, tone (523.0, 0.25));
        double diff = 0.0;
        for (size_t i = 0; i < ref.size () && i < copy.size (); ++i)
            diff = std::max (diff, (double)std::fabs (ref[i] - copy[i]));
        CHECK (diff < 2e-3, "restored instance renders the same clip offline (max diff %g)", diff);
        CHECK (std::fabs (rig2.controller->getParamNormalized (kPitch) - toNormalized (kPitch, 12.0)) < 1e-9,
               "controller params restored");

        // offline: a parameter change is audible in the very next block
        setParam (rig2, kPitch, 0.0);
        playFrom (rig2, 0);
        copy.clear ();
        rig2.render (2.5, copy, nullptr, tone (523.0, 0.25));
        const double p0 = pitchOf (copy, 60000, 100000);
        CHECK (std::fabs (p0 / (220.0 * std::pow (2.0, 7.0 / 12.0)) - 1.0) < 0.01, "offline follows changes: %.1f Hz", p0);
        rig2.stop ();

        // --- every algorithm renders through the plug-in ---------------------------------
        // (Pitch back to 0: Soloist keeps formants, so a pure sine shifted far up loses its energy.)
        setParam (rig, kPitch, 0.0);
        for (int a = 0; a < kNumAlgorithms; ++a)
        {
            setParam (rig, kAlgorithm, (double)a);
            settle (rig, 0.5);
            playFrom (rig, 48000);
            out.clear ();
            rig.render (1.0, out, nullptr, tone (523.0, 0.0));
            CHECK (allFinite (out) && rms (out, 0, out.size ()) > 0.05, "algorithm %d plays (rms %.3f)", a,
                   rms (out, 0, out.size ()));
        }

        // --- clear: transparent again -----------------------------------------------------
        ed.click (305, 52); // Clear
        settle (rig, 0.2);
        playFrom (rig, 0);
        out.clear ();
        rig.render (0.5, out, nullptr, tone (330.0));
        CHECK (std::fabs (pitchOf (out, 4000, 20000) - 330.0) < 2.0, "cleared");
        ed.click (578, 52); // Undo brings it back
        settle (rig, 0.6);
        playFrom (rig, 48000);
        out.clear ();
        rig.render (0.5, out, nullptr, tone (330.0, 0.0));
        CHECK (rms (out, 0, out.size ()) > 0.05, "undo restores the clip");
        ed.savePng (outDir + "/editor.png");

        // --- a musical phrase for the README screenshot ----------------------------------
        setParam (rig, kPitch, 0.0);
        setParam (rig, kSpeed, 1.0);
        setParam (rig, kAlgorithm, (double)kSoloist);
        stopTransport (rig);
        ed.click (56, 52); // Capture (replaces the clip)
        settle (rig, 0.1);
        playFrom (rig, 24000);
        out.clear ();
        rig.render (3.2, out, nullptr, [] (int, int, float* buf, int n, long long pos) {
            static const double notes[] = {220.0, 246.9, 261.6, 293.7, 329.6};
            for (int i = 0; i < n; ++i)
            {
                const double t = (double)(pos + i) / kSr - 0.5;
                if (t < 0.0 || t >= 3.0)
                {
                    buf[i] = 0.0f;
                    continue;
                }
                const int k = std::min (4, (int)(t / 0.6));
                const double tn = t - k * 0.6;
                const double f = notes[k] * (1.0 + 0.006 * std::sin (2.0 * M_PI * 5.5 * t));
                double v = 0.0;
                for (int h = 1; h < 12; ++h)
                    v += std::sin (2.0 * M_PI * f * h * t) / (h * (1.0 + 0.15 * h));
                const double env = std::min (1.0, tn / 0.01) * std::exp (-tn * 2.2) * (0.6 + 0.4 * (k % 2));
                buf[i] = (float)(0.45 * v * env + 0.5 * std::exp (-tn * 60.0) * std::sin (2.0 * M_PI * 60.0 * tn));
            }
        });
        stopTransport (rig);
        settle (rig, 0.8);
        {
            // clip: 3.2 s; fit view [-0.064, 3.328] over x 9..971
            auto xs = [] (double t) { return 9.0 + (t + 0.064) / 3.392 * 962.0; };
            ed.click (501, 52); // Pitch mode: a rising bend on the last note
            pump (0.05);
            const double yUp = 232.0 - 2.0 / 24.0 * 136.0;
            ed.click (xs (2.95), 232.0, 2);
            ed.click (xs (3.1), yUp, 2);
            ed.click (431, 52); // Stretch mode: markers on the notes, the third one held longer
            pump (0.05);
            for (double t : {1.1, 1.7, 2.3, 2.9})
            {
                ed.click (xs (t), 300, 2);
                pump (0.02);
            }
            ed.drag (xs (2.3), 300, xs (2.55), 300);
            pump (0.05);
            settle (rig, 0.8);
            ed.mouseDrag (xs (2.55), 300); // hover the dragged marker
        }
        ed.savePng (outDir + "/docs.png");

        rig.stop ();
        return finish ("stretchr host test");
    }
}
