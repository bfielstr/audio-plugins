// End-to-end test of the built Para.vst3. usage: para_hosttest <Para.vst3> <output dir>
#include "Params.h"
#include "Slopes.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/Editor.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace para;
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

// A state as Para 0.6 saved it (version 3: three slopes, one drive), with the values given (normalized).
static bool writeOldState (IBStream* stream, const std::vector<std::pair<uint32_t, double>>& values)
{
    IBStreamer s (stream, kLittleEndian);
    bool ok = s.writeInt32 (0x50455252) && s.writeInt32 (3) && s.writeInt32 ((int32)values.size ());
    for (auto [id, v] : values)
        ok = ok && s.writeInt32u (id) && s.writeDouble (v);
    return ok;
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
            return finish ("para host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams + 1, "param count (+ hidden pitch bend)");
        CHECK (rig.component->getBusCount (kEvent, kInput) == 1, "event input bus");

        State st = baseState ();
        st.norm[kHpFreq] = toNormalized (kHpFreq, 800.0);
        st.norm[kLpFreq] = toNormalized (kLpFreq, 200.0);
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        CHECK (rig.processor->getLatencySamples () > 0, "the end-of-chain saturator's latency is reported");

        // the notch between a high-pass at 800 Hz and a low-pass at 200 Hz, at 400 Hz
        std::vector<float> out;
        rig.render (1.0, out, nullptr, tone (400.0, 0.25));
        const size_t a = 24000, b = 48000;
        const double notch = toneDb (out, 400.0, a, b) + 12.0;
        CHECK (notch < -8.0, "notch %.1f dB", notch);

        // notes do not move the filters: an octave up, 400 Hz is still notched
        rig.note (72, 1.0f);
        out.clear ();
        rig.render (1.0, out, nullptr, tone (400.0, 0.25));
        const double notchUp = toneDb (out, 400.0, a, b) + 12.0;
        CHECK (std::fabs (notchUp - notch) < 1.0, "the notch stays: %.1f dB (before %.1f)", notchUp, notch);

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

        // the drives, one per filter (Post, +18 dB): a 110 Hz tone (in the low-pass's band) gets its third
        // harmonic from the low-pass's drive, hardly from the high-pass's; the latency stays
        const uint32 lat0 = rig.processor->getLatencySamples ();
        out.clear ();
        rig.render (0.5, out, nullptr, tone (110.0, 0.4));
        const double h3Off = toneDb (out, 330.0, 12000, 24000);
        rig.param (kHpDriveOn, 1.0);
        rig.param (kHpDrive, toNormalized (kHpDrive, 18.0));
        rig.param (kLpDrive, toNormalized (kLpDrive, 18.0));
        rig.param (kDrivePos, toNormalized (kDrivePos, kDrivePost));
        out.clear ();
        rig.render (0.5, out, nullptr, tone (110.0, 0.4));
        const double h3Hp = toneDb (out, 330.0, 12000, 24000);
        rig.param (kHpDriveOn, 0.0);
        rig.param (kLpDriveOn, 1.0);
        out.clear ();
        rig.render (0.5, out, nullptr, tone (110.0, 0.4));
        const double h3On = toneDb (out, 330.0, 12000, 24000);
        CHECK (h3On > -40.0 && h3On > h3Off + 20.0 && h3On > h3Hp + 20.0, "low-pass drive: third harmonic %.1f dB (off %.1f, high-pass drive %.1f)",
               h3On, h3Off, h3Hp);
        CHECK (rig.processor->getLatencySamples () == lat0, "the drive keeps the latency: %u vs %u",
               (unsigned)rig.processor->getLatencySamples (), (unsigned)lat0);

        // a project from Para 0.6 (18 dB, the one drive on at +12 dB): the slope stays 18 dB and both
        // filters get the drive
        CHECK (rig.applyState ([] (IBStream* s) {
                   return writeOldState (s, {{kSlope, 0.5}, {kHpDriveOn, 1.0}, {kHpDrive, toNormalized (kHpDrive, 12.0)}});
               }),
               "old setState");
        CHECK (std::lround (plainOf (rig, kSlope)) == kSlope18, "the old 18 dB: %s", paramTable ().toText (kSlope, plainOf (rig, kSlope)).c_str ());
        CHECK (plainOf (rig, kLpDriveOn) >= 0.5 && std::fabs (plainOf (rig, kLpDrive) - 12.0) < 1e-6 && plainOf (rig, kHpDriveOn) >= 0.5,
               "the old drive in both filters: %.1f dB", plainOf (rig, kLpDrive));
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState again");

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
            CHECK (win.savePng (outDir + "/ui_para.png"), "screenshot");
            // the high-pass handle sits at its cutoff
            auto xOfHz = [] (double hz) {
                return Editor::kViewLeft + std::log (hz / 20.0) / std::log (1000.0) * (Editor::kViewRight - Editor::kViewLeft);
            };
            auto yOfDb = [] (double db) {
                return Editor::kViewTop + (18.0 - db) / 54.0 * (Editor::kViewBottom - Editor::kViewTop - 16.0);
            };
            const double hx = xOfHz (plainOf (rig, kHpFreq)), hy = yOfDb (0.0);
            const double f0 = plainOf (rig, kHpFreq);
            win.drag (hx, hy, hx + 40, hy);
            CHECK (plainOf (rig, kHpFreq) > f0 * 1.3, "drag raises the high-pass: %.0f -> %.0f", f0, plainOf (rig, kHpFreq));
            // a handle sits as high as its gain plus its resonant peak (the slope's response at the cutoff)
            auto handleY = [&] () {
                const double peak = std::abs (filterResponse (kSlope24, true, 1.0, 1.0, plainOf (rig, kHpRes)));
                return yOfDb (plainOf (rig, kHpGain) + 20.0 * std::log10 (std::max (1.0, peak)));
            };
            // up / down: the resonance, the gain stays
            const double hx2 = xOfHz (plainOf (rig, kHpFreq));
            win.drag (hx2, handleY (), hx2, handleY () - 30);
            CHECK (plainOf (rig, kHpRes) > 0.15, "drag up raises the high-pass resonance: %.2f", plainOf (rig, kHpRes));
            CHECK (std::fabs (plainOf (rig, kHpGain)) < 1e-6, "the gain stays: %.1f dB", plainOf (rig, kHpGain));
            // with Drag Gain on, the gain rises with it
            rig.param (kDragGain, 1.0);
            const double res1 = plainOf (rig, kHpRes);
            const double y1 = handleY ();
            win.drag (hx2, y1, hx2, y1 - 20);
            CHECK (plainOf (rig, kHpGain) > 3.0 && plainOf (rig, kHpRes) > res1, "Drag Gain: gain %.1f dB, resonance %.2f",
                   plainOf (rig, kHpGain), plainOf (rig, kHpRes));
            CHECK (win.savePng (outDir + "/ui_para_drag.png"), "drag screenshot");
            win.click (xOfHz (plainOf (rig, kHpFreq)), handleY (), 2);
            CHECK (std::fabs (plainOf (rig, kHpFreq) - 300.0) < 1e-6 && std::fabs (plainOf (rig, kHpGain)) < 1e-6 &&
                       plainOf (rig, kHpRes) < 1e-6,
                   "double-click resets");
            // Vocal: the low-pass swept above the high-pass pushes it along and fades it
            rig.param (kMovement, toNormalized (kMovement, kVocal));
            rig.param (kLpFreq, toNormalized (kLpFreq, 1200.0));
            for (int i = 0; i < 10; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, tone (110.0, 0.4));
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_para_vocal.png"), "vocal screenshot");
            // the drive toggles in OUTPUT (at 8, 410): the high-pass's, then the low-pass's
            rig.param (kHpDriveOn, 0.0);
            rig.param (kLpDriveOn, 0.0);
            pump (0.05);
            win.click (8 + 537, 410 + 79);
            CHECK (plainOf (rig, kHpDriveOn) >= 0.5 && plainOf (rig, kLpDriveOn) < 0.5, "HP drive toggle");
            win.click (8 + 585, 410 + 79);
            CHECK (plainOf (rig, kLpDriveOn) >= 0.5, "LP drive toggle");
            // the slopes' drop-down (in SPLIT, at 384, 298) shows the slope; set to Brickwall, the display
            // draws it
            rig.param (kSlope, toNormalized (kSlope, kSlopeBrickwall));
            rig.param (kMovement, toNormalized (kMovement, kFree));
            for (int i = 0; i < 10; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, tone (110.0, 0.4));
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_para_brickwall.png"), "brickwall screenshot");
        }
        rig.stop ();
        return finish ("para host test");
    }
}
