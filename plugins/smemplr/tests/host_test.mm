// End-to-end test of the built Smemplr.vst3 through the VST3 hosting API:
// loads the bundle, restores a state that references a sample, plays MIDI through process(),
// checks the audio, round-trips the state and renders the editor into offscreen windows.
//
// usage: smemplr_hosttest <path/to/Smemplr.vst3> <output dir for screenshots>

#include "Params.h"
#include "Rack.h"
#include "plugin/StateIO.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/Editor.h"

#include "dr_wav.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <set>
#include <fstream>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
#define CHECK PK_CHECK

static constexpr double kSr = 48000.0;
static constexpr int kBlock = 480;

// A 2-bar 120 BPM loop (4 s): kick/snare/hat bursts over a bass line.
static std::string writeTestLoop (const std::string& path)
{
    const int sr = 44100, n = sr * 4;
    std::vector<float> inter ((size_t)n * 2, 0.0f);
    uint32_t seed = 1;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (float)((seed >> 8) & 0xFFFF) / 32768.0f - 1.0f;
    };
    for (int step = 0; step < 16; ++step)
    {
        const int s0 = step * sr / 4; // eighth notes at 120 BPM
        for (int i = 0; i < sr / 4 && s0 + i < n; ++i)
        {
            const float t = (float)i / sr;
            float v = 0.0f;
            if (step % 4 == 0)
                v += 0.8f * std::sin (2.0f * (float)M_PI * (50.0f + 120.0f * std::exp (-t * 30.0f)) * t) * std::exp (-t * 9.0f);
            if (step % 4 == 2)
                v += 0.5f * rnd () * std::exp (-t * 18.0f);
            v += 0.15f * rnd () * std::exp (-t * 60.0f);
            v += 0.2f * std::sin (2.0f * (float)M_PI * (step < 8 ? 55.0f : 73.4f) * (float)(s0 + i) / sr);
            inter[(size_t)(s0 + i) * 2] += v;
            inter[(size_t)(s0 + i) * 2 + 1] += v * 0.9f;
        }
    }
    drwav_data_format fmt {};
    fmt.container = drwav_container_riff;
    fmt.format = DR_WAVE_FORMAT_PCM;
    fmt.channels = 2;
    fmt.sampleRate = (drwav_uint32)sr;
    fmt.bitsPerSample = 16;
    drwav w;
    if (!drwav_init_file_write (&w, path.c_str (), &fmt, nullptr))
        return {};
    std::vector<int16_t> pcm (inter.size ());
    for (size_t i = 0; i < inter.size (); ++i)
        pcm[i] = (int16_t)std::lround (std::clamp (inter[i], -1.0f, 1.0f) * 30000.0f);
    drwav_write_pcm_frames (&w, (drwav_uint64)n, pcm.data ());
    drwav_uninit (&w);
    return path;
}

static bool applyState (Rig& rig, const smemplr::PluginState& st)
{
    return rig.applyState ([&] (IBStream* s) { return smemplr::writeState (s, st); });
}

static smemplr::PluginState baseState (const std::string& sample)
{
    smemplr::PluginState st;
    for (uint32_t i = 0; i < smemplr::kNumParams; ++i)
    {
        st.norm[i] = smemplr::defaultNormalized (i);
        st.has[i] = true;
    }
    st.samplePath = sample;
    auto set = [&] (uint32_t id, double plain) { st.norm[id] = smemplr::toNormalized (id, plain); };
    set (smemplr::kVolume, 0.0);
    set (smemplr::kWarpBeats, 8);
    return st;
}

static bool snapshot (Rig& rig, const std::string& file)
{
    EditorWindow w (rig.controller);
    return w.ok () && w.savePng (file);
}

static void uiInteraction (Rig& rig)
{
    EditorWindow win (rig.controller);
    CHECK (win.ok (), "editor attach");
    auto plain = [&] (uint32_t id) { return smemplr::toPlain (id, rig.controller->getParamNormalized (id)); };

    // mode selector: click "Slicing", then "One-Shot"
    win.click (480 + 260 * 2.5 / 3, 17);
    CHECK (std::lround (plain (smemplr::kMode)) == smemplr::kModeSlicing, "click Slicing -> mode %f", plain (smemplr::kMode));
    win.click (480 + 260 * 1.5 / 3, 17);
    CHECK (std::lround (plain (smemplr::kMode)) == smemplr::kModeOneShot, "click One-Shot -> mode %f", plain (smemplr::kMode));
    win.click (480 + 260 * 0.5 / 3, 17);

    // warp toggle
    const double warpBefore = plain (smemplr::kWarp);
    win.click (780, 17);
    CHECK (plain (smemplr::kWarp) != warpBefore, "warp toggle didn't change");
    win.click (780, 17);

    // drag the filter frequency knob down by 50 px (knob at 16..72 x 624..688)
    const double before = rig.controller->getParamNormalized (smemplr::kFilterFreq);
    win.mouseDown (44, 650);
    for (int i = 1; i <= 10; ++i)
        win.mouseDrag (44, 650 + i * 5);
    win.mouseUp (44, 700);
    const double after = rig.controller->getParamNormalized (smemplr::kFilterFreq);
    CHECK (std::fabs ((before - after) - 0.25) < 0.02, "knob drag: %f -> %f", before, after);
    // double-click resets to the default
    win.click (44, 650, 1);
    win.mouseDown (44, 650, 2);
    win.mouseUp (44, 650, 2);
    CHECK (std::fabs (rig.controller->getParamNormalized (smemplr::kFilterFreq) - smemplr::defaultNormalized (smemplr::kFilterFreq)) < 1e-6,
           "double-click reset: %f", rig.controller->getParamNormalized (smemplr::kFilterFreq));

    // the Transpose high-pass on the Global panel (at 924, 410), under Transpose: HP (4..38 x 232..250),
    // its frequency (42..104) and slope (108..174)
    const bool hpWasOn = plain (smemplr::kTransHpOn) >= 0.5; // (the loop screenshot's state has it on)
    win.click (924 + 21, 410 + 241);
    CHECK ((plain (smemplr::kTransHpOn) >= 0.5) != hpWasOn, "clicking HP should switch it");
    const double hpBefore = rig.controller->getParamNormalized (smemplr::kTransHpFreq);
    win.mouseDown (924 + 73, 410 + 241);
    for (int i = 1; i <= 10; ++i)
        win.mouseDrag (924 + 73, 410 + 241 - i * 4);
    win.mouseUp (924 + 73, 410 + 201);
    CHECK (rig.controller->getParamNormalized (smemplr::kTransHpFreq) > hpBefore, "dragging the HP frequency up: %f -> %f",
           hpBefore, rig.controller->getParamNormalized (smemplr::kTransHpFreq));
    win.click (924 + 73, 410 + 241, 1);
    win.mouseDown (924 + 73, 410 + 241, 2);
    win.mouseUp (924 + 73, 410 + 241, 2);
    CHECK (std::fabs (rig.controller->getParamNormalized (smemplr::kTransHpFreq) - smemplr::defaultNormalized (smemplr::kTransHpFreq)) < 1e-6,
           "double-click resets the HP frequency: %f", rig.controller->getParamNormalized (smemplr::kTransHpFreq));
    win.click (924 + 21, 410 + 241);
    CHECK ((plain (smemplr::kTransHpOn) >= 0.5) == hpWasOn, "clicking HP again should switch it back");

    // --- loop bar (Classic, state has loop on, flags 0.1..0.8, start 10 %, length 32 %) ---
    // the loop begins at Start and Length is its length (a share of the flagged region)
    auto wx = [] (double pos) { return 8.0 + pos * 1094.0; };
    const double rs = 0.1 + 0.1 * 0.7, le = rs + 0.32 * 0.7;
    const double barX = wx ((rs + le) / 2), barY = 285;
    CHECK (plain (smemplr::kLoopOn) >= 0.5, "loop should start on");
    win.click (barX, barY);
    CHECK (plain (smemplr::kLoopOn) < 0.5, "clicking the loop bar should switch looping off");
    win.click (barX, barY);
    CHECK (plain (smemplr::kLoopOn) >= 0.5, "clicking again should switch it back on");
    const double lengthBefore = plain (smemplr::kLength);
    win.mouseDown (barX, barY);
    win.mouseDrag (barX + 40, barY);
    win.mouseDrag (barX + 84, barY);
    win.mouseUp (barX + 84, barY);
    const double newRs = 0.1 + plain (smemplr::kStart) * 0.7;
    CHECK (std::fabs (newRs - (rs + 84.0 / 1094.0)) < 0.004, "loop drag moved Start to %f (want %f)", newRs,
           rs + 84.0 / 1094.0);
    CHECK (std::fabs (plain (smemplr::kLength) - lengthBefore) < 0.004, "the loop keeps its length while moving (%f vs %f)",
           plain (smemplr::kLength), lengthBefore);
    CHECK (plain (smemplr::kLoopOn) >= 0.5, "dragging must not toggle the loop");

    // --- envelope display: shift-drag bends a curve, double-click adds / removes breakpoints ---
    // amp envelope plot area in the editor: x 402..688, y 476..606
    const double ax = 402, aw = 286, atop = 476, ah = 130;
    auto envGeom = [&] (double& peakX, double& decayEndX, double& holdEndX, double& relEndX, std::vector<double>& ptX) {
        auto w = [] (double ms) { return std::log10 (1.0 + ms / 2.0) + 0.05; };
        const int n = (int)std::lround (plain (smemplr::envParam (0, smemplr::kEnvPointCount)));
        double units = w (plain (smemplr::kAmpA)) + w (plain (smemplr::kAmpD)) + 1.2 + w (plain (smemplr::kAmpR));
        for (int i = 0; i < n; ++i)
            units += w (plain (smemplr::envPointParam (0, i, smemplr::kPtTime)));
        const double sc = aw / units;
        double x = ax + w (plain (smemplr::kAmpA)) * sc;
        peakX = x;
        ptX.clear ();
        for (int i = 0; i < n; ++i)
        {
            x += w (plain (smemplr::envPointParam (0, i, smemplr::kPtTime))) * sc;
            ptX.push_back (x);
        }
        decayEndX = x + w (plain (smemplr::kAmpD)) * sc;
        holdEndX = decayEndX + 1.2 * sc;
        relEndX = holdEndX + w (plain (smemplr::kAmpR)) * sc;
    };
    double peakX, decayX, holdX, relX;
    std::vector<double> ptX;
    envGeom (peakX, decayX, holdX, relX, ptX);
    const double curveBefore = plain (smemplr::envParam (0, smemplr::kEnvCurveR));
    const double rx = (holdX + relX) / 2, ry = atop + ah * 0.6;
    win.mouseDown (rx, ry, 1, kShift);
    win.mouseDrag (rx, ry - 20, kShift);
    win.mouseDrag (rx, ry - 40, kShift);
    win.mouseUp (rx, ry - 40, 1, kShift);
    const double curveAfter = plain (smemplr::envParam (0, smemplr::kEnvCurveR));
    CHECK (curveAfter > curveBefore + 0.3, "shift-drag up on the release should bow it up: %f -> %f", curveBefore, curveAfter);

    const double addX = peakX + (decayX - peakX) * 0.45, addY = atop + ah * 0.5;
    win.mouseDown (addX, addY, 2);
    win.mouseUp (addX, addY, 2);
    CHECK (std::lround (plain (smemplr::envParam (0, smemplr::kEnvPointCount))) == 1, "double-click should add a breakpoint (count %f)",
           plain (smemplr::envParam (0, smemplr::kEnvPointCount)));
    CHECK (std::fabs (plain (smemplr::envPointParam (0, 0, smemplr::kPtLevel)) - 0.5) < 0.03, "breakpoint level %f",
           plain (smemplr::envPointParam (0, 0, smemplr::kPtLevel)));
    const double total = plain (smemplr::envPointParam (0, 0, smemplr::kPtTime)) + plain (smemplr::kAmpD);
    CHECK (std::fabs (total - 600.0) < 6.0, "splitting keeps the decay's total time: %f ms", total);
    envGeom (peakX, decayX, holdX, relX, ptX);
    if (!ptX.empty ())
    {
        const double py = atop + ah * (1.0 - plain (smemplr::envPointParam (0, 0, smemplr::kPtLevel)));
        win.mouseDown (ptX[0], py, 2);
        win.mouseUp (ptX[0], py, 2);
        CHECK (std::lround (plain (smemplr::envParam (0, smemplr::kEnvPointCount))) == 0, "double-click on it should remove it");
        CHECK (std::fabs (plain (smemplr::kAmpD) - 600.0) < 6.0, "removing merges the time back: %f", plain (smemplr::kAmpD));
    }

    // --- help toggle ---
    auto tipsOn = [&] {
        MemoryStream ms;
        rig.controller->getState (&ms);
        ms.seek (0, IBStream::kIBSeekSet, nullptr);
        double scale = 0;
        bool tips = false;
        int32 n = 0;
        ms.read (&scale, sizeof (scale), &n);
        ms.read (&tips, sizeof (tips), &n);
        return tips;
    };
    CHECK (tipsOn (), "help tooltips default to on");
    win.click (1033, 17);
    CHECK (!tipsOn (), "the ? button should switch help off");
    win.click (1033, 17);
    CHECK (tipsOn (), "and back on");

    // drag the sample start flag (waveform x 8..1102) to the middle
    const double flagX = 8.0 + plain (smemplr::kSampleStart) * 1094.0;
    win.mouseDown (flagX, 150);
    win.mouseDrag (200, 150);
    win.mouseDrag (555, 150);
    win.mouseUp (555, 150);
    CHECK (std::fabs (plain (smemplr::kSampleStart) - 0.5) < 0.02, "flag drag -> start %f", plain (smemplr::kSampleStart));

}

int main (int argc, char** argv)
{
    @autoreleasepool
    {
        if (argc < 3)
        {
            std::printf ("usage: %s <Smemplr.vst3> <outdir>\n", argv[0]);
            return 2;
        }
        initHost ();
        const std::string outDir = argv[2];
        const std::string wav = writeTestLoop (outDir + "/test_loop.wav");
        CHECK (!wav.empty (), "could not write test wav");


        Rig rig;
        CHECK (rig.load (argv[1]), "load failed");
        if (gFail)
            return 1;

        // --- controller surface ---------------------------------------------------
        const int32 count = rig.controller->getParameterCount ();
        CHECK (count == (int32)smemplr::kNumParams + 3, "param count %d", count);
        String128 str;
        rig.controller->getParamStringByValue (smemplr::kFilterFreq, 1.0, str);
        const std::string freqText = StringConvert::convert (std::u16string (reinterpret_cast<const char16_t*> (str)));
        CHECK (freqText == "22.00 kHz", "freq text '%s'", freqText.c_str ());
        ParamValue parsed = -1;
        rig.controller->getParamValueByString (smemplr::kFilterFreq, (TChar*)u"1 kHz", parsed);
        CHECK (std::fabs (smemplr::toPlain (smemplr::kFilterFreq, parsed) - 1000.0) < 1.0, "parse 1 kHz -> %f", parsed);
        FUnknownPtr<IMidiMapping> mm (rig.controller);
        ParamID pid = 0;
        CHECK (mm && mm->getMidiControllerAssignment (0, 3, kPitchBend, pid) == kResultTrue &&
                   pid == smemplr::kMidiPitchBend,
               "pitch bend mapping");
        int notAutomatable = 0;
        for (int32 i = 0; i < count; ++i)
        {
            ParameterInfo info {};
            rig.controller->getParameterInfo (i, info);
            if (!(info.flags & ParameterInfo::kCanAutomate))
                ++notAutomatable;
        }
        CHECK (notAutomatable == 0, "%d parameters are not automatable", notAutomatable);
        CHECK (mm && mm->getMidiControllerAssignment (0, 0, kCtrlSustainOnOff, pid) == kResultTrue &&
                   pid == smemplr::kMidiSustain,
               "sustain mapping");

        // --- restore state that references the sample -------------------------------
        auto st = baseState (wav);
        CHECK (applyState (rig, st), "setState");
        CHECK (rig.start (), "start processing");

        // Classic: note on/off
        std::vector<float> out;
        rig.note (60, 1.0f);
        rig.render (0.5, out);
        rig.note (60, 0.0f);
        rig.render (0.5, out);
        CHECK (allFinite (out), "non-finite output");
        CHECK (rms (out, 0, 24000) > 0.05, "classic silent: %f", rms (out, 0, 24000));
        CHECK (rms (out, 36000, 48000) < 1e-4, "classic didn't release: %f", rms (out, 36000, 48000));

        // Parameter automation through process(): close the filter
        rig.param (smemplr::kFilterType, smemplr::toNormalized (smemplr::kFilterType, smemplr::kHighpass));
        rig.param (smemplr::kFilterFreq, smemplr::toNormalized (smemplr::kFilterFreq, 6000.0));
        rig.param (smemplr::kFilterSlope, 1.0);
        out.clear ();
        rig.note (60, 1.0f);
        rig.render (0.5, out);
        rig.note (60, 0.0f);
        rig.render (0.2, out);
        const double closed = rms (out, 4800, 24000);
        rig.param (smemplr::kFilterType, 0.0);
        rig.param (smemplr::kFilterFreq, 1.0);
        rig.param (smemplr::kFilterSlope, 0.0);
        out.clear ();
        rig.note (60, 1.0f);
        rig.render (0.5, out);
        rig.note (60, 0.0f);
        rig.render (0.2, out);
        const double open = rms (out, 4800, 24000);
        CHECK (closed < open * 0.25, "filter automation: closed %f open %f", closed, open);

        // Slicing: 16 region slices, note C1+4 plays the 5th slice only
        rig.param (smemplr::kMode, smemplr::toNormalized (smemplr::kMode, smemplr::kModeSlicing));
        rig.param (smemplr::kSliceBy, smemplr::toNormalized (smemplr::kSliceBy, smemplr::kSliceRegion));
        rig.param (smemplr::kRegions, smemplr::toNormalized (smemplr::kRegions, 3));
        out.clear ();
        rig.render (0.05, out);
        out.clear ();
        rig.note (smemplr::kSliceBaseNote + 4, 1.0f);
        rig.render (0.6, out);
        const double sliceSecs = soundEnd (out, 1e-4f) / kSr;
        CHECK (sliceSecs > 0.15 && sliceSecs < 0.3, "slice length %f s (want ~0.25)", sliceSecs);

        // Warp: 8 beats over 4 s at 120 BPM; at 60 BPM a one-shot lasts 8 s
        rig.param (smemplr::kMode, smemplr::toNormalized (smemplr::kMode, smemplr::kModeOneShot));
        rig.param (smemplr::kWarp, 1.0);
        rig.param (smemplr::kWarpMode, smemplr::toNormalized (smemplr::kWarpMode, smemplr::kWarpComplex));
        rig.ctx.tempo = 60.0;
        out.clear ();
        rig.note (60, 1.0f);
        rig.render (9.0, out);
        const double warpSecs = soundEnd (out, 1e-3f) / kSr;
        CHECK (std::fabs (warpSecs - 8.0) < 0.3, "warped length %f s (want 8)", warpSecs);
        CHECK (allFinite (out), "warp non-finite");
        rig.ctx.tempo = 120.0;
        rig.param (smemplr::kWarp, 0.0);
        rig.param (smemplr::kMode, 0.0);

        // Sustain pedal via MIDI-mapped parameter
        rig.param (smemplr::kLoopOn, 1.0);
        out.clear ();
        rig.param (smemplr::kMidiSustain, 1.0);
        rig.note (60, 1.0f);
        rig.render (0.1, out);
        rig.note (60, 0.0f);
        rig.render (0.4, out);
        CHECK (rms (out, 19200, 24000) > 0.02, "sustain pedal didn't hold");
        rig.param (smemplr::kMidiSustain, 0.0);
        rig.render (0.3, out);
        CHECK (rms (out, out.size () - 4800, out.size ()) < 1e-4, "pedal up didn't release");
        rig.param (smemplr::kLoopOn, 0.0);

        // Polyphony + CPU
        rig.param (smemplr::kLoopOn, 1.0);
        const auto t0 = std::chrono::steady_clock::now ();
        for (int i = 0; i < 16; ++i)
            rig.note (36 + i * 3, 0.8f);
        out.clear ();
        rig.render (2.0, out);
        const double cpu = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count () / 2.0 * 100.0;
        std::printf ("  16-voice CPU through the plug-in: %.1f%% of one core\n", cpu);
        CHECK (allFinite (out), "poly non-finite");
        CHECK (cpu < 30.0, "too slow: %f%%", cpu);
        for (int i = 0; i < 16; ++i)
            rig.note (36 + i * 3, 0.0f);
        rig.render (0.2, out);
        rig.param (smemplr::kLoopOn, 0.0);

        // --- state round trip ----------------------------------------------------
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        smemplr::PluginState back;
        CHECK (smemplr::readState (&saved, back), "readState of plug-in output");
        CHECK (back.samplePath == wav, "path '%s'", back.samplePath.c_str ());
        CHECK (std::fabs (back.norm[smemplr::kVolume] - st.norm[smemplr::kVolume]) < 1e-9, "volume not saved");
        CHECK (std::fabs (back.norm[smemplr::kRegions] - smemplr::toNormalized (smemplr::kRegions, 3)) < 1e-9,
               "automated param not saved");

        // States from 0.1.x (no loop-fade parameter) migrate the old flag
        {
            auto old = baseState (wav);
            old.has[smemplr::kLoopFadePower] = false;
            for (uint32_t id = smemplr::kEnvExtBase; id < smemplr::kNumParams; ++id)
                old.has[id] = false;
            old.constantPowerFade = false;
            rig.stop ();
            CHECK (applyState (rig, old), "old-format state");
            MemoryStream s3;
            rig.component->getState (&s3);
            s3.seek (0, IBStream::kIBSeekSet, nullptr);
            smemplr::PluginState b3;
            smemplr::readState (&s3, b3);
            CHECK (b3.norm[smemplr::kLoopFadePower] == 0.0, "linear fade should migrate: %f", b3.norm[smemplr::kLoopFadePower]);
            CHECK (std::fabs (b3.norm[smemplr::envParam (0, smemplr::kEnvCurveD)] -
                              smemplr::defaultNormalized (smemplr::envParam (0, smemplr::kEnvCurveD))) < 1e-9,
                   "missing new params should get defaults");
            rig.start ();
        }

        // States from 0.8 (version 8): the saturator after the rack, on there, goes into the rack after the
        // last effect with its settings (and is off after the rack); the M/S EQ's slope was one of three
        {
            using smemplr::kSlotType;
            auto v8 = baseState (wav);
            auto put = [&] (uint32_t id, double plain) { v8.norm[id] = smemplr::toNormalized (id, plain); };
            // (version 8 stored the slot type over the kinds before Levlr)
            auto putType = [&] (int slot, int type) {
                v8.norm[smemplr::slotParam (slot, kSlotType)] = (double)type / (smemplr::kFxTypesBeforeLevlr - 1);
            };
            for (int s = 0; s < smemplr::kRackSlots; ++s)
                putType (s, smemplr::kFxEmpty);
            putType (0, smemplr::kFxPara);
            putType (1, smemplr::kFxMsEq);
            v8.norm[smemplr::slotBlockParam (1, smemplr::mseq::kSlope)] = 0.5; // 12 dB of 6 / 12 / 24
            v8.norm[smemplr::kTailBase + pk::kTailOn] = 1.0;
            put (smemplr::kTailBase + pk::kTailDrive, 6.0);
            MemoryStream raw;
            CHECK (smemplr::writeState (&raw, v8), "write a state");
            const int32 eight = 8; // (the version, after the magic number)
            std::memcpy (raw.getData () + 4, &eight, sizeof (eight));
            raw.seek (0, IBStream::kIBSeekSet, nullptr);
            smemplr::PluginState b8;
            CHECK (smemplr::readState (&raw, b8), "read a version 8 state");
            auto typeIn = [&] (int s) {
                return (int)std::lround (smemplr::toPlain (smemplr::slotParam (s, kSlotType), b8.norm[smemplr::slotParam (s, kSlotType)]));
            };
            CHECK (typeIn (0) == smemplr::kFxPara && typeIn (1) == smemplr::kFxMsEq && typeIn (2) == smemplr::kFxSmacheratr &&
                       typeIn (3) == smemplr::kFxEmpty && b8.norm[smemplr::kTailBase + pk::kTailOn] == 0.0,
                   "version 8: the end saturator in the third slot: %d %d %d %d", typeIn (0), typeIn (1), typeIn (2), typeIn (3));
            const double slope = smemplr::mseq::paramTable ().toPlain (smemplr::mseq::kSlope, b8.norm[smemplr::slotBlockParam (1, smemplr::mseq::kSlope)]);
            const double drive = smacheratr::paramTable ().toPlain (smacheratr::kDrive, b8.norm[smemplr::slotBlockParam (2, smacheratr::kDrive)]);
            CHECK (std::lround (slope) == smemplr::MsEq::k12 && std::fabs (drive - 6.0) < 1e-6, "slope %f (12 dB), drive %f", slope, drive);
            // the old saturator never had Gently's Advanced mode: its slot gets the defaults
            for (uint32_t id = smacheratr::kClarityAdvanced; id <= smacheratr::kClaritySubThreshold; ++id)
                CHECK (b8.norm[smemplr::slotBlockParam (2, id)] == smacheratr::defaultNormalized (id), "version 8: %s at its default",
                       smacheratr::paramTable ().info (id).name);
        }

        // States from before version 11: Gently's Advanced places in a Smacheratr slot held nothing that
        // was used; they get their defaults (Advanced off: the same sound)
        {
            auto v10 = baseState (wav);
            const uint32_t typeId = smemplr::slotParam (0, smemplr::kSlotType);
            v10.norm[typeId] = smemplr::toNormalized (typeId, smemplr::kFxSmacheratr);
            for (uint32_t id = smacheratr::kClarityAdvanced; id <= smacheratr::kClaritySubThreshold; ++id)
                v10.norm[smemplr::slotBlockParam (0, id)] = 1.0;
            MemoryStream raw;
            CHECK (smemplr::writeState (&raw, v10), "write a state");
            const int32 ten = 10;
            std::memcpy (raw.getData () + 4, &ten, sizeof (ten));
            raw.seek (0, IBStream::kIBSeekSet, nullptr);
            smemplr::PluginState b10;
            CHECK (smemplr::readState (&raw, b10), "read a version 10 state");
            for (uint32_t id = smacheratr::kClarityAdvanced; id <= smacheratr::kClaritySubThreshold; ++id)
                CHECK (b10.norm[smemplr::slotBlockParam (0, id)] == smacheratr::defaultNormalized (id), "version 10: %s at its default (%f)",
                       smacheratr::paramTable ().info (id).name, b10.norm[smemplr::slotBlockParam (0, id)]);
        }

        // Missing sample: loads without crashing, path is preserved
        auto missing = baseState ("/nonexistent/folder/gone.wav");
        rig.stop ();
        CHECK (applyState (rig, missing), "setState with missing file");
        MemoryStream saved2;
        rig.component->getState (&saved2);
        saved2.seek (0, IBStream::kIBSeekSet, nullptr);
        smemplr::PluginState back2;
        smemplr::readState (&saved2, back2);
        CHECK (back2.samplePath == missing.samplePath, "missing path lost: '%s'", back2.samplePath.c_str ());
        CHECK (rig.start (), "restart");
        out.clear ();
        rig.note (60, 1.0f);
        rig.render (0.2, out);
        CHECK (rms (out, 0, out.size ()) == 0.0, "no sample should be silent");
        rig.note (60, 0.0f);
        rig.stop ();

        // --- editor screenshots --------------------------------------------------
        auto st2 = baseState (wav);
        applyState (rig, st2);
        CHECK (rig.start (), "restart 2");
        rig.note (60, 1.0f);
        rig.render (0.3, out);
        CHECK (snapshot (rig, outDir + "/ui_classic.png"), "classic snapshot");

        // the effects rack with audio flowing: every kind of effect in a slot, one screenshot per tab
        using smemplr::slotParam;
        auto loadFx = [&] (int slot, int type) {
            rig.param (slotParam (slot, smemplr::kSlotType), smemplr::toNormalized (slotParam (slot, smemplr::kSlotType), type));
            rig.param (slotParam (slot, smemplr::kSlotOn), 1.0);
            const auto& t = smemplr::fxBlockTable (type);
            for (uint32_t j = 0; j < smemplr::kSlotBlockAll; ++j)
                rig.param (smemplr::slotBlockParam (slot, j), j < t.size () ? t.defaultNormalized (j) : 0.0);
        };
        constexpr int kKinds = 7;
        const int kinds[kKinds] = {smemplr::kFxPara, smemplr::kFxMultidyn, smemplr::kFxMsEq, smemplr::kFxSmacheratr,
                                   smemplr::kFxWidr, smemplr::kFxWubr,     smemplr::kFxLevlr};
        constexpr int kWubrSlot = 5;
        for (int s = 0; s < kKinds; ++s)
            loadFx (s, kinds[s]);
        // Wubr (slot 6) with a value in the slot's extension (block positions from kSlotBlock on): band 2's
        // third point's level; it must move with the slot when the editor moves the rack up
        const uint32_t wubrExt = (uint32_t)smemplr::fxBlockOf (smemplr::kFxWubr, wubr::pointParam (1, 2, wubr::kPtY));
        CHECK (wubrExt >= smemplr::kSlotBlock && wubrExt < smemplr::kSlotBlockAll, "band 2's point 3 level is in the extension: %u", wubrExt);
        rig.param (smemplr::slotBlockParam (kWubrSlot, wubrExt), 0.75);
        auto typeOf = [&] (int slot) {
            return (int)std::lround (smemplr::toPlain (slotParam (slot, smemplr::kSlotType),
                                                        rig.controller->getParamNormalized (slotParam (slot, smemplr::kSlotType))));
        };
        const std::string rackReport = outDir + "/rack_pages.txt";
        std::remove (rackReport.c_str ());
        setenv ("SMEMPLR_RACK_PAGE_REPORT", rackReport.c_str (), 1);
        {
            EditorWindow fx (rig.controller);
            CHECK (fx.ok (), "fx editor");
            auto settle = [&] {
                rig.note (60, 1.0f);
                for (int i = 0; i < 12; ++i)
                {
                    rig.render (0.05, out);
                    pump (0.03);
                }
                rig.note (60, 0.0f);
            };
            const double tabY = smemplr::Editor::kFxTabTop + 10, ctlY = smemplr::Editor::kFxCtlTop + 10;
            // one tab per slot in chain order (no saturator after the rack any more: a slot does that)
            const char* names[kKinds] = {"para", "multidyn", "ms", "smacheratr", "widr", "wubr", "levlr"};
            auto tabX = [] (int pos) { return 8 + pos * smemplr::Editor::kFxTabWidth + 40; };
            for (int t = 0; t < kKinds; ++t)
            {
                fx.click (tabX (t), tabY);
                settle ();
                CHECK (fx.savePng (outDir + "/ui_fx_" + names[t] + ".png"), "fx %s snapshot", names[t]);
                // every parameter of the effect has a control on its rack page, unless it is listed as
                // deliberately not shown (so the rack keeps up when an effect gains a parameter); the
                // editor reports its pages to the file in SMEMPLR_RACK_PAGE_REPORT
                {
                    std::set<uint32_t> shown;
                    int reported = -1;
                    std::ifstream rep (rackReport);
                    for (std::string line; std::getline (rep, line);)
                    {
                        std::istringstream ls (line);
                        int type = -1;
                        ls >> type;
                        if (type != kinds[t])
                            continue;
                        reported = type;
                        shown.clear ();
                        for (uint32_t id; ls >> id;)
                            shown.insert (id);
                    }
                    CHECK (reported == kinds[t], "rack %s: the page was reported", names[t]);
                    const auto& table = smemplr::fxTable (kinds[t]);
                    const auto& hidden = smemplr::rackHiddenParams (kinds[t]);
                    for (uint32_t id = 0; id < table.size (); ++id)
                    {
                        const bool listed = std::any_of (hidden.begin (), hidden.end (),
                                                         [id] (const smemplr::RackHidden& h) { return id >= h.first && id <= h.last; });
                        CHECK (listed || shown.count (id) == 1, "rack %s: \"%s\" (%u) has no control on the page", names[t],
                               table.info (id).name, id);
                    }
                }
            }
            // drag the second slot's tab onto the first: Multidyn first, then Para (the moved slot is selected)
            fx.drag (tabX (1), tabY, tabX (0), tabY);
            pump (0.1);
            CHECK (typeOf (0) == smemplr::kFxMultidyn && typeOf (1) == smemplr::kFxPara, "dragged: %d %d", typeOf (0), typeOf (1));
            // a drag of less than a few pixels is a click: nothing moves
            fx.drag (tabX (2), tabY, tabX (2) + 2, tabY);
            pump (0.1);
            CHECK (typeOf (1) == smemplr::kFxPara && typeOf (2) == smemplr::kFxMsEq, "a click does not move: %d %d", typeOf (1), typeOf (2));
            // and back: drag Multidyn two places right (Para, M/S EQ, Multidyn), then one left again
            fx.drag (tabX (0), tabY, tabX (2), tabY);
            pump (0.1);
            CHECK (typeOf (0) == smemplr::kFxPara && typeOf (1) == smemplr::kFxMsEq && typeOf (2) == smemplr::kFxMultidyn,
                   "dragged right: %d %d %d", typeOf (0), typeOf (1), typeOf (2));
            fx.drag (tabX (2), tabY, tabX (1), tabY);
            pump (0.1);
            CHECK (typeOf (0) == smemplr::kFxPara && typeOf (1) == smemplr::kFxMultidyn && typeOf (2) == smemplr::kFxMsEq,
                   "dragged left: %d %d %d", typeOf (0), typeOf (1), typeOf (2));
            // remove it (the moved slot is selected): Para, then the rest move up
            fx.click (8 + 208 + 34, ctlY); // Remove
            pump (0.1);
            CHECK (typeOf (0) == smemplr::kFxPara && typeOf (1) == smemplr::kFxMsEq && typeOf (4) == smemplr::kFxWubr &&
                       typeOf (5) == smemplr::kFxLevlr && typeOf (6) == smemplr::kFxEmpty,
                   "removed: %d %d %d %d %d", typeOf (0), typeOf (1), typeOf (4), typeOf (5), typeOf (6));
            // Wubr moved up a slot with its extension value, and the freed slot's extension is cleared
            const double moved = rig.controller->getParamNormalized (smemplr::slotBlockParam (kWubrSlot - 1, wubrExt));
            const double freed = rig.controller->getParamNormalized (smemplr::slotBlockParam (kKinds - 1, wubrExt));
            CHECK (std::fabs (moved - 0.75) < 1e-9 && freed == 0.0, "Wubr's extension moved with it: %f, left behind %f", moved, freed);
            settle ();
            CHECK (fx.savePng (outDir + "/ui_fx_after_remove.png"), "after remove snapshot");
            // Cmd-drag (Ctrl on Windows) Para two places right: a copy lands in that gap, the rest move up one
            fx.drag (tabX (0), tabY, tabX (2), tabY, kCmd);
            pump (0.1);
            CHECK (typeOf (0) == smemplr::kFxPara && typeOf (1) == smemplr::kFxMsEq && typeOf (2) == smemplr::kFxPara &&
                       typeOf (3) == smemplr::kFxSmacheratr && typeOf (6) == smemplr::kFxLevlr,
                   "duplicated: %d %d %d %d %d", typeOf (0), typeOf (1), typeOf (2), typeOf (3), typeOf (6));
            settle ();
            CHECK (fx.savePng (outDir + "/ui_fx_after_duplicate.png"), "after duplicate snapshot");
            // Alt-click (Option-click) the copy: it goes, the ones after it move up
            fx.click (tabX (2), tabY, 1, kAlt);
            pump (0.1);
            CHECK (typeOf (1) == smemplr::kFxMsEq && typeOf (2) == smemplr::kFxSmacheratr && typeOf (5) == smemplr::kFxLevlr &&
                       typeOf (6) == smemplr::kFxEmpty,
                   "alt-click removed it: %d %d %d %d", typeOf (1), typeOf (2), typeOf (5), typeOf (6));
        }
        for (int s = 0; s < smemplr::kRackSlots; ++s)
            rig.param (slotParam (s, smemplr::kSlotType), 0.0);
        rig.note (60, 0.0f);
        rig.render (0.3, out);

        // the kinds that did not fit the rack above (it has 8 slots): Gently and Smoothr, their pages (every
        // parameter with a control, or listed as not shown), and Copy / Paste between two Smoothr slots
        {
            loadFx (0, smemplr::kFxGently);
            loadFx (1, smemplr::kFxSmoothr);
            loadFx (2, smemplr::kFxSmoothr);
            const uint32_t ceil1 = smemplr::slotBlockParam (1, (uint32_t)smemplr::fxBlockOf (smemplr::kFxSmoothr, smoothr::kCeiling));
            const uint32_t ceil2 = smemplr::slotBlockParam (2, (uint32_t)smemplr::fxBlockOf (smemplr::kFxSmoothr, smoothr::kCeiling));
            rig.param (ceil1, 0.2);
            EditorWindow fx (rig.controller);
            CHECK (fx.ok (), "fx editor (gently, smoothr)");
            const double tabY = smemplr::Editor::kFxTabTop + 10, ctlY = smemplr::Editor::kFxCtlTop + 10;
            auto tabX = [] (int pos) { return 8 + pos * smemplr::Editor::kFxTabWidth + 40; };
            const int more[2] = {smemplr::kFxGently, smemplr::kFxSmoothr};
            const char* moreNames[2] = {"gently", "smoothr"};
            for (int t = 0; t < 2; ++t)
            {
                fx.click (tabX (t), tabY);
                rig.note (60, 1.0f);
                for (int i = 0; i < 12; ++i)
                {
                    rig.render (0.05, out);
                    pump (0.03);
                }
                rig.note (60, 0.0f);
                CHECK (fx.savePng (outDir + "/ui_fx_" + moreNames[t] + ".png"), "fx %s snapshot", moreNames[t]);
                std::set<uint32_t> shown;
                int reported = -1;
                std::ifstream rep (rackReport);
                for (std::string line; std::getline (rep, line);)
                {
                    std::istringstream ls (line);
                    int type = -1;
                    ls >> type;
                    if (type != more[t])
                        continue;
                    reported = type;
                    shown.clear ();
                    for (uint32_t id; ls >> id;)
                        shown.insert (id);
                }
                CHECK (reported == more[t], "rack %s: the page was reported", moreNames[t]);
                const auto& table = smemplr::fxTable (more[t]);
                const auto& hidden = smemplr::rackHiddenParams (more[t]);
                for (uint32_t id = 0; id < table.size (); ++id)
                {
                    const bool listed = std::any_of (hidden.begin (), hidden.end (),
                                                     [id] (const smemplr::RackHidden& h) { return id >= h.first && id <= h.last; });
                    CHECK (listed || shown.count (id) == 1, "rack %s: \"%s\" (%u) has no control on the page", moreNames[t],
                           table.info (id).name, id);
                }
            }
            // Copy on slot 2's page, Paste on slot 3's: the Ceiling comes along
            fx.click (tabX (1), tabY);
            pump (0.1);
            fx.click (8 + 284 + 26, ctlY); // Copy
            fx.click (tabX (2), tabY);
            pump (0.1);
            fx.click (8 + 340 + 26, ctlY); // Paste
            pump (0.1);
            CHECK (std::fabs (rig.controller->getParamNormalized (ceil2) - 0.2) < 1e-9, "pasted: the ceiling %f",
                   rig.controller->getParamNormalized (ceil2));
        }
        for (int s = 0; s < smemplr::kRackSlots; ++s)
            rig.param (slotParam (s, smemplr::kSlotType), 0.0);
        rig.render (0.3, out);

        auto set = [&] (uint32_t id, double plain) { rig.controller->setParamNormalized (id, smemplr::toNormalized (id, plain)); };
        auto st3 = baseState (wav);
        st3.norm[smemplr::kMode] = smemplr::toNormalized (smemplr::kMode, smemplr::kModeSlicing);
        st3.norm[smemplr::kWarp] = 1.0;
        st3.norm[smemplr::kFilterFreq] = smemplr::toNormalized (smemplr::kFilterFreq, 1800.0);
        st3.norm[smemplr::kFilterRes] = 0.6;
        st3.norm[smemplr::kFilterSlope] = 1.0;
        st3.norm[smemplr::kFilterCircuit] = smemplr::toNormalized (smemplr::kFilterCircuit, smemplr::kPRD);
        st3.norm[smemplr::kLfoOn] = 1.0;
        st3.norm[smemplr::kLfoPitch] = 0.2;
        st3.edits.manual.push_back (0.33);
        rig.stop ();
        applyState (rig, st3);
        rig.start ();
        (void)set;
        CHECK (snapshot (rig, outDir + "/ui_slicing.png"), "slicing snapshot");

        auto st4 = baseState (wav);
        st4.norm[smemplr::kLoopOn] = 1.0;
        st4.norm[smemplr::kSampleStart] = 0.1;
        st4.norm[smemplr::kSampleEnd] = 0.8;
        st4.norm[smemplr::kStart] = 0.1;
        st4.norm[smemplr::kLength] = 0.32;
        st4.norm[smemplr::kFilterType] = smemplr::toNormalized (smemplr::kFilterType, smemplr::kMorph);
        st4.norm[smemplr::kFilterMorph] = 0.35;
        st4.norm[smemplr::kFilterRes] = 0.5;
        st4.norm[smemplr::kFilterFreq] = smemplr::toNormalized (smemplr::kFilterFreq, 900.0);
        // the Transpose high-pass on, at 40 Hz and 36 dB (the Global panel shows it)
        st4.norm[smemplr::kTransHpOn] = 1.0;
        st4.norm[smemplr::kTransHpFreq] = smemplr::toNormalized (smemplr::kTransHpFreq, 40.0);
        st4.norm[smemplr::kTransHpSlope] = smemplr::toNormalized (smemplr::kTransHpSlope, smemplr::kTransHp36);
        rig.stop ();
        applyState (rig, st4);
        rig.start ();
        CHECK (snapshot (rig, outDir + "/ui_classic_loop.png"), "loop snapshot");
        rig.stop ();

        rig.start ();
        uiInteraction (rig);
        rig.stop ();

        // breakpoint envelope + loop-off screenshot
        {
            auto st5 = baseState (wav);
            auto setp = [&] (uint32_t id, double v) { st5.norm[id] = smemplr::toNormalized (id, v); };
            setp (smemplr::kAmpA, 30.0);
            setp (smemplr::kAmpD, 300.0);
            setp (smemplr::kAmpS, 0.45);
            setp (smemplr::kAmpR, 800.0);
            setp (smemplr::envParam (0, smemplr::kEnvCurveA), 0.6);
            setp (smemplr::envParam (0, smemplr::kEnvCurveR), -0.8);
            setp (smemplr::envParam (0, smemplr::kEnvPointCount), 2);
            setp (smemplr::envPointParam (0, 0, smemplr::kPtTime), 120.0);
            setp (smemplr::envPointParam (0, 0, smemplr::kPtLevel), 0.25);
            setp (smemplr::envPointParam (0, 0, smemplr::kPtCurve), -0.7);
            setp (smemplr::envPointParam (0, 1, smemplr::kPtTime), 200.0);
            setp (smemplr::envPointParam (0, 1, smemplr::kPtLevel), 0.8);
            setp (smemplr::envPointParam (0, 1, smemplr::kPtCurve), 0.5);
            setp (smemplr::kLoopOn, 0.0);
            setp (smemplr::kLength, 0.7);
            applyState (rig, st5);
            rig.start ();
            CHECK (snapshot (rig, outDir + "/ui_envelope_points.png"), "envelope snapshot");
            rig.stop ();
        }

        // editor resize constraint keeps the aspect ratio
        if (IPlugView* v = rig.controller->createView (ViewType::kEditor))
        {
            ViewRect r (0, 0, 1665, 900);
            CHECK (v->canResize () == kResultTrue, "resizable");
            v->checkSizeConstraint (&r);
            // the editor keeps its aspect ratio (1110 x 1012 with the effects rack)
            CHECK (std::abs (r.getWidth () * 1012 - r.getHeight () * 1110) < 1110, "aspect %dx%d", r.getWidth (),
                   r.getHeight ());
            v->release ();
        }

        rig.provider = nullptr;
        rig.module = nullptr;
        return finish ("host test");
    }
}
