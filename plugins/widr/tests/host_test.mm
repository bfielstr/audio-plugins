// End-to-end test of the built Widr.vst3. usage: widr_hosttest <Widr.vst3> <output dir>
// Two instances in one process: they must find each other and share the stereo field.
#include "Dsp.h"
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/Editor.h"
#include "ui/StageView.h"

#include "public.sdk/source/common/memorystream.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace widr;
#define CHECK PK_CHECK

static double plainOf (Rig& rig, uint32_t id) { return toPlain (id, rig.controller->getParamNormalized (id)); }
static void setPlain (Rig& rig, uint32_t id, double v) { rig.param (id, toNormalized (id, v)); }

// Deterministic noise, the same on both channels (a mono source), optionally band-passed.
struct Noise
{
    double lo = 0.0, hi = 0.0; // band-pass edges (0: full band)
    double gain = 1.0;
    std::array<Biquad, 4> st[2];
    InputFn fn ()
    {
        return [this] (int, int ch, float* buf, int n, long long pos) {
            const auto hp = BiquadCoeffs::highPass (lo, 0.7, 48000.0), lp = BiquadCoeffs::lowPass (hi, 0.7, 48000.0);
            for (int i = 0; i < n; ++i)
            {
                uint32_t h = (uint32_t)(pos + i) * 2654435761u;
                h ^= h >> 15;
                h *= 2246822519u;
                h ^= h >> 13;
                double x = ((double)(h & 0xFFFFFF) / 8388608.0 - 1.0) * 0.25 * gain;
                if (lo > 0.0)
                    x = st[ch][3].tick (lp, st[ch][2].tick (lp, st[ch][1].tick (hp, st[ch][0].tick (hp, x)))) * 3.0;
                buf[i] = (float)x;
            }
        };
    }
};

// Energy (dB) of the side (L - R) / 2 between two frequencies, over [a, b).
static double sideDb (const std::vector<float>& l, const std::vector<float>& r, double f0, double f1, size_t a, size_t b)
{
    const auto hp = BiquadCoeffs::highPass (f0, 0.7, 48000.0), lp = BiquadCoeffs::lowPass (f1, 0.7, 48000.0);
    Biquad s[4];
    double e = 0.0;
    for (size_t i = 0; i < b; ++i)
    {
        const double x = s[3].tick (lp, s[2].tick (lp, s[1].tick (hp, s[0].tick (hp, 0.5 * ((double)l[i] - r[i])))));
        if (i >= a)
            e += x * x;
    }
    return 10.0 * std::log10 (std::max (1e-20, e / (double)(b - a)));
}

// Renders both instances side by side (as a host would, block by block) for `secs`.
static void renderBoth (Rig& a, Rig& b, Noise& na, Noise& nb, double secs, std::vector<float>& al, std::vector<float>& ar,
                        std::vector<float>& bl, std::vector<float>& br)
{
    al.clear ();
    ar.clear ();
    bl.clear ();
    br.clear ();
    for (double t = 0.0; t < secs; t += 0.01)
    {
        a.render (0.01, al, &ar, na.fn ());
        b.render (0.01, bl, &br, nb.fn ());
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
        Rig a, b;
        CHECK (a.load (argv[1]) && b.load (argv[1]), "load two instances");
        if (gFail)
            return finish ("widr host test");
        CHECK (a.controller->getParameterCount () == (int32)kNumParams, "param count");
        CHECK (countNonAutomatable (a.controller) == 0, "non-automatable parameters");
        CHECK (a.start () && b.start (), "start");
        const uint32 latency = a.processor->getLatencySamples ();
        CHECK (latency > 0 && latency < 200, "latency reported %u", latency);

        // A: full-band material, B: band-limited material around 1 kHz. Each only gives way to a
        // higher role, so a role change on one moves the other's gains in the band they share.
        for (Rig* r : {&a, &b})
        {
            setPlain (*r, kAware, 1.0);
            setPlain (*r, kGuard, 0.0);
            setPlain (*r, kWidth, 1.5);
        }
        Noise na, nb;
        nb.lo = 600.0;
        nb.hi = 1600.0;
        std::vector<float> al, ar, bl, br;
        const size_t s0 = 96000;
        // 1, 2: A (Wide) sees B. With B an Ambient below it, A keeps its width; with B the Anchor,
        // A gives way in B's band (and only there).
        nb.gain = 5.0;
        setPlain (a, kRole, kWideRole);
        setPlain (b, kRole, kAmbient);
        renderBoth (a, b, na, nb, 3.0, al, ar, bl, br);
        const double aBand1 = sideDb (al, ar, 700.0, 1300.0, s0, al.size ()) - sideDb (al, ar, 5000.0, 12000.0, s0, al.size ());
        setPlain (b, kRole, kAnchor);
        renderBoth (a, b, na, nb, 3.0, al, ar, bl, br);
        const double aBand2 = sideDb (al, ar, 700.0, 1300.0, s0, al.size ()) - sideDb (al, ar, 5000.0, 12000.0, s0, al.size ());
        CHECK (aBand2 < aBand1 - 3.0, "A gives way in B's band once B is the Anchor: %.1f -> %.1f dB", aBand1, aBand2);
        // 3, 4: B (Support) sees A. With A an Ambient below it, B keeps its width; with A the
        // Anchor, B gives way where A is wide.
        nb.gain = 1.0;
        na.gain = 3.0;
        setPlain (b, kRole, kSupport);
        setPlain (a, kRole, kAmbient);
        renderBoth (a, b, na, nb, 3.0, al, ar, bl, br);
        const double bSide3 = sideDb (bl, br, 700.0, 1300.0, s0, bl.size ());
        setPlain (a, kRole, kAnchor);
        renderBoth (a, b, na, nb, 3.0, al, ar, bl, br);
        const double bSide4 = sideDb (bl, br, 700.0, 1300.0, s0, bl.size ());
        CHECK (bSide4 < bSide3 - 3.0, "B gives way once A is the Anchor: %.1f -> %.1f dB", bSide3, bSide4);
        na.gain = 1.0;

        // state round trip into a fresh instance
        setPlain (a, kWidth, 1.7);
        setPlain (a, kCharacter, kEpic);
        a.render (0.05, al, &ar, na.fn ());
        MemoryStream saved;
        CHECK (a.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::fabs (toPlain (kWidth, back.norm[kWidth]) - 1.7) < 1e-6 && std::lround (toPlain (kCharacter, back.norm[kCharacter])) == kEpic,
               "width and character saved");
        {
            Rig c;
            CHECK (c.load (argv[1]), "load 3");
            saved.seek (0, IBStream::kIBSeekSet, nullptr);
            CHECK (c.component->setState (&saved) == kResultOk, "setState");
            saved.seek (0, IBStream::kIBSeekSet, nullptr);
            CHECK (c.controller->setComponentState (&saved) == kResultOk, "setComponentState");
            CHECK (std::fabs (plainOf (c, kWidth) - 1.7) < 1e-6, "controller restored: %f", plainOf (c, kWidth));
        }

        // editor: screenshot with both instances playing, then gestures on the stage
        setPlain (a, kWidth, 1.2);
        setPlain (a, kSpace, 0.45);
        setPlain (b, kWidth, 0.6);
        setPlain (b, kSpace, 0.15);
        {
            EditorWindow win (a.controller);
            CHECK (win.ok (), "editor");
            for (int i = 0; i < 30; ++i)
            {
                a.render (0.03, al, &ar, na.fn ());
                b.render (0.03, bl, &br, nb.fn ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_widr.png"), "screenshot");

            // the stage geometry (StageView): the listener at the bottom centre of the field
            const double fieldBottom = Editor::kStageBottom - StageView::kStrip - 6.0;
            const double cx = 0.5 * (Editor::kStageLeft + Editor::kStageRight), cy = fieldBottom - 14.0;
            auto radius = [&] (double space) {
                const double rMax = (fieldBottom - Editor::kStageTop) - 34.0;
                return 46.0 + space * (rMax - 46.0);
            };
            const double w0 = plainOf (a, kWidth);
            const double ang = StageView::angleFor (w0) * M_PI / 180.0, r = radius (plainOf (a, kSpace));
            const double ex = cx + r * std::sin (ang), ey = cy - r * std::cos (ang);
            win.drag (ex, ey, ex + 50, ey);
            CHECK (plainOf (a, kWidth) > w0 + 0.1, "dragging the arc end outwards widens: %.2f -> %.2f", w0, plainOf (a, kWidth));
            const double sp0 = plainOf (a, kSpace);
            win.drag (cx, cy - 20, cx, cy - 74);
            CHECK (std::fabs (plainOf (a, kSpace) - (sp0 + 54.0 / 180.0)) < 0.03, "drag up adds Space: %.2f -> %.2f", sp0,
                   plainOf (a, kSpace));
            win.drag (cx, cy - 20, cx, cy - 74, kShift);
            const double fine = plainOf (a, kSpace);
            CHECK (fine > sp0 && fine < sp0 + 54.0 / 180.0 + 0.3 * 54.0 / 180.0, "Shift is fine: %.2f", fine);
            win.click (cx, cy - 20, 2);
            CHECK (std::fabs (plainOf (a, kWidth) - 1.0) < 1e-6 && std::fabs (plainOf (a, kSpace) - 0.2) < 1e-6,
                   "double-click resets Width and Space");
            for (int i = 0; i < 5; ++i) // let the editor redraw
            {
                a.render (0.03, al, &ar, na.fn ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_widr_reset.png"), "screenshot 2");
        }
        a.stop ();
        b.stop ();
        return finish ("widr host test");
    }
}
