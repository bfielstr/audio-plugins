// End-to-end test of the built Wubr.vst3. usage: wubr_hosttest <Wubr.vst3> <output dir>
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
using namespace wubr;
#define CHECK PK_CHECK

static InputFn tone (double hz, double amp)
{
    return [hz, amp] (int, int, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
            buf[i] = (float)(amp * std::sin (2 * M_PI * hz * (double)(pos + i) / 48000.0));
    };
}

// dB rms of 20 ms windows
static std::vector<double> windows (const std::vector<float>& x)
{
    std::vector<double> out;
    for (size_t a = 0; a + 960 <= x.size (); a += 960)
    {
        double s = 0.0;
        for (size_t i = a; i < a + 960; ++i)
            s += (double)x[i] * x[i];
        out.push_back (10.0 * std::log10 (s / 960.0 + 1e-20));
    }
    return out;
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
            return finish ("wubr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams, "param count");
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets
        checkNewInstanceGentlr (rig.controller, kTailBase + pk::kTailOn, kTailExtBase + pk::kTailExtClarity, kTailExt3Base + pk::kTailExt3Slope);
        // the end saturator as a new instance had it up to 0.24 (off, its Gentlr off, 12 / 12), before it
        // starts: the checks are about wubr's own sound
        {
            State st;
            for (uint32_t id = 0; id < kNumParams; ++id)
            {
                st.norm[id] = defaultNormalized (id);
                st.has[id] = true;
            }
            st.norm[kTailBase + pk::kTailOn] = 0.0;
            st.norm[kTailExtBase + pk::kTailExtClarity] = 0.0;
            st.norm[kTailExt3Base + pk::kTailExt3Slope] = 0.0;
            CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState: the end saturator as it was");
        }
        CHECK (rig.component->getBusCount (kEvent, kInput) == 1, "event input bus (the envelope's MIDI trigger)");
        CHECK (rig.start (), "start");
        // measured below: band 1 alone moving its gain, synced at 1/4 (the defaults sweep both bands' centres at 0 dB)
        rig.param (bandParam (0, kTarget), toNormalized (bandParam (0, kTarget), kTargetGain));
        rig.param (bandParam (0, kRateMode), toNormalized (bandParam (0, kRateMode), kSynced));
        rig.param (bandParam (1, kBandOn), 0.0);
        rig.param (kLinkRate, 0.0);

        // the LFO swings a 120 Hz tone through band 1 by about +-12 dB
        std::vector<float> out;
        rig.render (2.0, out, nullptr, tone (120.0, 0.1));
        auto w = windows (out);
        const auto [lo, hi] = std::minmax_element (w.begin () + 10, w.end ());
        CHECK (*hi - *lo > 18.0, "the band moves: %.1f dB swing", *hi - *lo);

        // Envelope, MIDI: a note starts the shape; with the hold on its top it stays up while the note is held
        rig.param (kMode, toNormalized (kMode, kEnvelope));
        rig.param (bandParam (0, kHold), toNormalized (bandParam (0, kHold), 2.0));
        out.clear ();
        rig.render (0.3, out, nullptr, tone (120.0, 0.1));
        rig.note (60, 1.0f);
        out.clear ();
        rig.render (0.8, out, nullptr, tone (120.0, 0.1));
        auto held = windows (out);
        CHECK (held.back () > held.front () + 12.0, "held at the top: %.1f dB (from %.1f)", held.back (), held.front ());

        // state round trip
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::lround (toPlain (kMode, back.norm[kMode])) == kEnvelope, "the mode saved");

        // editor screenshots: LFO with audio moving the band; band 2 on, frequency target
        rig.note (60, 0.0f);
        rig.param (kMode, toNormalized (kMode, kLfo));
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, tone (120.0, 0.2));
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_wubr.png"), "screenshot");
        }
        rig.param (bandParam (1, kBandOn), 1.0);
        rig.param (bandParam (1, kTarget), toNormalized (bandParam (1, kTarget), kTargetBoth));
        rig.param (kMode, toNormalized (kMode, kEnvelope));
        rig.param (kTailBase + pk::kTailOn, 1.0);
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor, band 2");
            for (int i = 0; i < 10; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, tone (120.0, 0.2));
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_wubr_envelope.png"), "envelope screenshot");
        }
        CHECK (plainOf (rig, bandParam (1, kBandOn)) >= 0.5, "band 2 on");
        return finish ("wubr host test");
    }
}
