// End-to-end test of the built Dropr.vst3. usage: dropr_hosttest <Dropr.vst3> <output dir>
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/BandView.h"
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

static double plainOf (Rig& rig, uint32_t id) { return toPlain (id, rig.controller->getParamNormalized (id)); }

// a snare-like hit every 0.5 s from 0.25 s: a crack gone in about 5 ms and a body 20 dB under it, decaying over 120 ms
static double snareAt (long long i)
{
    const double t = (double)i / 48000.0;
    if (t < 0.25)
        return 0.0;
    const double u = std::fmod (t - 0.25, 0.5);
    const double noise = std::sin (12345.678 * (double)i) * std::sin (0.731 * (double)i); // deterministic hiss
    return 0.9 * std::exp (-u / 0.0015) * (0.6 * noise + 0.4 * std::sin (2 * M_PI * 220.0 * u)) +
           0.09 * std::exp (-u / 0.12) * (0.7 * std::sin (2 * M_PI * 190.0 * u) + 0.3 * noise);
}

static void hits (int, int, float* buf, int n, long long pos)
{
    for (int i = 0; i < n; ++i)
        buf[i] = (float)snareAt (pos + i);
}

// the crack's peak (first 5 ms) against the body (30 .. 150 ms), dB, for the hit at `onset` s
static double transientToBody (const std::vector<float>& x, double onset, size_t latency)
{
    const size_t a = (size_t)(onset * 48000.0) + latency;
    double pk = 0.0, s = 0.0;
    for (size_t i = a; i < a + 240 && i < x.size (); ++i)
        pk = std::max (pk, (double)std::fabs (x[i]));
    const size_t b0 = a + 1440, b1 = std::min (x.size (), a + 7200);
    for (size_t i = b0; i < b1; ++i)
        s += (double)x[i] * x[i];
    return 20.0 * std::log10 (std::max (1e-9, pk) / std::max (1e-9, std::sqrt (2.0 * s / (double)std::max<size_t> (1, b1 - b0))));
}

// the display's geometry (BandView), in editor coordinates
static double plotLeft () { return Editor::kDisplayLeft + BandView::kAxisLeft; }
static double plotRight () { return Editor::kDisplayRight - BandView::kPad; }
static double plotTop () { return Editor::kDisplayTop + BandView::kTop; }
static double plotBottom () { return Editor::kDisplayBottom - BandView::kAxisBottom - 4.0; }
static double xOfHz (double hz)
{
    return plotLeft () + std::log (hz / kMinXoverHz) / std::log (kMaxXoverHz / kMinXoverHz) * (plotRight () - plotLeft ());
}
static double yOfDb (double db)
{
    return plotTop () + (BandView::kTopDb - db) / (BandView::kTopDb - BandView::kBottomDb) * (plotBottom () - plotTop ());
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
        CHECK (rig.component->getBusCount (kEvent, Steinberg::Vst::kInput) == 0, "no event input");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets

        State st = baseState ();
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        const uint32 latency = rig.processor->getLatencySamples ();
        CHECK (latency > 0 && latency < 200, "latency reported %u (the end saturator's)", latency);

        // the defaults: the crack clamped, the body up, the end held to 0 dBFS
        std::vector<float> out;
        rig.render (1.5, out, nullptr, hits);
        CHECK (allFinite (out), "finite");
        float pk = 0.0f;
        for (float v : out)
            pk = std::max (pk, std::fabs (v));
        CHECK (pk <= 1.001f && pk > 0.5f, "the output peaks near 0 dBFS: %.2f dBFS", dbfs (pk));
        std::vector<float> in (out.size ());
        hits (0, 0, in.data (), (int)in.size (), 0);
        const double before = transientToBody (in, 0.75, 0), after = transientToBody (out, 0.75, latency);
        CHECK (after < before - 10.0, "the snap clamped against the body: %.1f dB -> %.1f dB", before, after);

        // the text of the negative ratio
        String128 txt {};
        rig.controller->getParamStringByValue (kNegRatio, 1.0, txt);
        CHECK (txt[0] == '1' && std::u16string (reinterpret_cast<const char16_t*> (txt)).find (u"-inf") != std::u16string::npos,
               "the negative ratio reads 1 : -inf");

        // state round trip
        rig.param (kDownThreshold, toNormalized (kDownThreshold, -60.0));
        rig.param (kBands, toNormalized (kBands, 4.0));
        out.clear ();
        rig.render (0.05, out, nullptr, hits); // (the processor takes the change with its next block)
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (std::fabs (toPlain (kDownThreshold, back.norm[kDownThreshold]) + 60.0) < 1e-3, "threshold saved");
        CHECK (std::lround (toPlain (kBands, back.norm[kBands])) == 4, "bands saved");
        rig.param (kDownThreshold, defaultNormalized (kDownThreshold));
        rig.param (kBands, defaultNormalized (kBands));

        // editor: screenshot while audio is flowing (the meters moving), then its controls
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            ViewRect r;
            CHECK (win.view () && win.view ()->getSize (&r) == kResultOk && r.getWidth () == (int32)Editor::kWidth &&
                       r.getHeight () == (int32)(Editor::kHeight + pk::EditorBase::kInfoHeight),
                   "editor size %d x %d", r.getWidth (), r.getHeight ());
            for (int i = 0; i < 20; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, hits);
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_dropr.png"), "screenshot");

            // the downward threshold line: drag it from -72 dB up to about -48 dB (at 450 Hz: no point or handle there)
            const double tx = xOfHz (450.0);
            win.drag (tx, yOfDb (-72.0), tx, yOfDb (-48.0));
            pump (0.05);
            CHECK (std::fabs (plainOf (rig, kDownThreshold) + 48.0) < 2.0, "the threshold line dragged: %.1f dB", plainOf (rig, kDownThreshold));
            win.click (tx, yOfDb (plainOf (rig, kDownThreshold)), 2); // double-click: back to -72 dB
            pump (0.05);
            CHECK (std::fabs (plainOf (rig, kDownThreshold) + 72.0) < 0.01, "double-click resets it: %.1f dB", plainOf (rig, kDownThreshold));

            // crossover 2 (800 Hz): drag its handle to about 1.2 kHz
            const double hy = yOfDb (-20.0);
            win.drag (xOfHz (800.0), hy, xOfHz (1200.0), hy);
            pump (0.05);
            CHECK (std::fabs (plainOf (rig, kXover1 + 1) - 1200.0) < 60.0, "crossover 2 dragged to %.0f Hz", plainOf (rig, kXover1 + 1));

            // band 3's point (at the centre of 1.2 kHz .. 2.15 kHz, +12 dB): up to +24 dB
            const double cx = xOfHz (std::sqrt (plainOf (rig, kXover1 + 1) * plainOf (rig, kXover1 + 2)));
            win.drag (cx, yOfDb (12.0), cx, yOfDb (24.0));
            pump (0.05);
            CHECK (std::fabs (plainOf (rig, kBandGain1 + 2) - 24.0) < 1.5, "band 3's gain dragged to %.1f dB", plainOf (rig, kBandGain1 + 2));

            // Bands: 3; the Negative toggle: off (the normal ratio knob shows)
            win.click (Editor::kSideLeft + Editor::kBandsSelLeft + 2.5 * Editor::kBandsSelW / 6.0, Editor::kSideTop + Editor::kBandsSelTop + 10.0);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kBands)) == 3, "Bands 3 picked (%.0f)", plainOf (rig, kBands));
            win.click (Editor::kDynLeft + Editor::kDynFirst + 4 * Editor::kDynColumn + 28.0, Editor::kDynTop + Editor::kNegToggleTop + 8.0);
            pump (0.05);
            CHECK (plainOf (rig, kNegative) < 0.5, "Negative switched off");
            for (int i = 0; i < 10; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, hits);
                pump (0.03);
            }
            CHECK (allFinite (out), "finite after the edits");
            CHECK (win.savePng (outDir + "/ui_dropr_edited.png"), "screenshot after the edits");
        }
        rig.stop ();
        return finish ("dropr host test");
    }
}
