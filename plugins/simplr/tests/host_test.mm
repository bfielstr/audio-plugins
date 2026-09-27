// End-to-end test of the built Simplr.vst3 through the VST3 hosting API:
// loads the bundle, restores a state that references a sample, plays MIDI through process(),
// checks the audio, round-trips the state and renders the editor into offscreen windows.
//
// usage: simplr_hosttest <path/to/Simplr.vst3> <output dir for screenshots>

#include "Params.h"
#include "plugin/StateIO.h"
#include "pluginkit/testing/HostRig.h"

#include "dr_wav.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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

static bool applyState (Rig& rig, const simplr::PluginState& st)
{
    return rig.applyState ([&] (IBStream* s) { return simplr::writeState (s, st); });
}

static simplr::PluginState baseState (const std::string& sample)
{
    simplr::PluginState st;
    for (uint32_t i = 0; i < simplr::kNumParams; ++i)
    {
        st.norm[i] = simplr::defaultNormalized (i);
        st.has[i] = true;
    }
    st.samplePath = sample;
    auto set = [&] (uint32_t id, double plain) { st.norm[id] = simplr::toNormalized (id, plain); };
    set (simplr::kVolume, 0.0);
    set (simplr::kWarpBeats, 8);
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
    auto plain = [&] (uint32_t id) { return simplr::toPlain (id, rig.controller->getParamNormalized (id)); };

    // mode selector: click "Slicing", then "One-Shot"
    win.click (480 + 260 * 2.5 / 3, 17);
    CHECK (std::lround (plain (simplr::kMode)) == simplr::kModeSlicing, "click Slicing -> mode %f", plain (simplr::kMode));
    win.click (480 + 260 * 1.5 / 3, 17);
    CHECK (std::lround (plain (simplr::kMode)) == simplr::kModeOneShot, "click One-Shot -> mode %f", plain (simplr::kMode));
    win.click (480 + 260 * 0.5 / 3, 17);

    // warp toggle
    const double warpBefore = plain (simplr::kWarp);
    win.click (780, 17);
    CHECK (plain (simplr::kWarp) != warpBefore, "warp toggle didn't change");
    win.click (780, 17);

    // drag the filter frequency knob down by 50 px (knob at 16..72 x 624..688)
    const double before = rig.controller->getParamNormalized (simplr::kFilterFreq);
    win.mouseDown (44, 650);
    for (int i = 1; i <= 10; ++i)
        win.mouseDrag (44, 650 + i * 5);
    win.mouseUp (44, 700);
    const double after = rig.controller->getParamNormalized (simplr::kFilterFreq);
    CHECK (std::fabs ((before - after) - 0.25) < 0.02, "knob drag: %f -> %f", before, after);
    // double-click resets to the default
    win.click (44, 650, 1);
    win.mouseDown (44, 650, 2);
    win.mouseUp (44, 650, 2);
    CHECK (std::fabs (rig.controller->getParamNormalized (simplr::kFilterFreq) - simplr::defaultNormalized (simplr::kFilterFreq)) < 1e-6,
           "double-click reset: %f", rig.controller->getParamNormalized (simplr::kFilterFreq));

    // --- loop bar (Classic, state has loop on, flags 0.1..0.8, start 10 %, length 80 %, loop 40 %) ---
    auto wx = [] (double pos) { return 8.0 + pos * 1094.0; };
    const double rs = 0.1 + 0.1 * 0.7, re = rs + 0.8 * 0.7, ls = re - 0.4 * (re - rs);
    const double barX = wx ((ls + re) / 2), barY = 285;
    CHECK (plain (simplr::kLoopOn) >= 0.5, "loop should start on");
    win.click (barX, barY);
    CHECK (plain (simplr::kLoopOn) < 0.5, "clicking the loop bar should switch looping off");
    win.click (barX, barY);
    CHECK (plain (simplr::kLoopOn) >= 0.5, "clicking again should switch it back on");
    const double loopFramesBefore = plain (simplr::kLoopLen) * (re - rs);
    win.mouseDown (barX, barY);
    win.mouseDrag (barX - 40, barY);
    win.mouseDrag (barX - 84, barY);
    win.mouseUp (barX - 84, barY);
    const double newRe = rs + plain (simplr::kLength) * 0.7;
    CHECK (std::fabs (newRe - (re - 84.0 / 1094.0)) < 0.004, "loop drag moved the end to %f (want %f)", newRe,
           re - 84.0 / 1094.0);
    CHECK (std::fabs (plain (simplr::kLoopLen) * (newRe - rs) - loopFramesBefore) < 0.004,
           "loop length should be kept while moving (%f vs %f)", plain (simplr::kLoopLen) * (newRe - rs), loopFramesBefore);
    CHECK (plain (simplr::kLoopOn) >= 0.5, "dragging must not toggle the loop");

    // --- envelope display: shift-drag bends a curve, double-click adds / removes breakpoints ---
    // amp envelope plot area in the editor: x 402..688, y 476..606
    const double ax = 402, aw = 286, atop = 476, ah = 130;
    auto envGeom = [&] (double& peakX, double& decayEndX, double& holdEndX, double& relEndX, std::vector<double>& ptX) {
        auto w = [] (double ms) { return std::log10 (1.0 + ms / 2.0) + 0.05; };
        const int n = (int)std::lround (plain (simplr::envParam (0, simplr::kEnvPointCount)));
        double units = w (plain (simplr::kAmpA)) + w (plain (simplr::kAmpD)) + 1.2 + w (plain (simplr::kAmpR));
        for (int i = 0; i < n; ++i)
            units += w (plain (simplr::envPointParam (0, i, simplr::kPtTime)));
        const double sc = aw / units;
        double x = ax + w (plain (simplr::kAmpA)) * sc;
        peakX = x;
        ptX.clear ();
        for (int i = 0; i < n; ++i)
        {
            x += w (plain (simplr::envPointParam (0, i, simplr::kPtTime))) * sc;
            ptX.push_back (x);
        }
        decayEndX = x + w (plain (simplr::kAmpD)) * sc;
        holdEndX = decayEndX + 1.2 * sc;
        relEndX = holdEndX + w (plain (simplr::kAmpR)) * sc;
    };
    double peakX, decayX, holdX, relX;
    std::vector<double> ptX;
    envGeom (peakX, decayX, holdX, relX, ptX);
    const double curveBefore = plain (simplr::envParam (0, simplr::kEnvCurveR));
    const double rx = (holdX + relX) / 2, ry = atop + ah * 0.6;
    win.mouseDown (rx, ry, 1, kShift);
    win.mouseDrag (rx, ry - 20, kShift);
    win.mouseDrag (rx, ry - 40, kShift);
    win.mouseUp (rx, ry - 40, 1, kShift);
    const double curveAfter = plain (simplr::envParam (0, simplr::kEnvCurveR));
    CHECK (curveAfter > curveBefore + 0.3, "shift-drag up on the release should bow it up: %f -> %f", curveBefore, curveAfter);

    const double addX = peakX + (decayX - peakX) * 0.45, addY = atop + ah * 0.5;
    win.mouseDown (addX, addY, 2);
    win.mouseUp (addX, addY, 2);
    CHECK (std::lround (plain (simplr::envParam (0, simplr::kEnvPointCount))) == 1, "double-click should add a breakpoint (count %f)",
           plain (simplr::envParam (0, simplr::kEnvPointCount)));
    CHECK (std::fabs (plain (simplr::envPointParam (0, 0, simplr::kPtLevel)) - 0.5) < 0.03, "breakpoint level %f",
           plain (simplr::envPointParam (0, 0, simplr::kPtLevel)));
    const double total = plain (simplr::envPointParam (0, 0, simplr::kPtTime)) + plain (simplr::kAmpD);
    CHECK (std::fabs (total - 600.0) < 6.0, "splitting keeps the decay's total time: %f ms", total);
    envGeom (peakX, decayX, holdX, relX, ptX);
    if (!ptX.empty ())
    {
        const double py = atop + ah * (1.0 - plain (simplr::envPointParam (0, 0, simplr::kPtLevel)));
        win.mouseDown (ptX[0], py, 2);
        win.mouseUp (ptX[0], py, 2);
        CHECK (std::lround (plain (simplr::envParam (0, simplr::kEnvPointCount))) == 0, "double-click on it should remove it");
        CHECK (std::fabs (plain (simplr::kAmpD) - 600.0) < 6.0, "removing merges the time back: %f", plain (simplr::kAmpD));
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
    const double flagX = 8.0 + plain (simplr::kSampleStart) * 1094.0;
    win.mouseDown (flagX, 150);
    win.mouseDrag (200, 150);
    win.mouseDrag (555, 150);
    win.mouseUp (555, 150);
    CHECK (std::fabs (plain (simplr::kSampleStart) - 0.5) < 0.02, "flag drag -> start %f", plain (simplr::kSampleStart));

}

int main (int argc, char** argv)
{
    @autoreleasepool
    {
        if (argc < 3)
        {
            std::printf ("usage: %s <Simplr.vst3> <outdir>\n", argv[0]);
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
        CHECK (count == (int32)simplr::kNumParams + 3, "param count %d", count);
        String128 str;
        rig.controller->getParamStringByValue (simplr::kFilterFreq, 1.0, str);
        const std::string freqText = StringConvert::convert (std::u16string (reinterpret_cast<const char16_t*> (str)));
        CHECK (freqText == "22.00 kHz", "freq text '%s'", freqText.c_str ());
        ParamValue parsed = -1;
        rig.controller->getParamValueByString (simplr::kFilterFreq, (TChar*)u"1 kHz", parsed);
        CHECK (std::fabs (simplr::toPlain (simplr::kFilterFreq, parsed) - 1000.0) < 1.0, "parse 1 kHz -> %f", parsed);
        FUnknownPtr<IMidiMapping> mm (rig.controller);
        ParamID pid = 0;
        CHECK (mm && mm->getMidiControllerAssignment (0, 3, kPitchBend, pid) == kResultTrue &&
                   pid == simplr::kMidiPitchBend,
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
                   pid == simplr::kMidiSustain,
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
        rig.param (simplr::kFilterType, simplr::toNormalized (simplr::kFilterType, simplr::kHighpass));
        rig.param (simplr::kFilterFreq, simplr::toNormalized (simplr::kFilterFreq, 6000.0));
        rig.param (simplr::kFilterSlope, 1.0);
        out.clear ();
        rig.note (60, 1.0f);
        rig.render (0.5, out);
        rig.note (60, 0.0f);
        rig.render (0.2, out);
        const double closed = rms (out, 4800, 24000);
        rig.param (simplr::kFilterType, 0.0);
        rig.param (simplr::kFilterFreq, 1.0);
        rig.param (simplr::kFilterSlope, 0.0);
        out.clear ();
        rig.note (60, 1.0f);
        rig.render (0.5, out);
        rig.note (60, 0.0f);
        rig.render (0.2, out);
        const double open = rms (out, 4800, 24000);
        CHECK (closed < open * 0.25, "filter automation: closed %f open %f", closed, open);

        // Slicing: 16 region slices, note C1+4 plays the 5th slice only
        rig.param (simplr::kMode, simplr::toNormalized (simplr::kMode, simplr::kModeSlicing));
        rig.param (simplr::kSliceBy, simplr::toNormalized (simplr::kSliceBy, simplr::kSliceRegion));
        rig.param (simplr::kRegions, simplr::toNormalized (simplr::kRegions, 3));
        out.clear ();
        rig.render (0.05, out);
        out.clear ();
        rig.note (simplr::kSliceBaseNote + 4, 1.0f);
        rig.render (0.6, out);
        const double sliceSecs = soundEnd (out, 1e-4f) / kSr;
        CHECK (sliceSecs > 0.15 && sliceSecs < 0.3, "slice length %f s (want ~0.25)", sliceSecs);

        // Warp: 8 beats over 4 s at 120 BPM; at 60 BPM a one-shot lasts 8 s
        rig.param (simplr::kMode, simplr::toNormalized (simplr::kMode, simplr::kModeOneShot));
        rig.param (simplr::kWarp, 1.0);
        rig.param (simplr::kWarpMode, simplr::toNormalized (simplr::kWarpMode, simplr::kWarpComplex));
        rig.ctx.tempo = 60.0;
        out.clear ();
        rig.note (60, 1.0f);
        rig.render (9.0, out);
        const double warpSecs = soundEnd (out, 1e-3f) / kSr;
        CHECK (std::fabs (warpSecs - 8.0) < 0.3, "warped length %f s (want 8)", warpSecs);
        CHECK (allFinite (out), "warp non-finite");
        rig.ctx.tempo = 120.0;
        rig.param (simplr::kWarp, 0.0);
        rig.param (simplr::kMode, 0.0);

        // Sustain pedal via MIDI-mapped parameter
        rig.param (simplr::kLoopOn, 1.0);
        out.clear ();
        rig.param (simplr::kMidiSustain, 1.0);
        rig.note (60, 1.0f);
        rig.render (0.1, out);
        rig.note (60, 0.0f);
        rig.render (0.4, out);
        CHECK (rms (out, 19200, 24000) > 0.02, "sustain pedal didn't hold");
        rig.param (simplr::kMidiSustain, 0.0);
        rig.render (0.3, out);
        CHECK (rms (out, out.size () - 4800, out.size ()) < 1e-4, "pedal up didn't release");
        rig.param (simplr::kLoopOn, 0.0);

        // Polyphony + CPU
        rig.param (simplr::kLoopOn, 1.0);
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
        rig.param (simplr::kLoopOn, 0.0);

        // --- state round trip ----------------------------------------------------
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        simplr::PluginState back;
        CHECK (simplr::readState (&saved, back), "readState of plug-in output");
        CHECK (back.samplePath == wav, "path '%s'", back.samplePath.c_str ());
        CHECK (std::fabs (back.norm[simplr::kVolume] - st.norm[simplr::kVolume]) < 1e-9, "volume not saved");
        CHECK (std::fabs (back.norm[simplr::kRegions] - simplr::toNormalized (simplr::kRegions, 3)) < 1e-9,
               "automated param not saved");

        // States from 0.1.x (no loop-fade parameter) migrate the old flag
        {
            auto old = baseState (wav);
            old.has[simplr::kLoopFadePower] = false;
            for (uint32_t id = simplr::kEnvExtBase; id < simplr::kNumParams; ++id)
                old.has[id] = false;
            old.constantPowerFade = false;
            rig.stop ();
            CHECK (applyState (rig, old), "old-format state");
            MemoryStream s3;
            rig.component->getState (&s3);
            s3.seek (0, IBStream::kIBSeekSet, nullptr);
            simplr::PluginState b3;
            simplr::readState (&s3, b3);
            CHECK (b3.norm[simplr::kLoopFadePower] == 0.0, "linear fade should migrate: %f", b3.norm[simplr::kLoopFadePower]);
            CHECK (std::fabs (b3.norm[simplr::envParam (0, simplr::kEnvCurveD)] -
                              simplr::defaultNormalized (simplr::envParam (0, simplr::kEnvCurveD))) < 1e-9,
                   "missing new params should get defaults");
            rig.start ();
        }

        // Missing sample: loads without crashing, path is preserved
        auto missing = baseState ("/nonexistent/folder/gone.wav");
        rig.stop ();
        CHECK (applyState (rig, missing), "setState with missing file");
        MemoryStream saved2;
        rig.component->getState (&saved2);
        saved2.seek (0, IBStream::kIBSeekSet, nullptr);
        simplr::PluginState back2;
        simplr::readState (&saved2, back2);
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
        rig.note (60, 0.0f);
        rig.render (0.3, out);

        auto set = [&] (uint32_t id, double plain) { rig.controller->setParamNormalized (id, simplr::toNormalized (id, plain)); };
        auto st3 = baseState (wav);
        st3.norm[simplr::kMode] = simplr::toNormalized (simplr::kMode, simplr::kModeSlicing);
        st3.norm[simplr::kWarp] = 1.0;
        st3.norm[simplr::kFilterFreq] = simplr::toNormalized (simplr::kFilterFreq, 1800.0);
        st3.norm[simplr::kFilterRes] = 0.6;
        st3.norm[simplr::kFilterSlope] = 1.0;
        st3.norm[simplr::kFilterCircuit] = simplr::toNormalized (simplr::kFilterCircuit, simplr::kPRD);
        st3.norm[simplr::kLfoOn] = 1.0;
        st3.norm[simplr::kLfoPitch] = 0.2;
        st3.edits.manual.push_back (0.33);
        rig.stop ();
        applyState (rig, st3);
        rig.start ();
        (void)set;
        CHECK (snapshot (rig, outDir + "/ui_slicing.png"), "slicing snapshot");

        auto st4 = baseState (wav);
        st4.norm[simplr::kLoopOn] = 1.0;
        st4.norm[simplr::kSampleStart] = 0.1;
        st4.norm[simplr::kSampleEnd] = 0.8;
        st4.norm[simplr::kStart] = 0.1;
        st4.norm[simplr::kLength] = 0.8;
        st4.norm[simplr::kLoopLen] = 0.4;
        st4.norm[simplr::kFilterType] = simplr::toNormalized (simplr::kFilterType, simplr::kMorph);
        st4.norm[simplr::kFilterMorph] = 0.35;
        st4.norm[simplr::kFilterRes] = 0.5;
        st4.norm[simplr::kFilterFreq] = simplr::toNormalized (simplr::kFilterFreq, 900.0);
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
            auto setp = [&] (uint32_t id, double v) { st5.norm[id] = simplr::toNormalized (id, v); };
            setp (simplr::kAmpA, 30.0);
            setp (simplr::kAmpD, 300.0);
            setp (simplr::kAmpS, 0.45);
            setp (simplr::kAmpR, 800.0);
            setp (simplr::envParam (0, simplr::kEnvCurveA), 0.6);
            setp (simplr::envParam (0, simplr::kEnvCurveR), -0.8);
            setp (simplr::envParam (0, simplr::kEnvPointCount), 2);
            setp (simplr::envPointParam (0, 0, simplr::kPtTime), 120.0);
            setp (simplr::envPointParam (0, 0, simplr::kPtLevel), 0.25);
            setp (simplr::envPointParam (0, 0, simplr::kPtCurve), -0.7);
            setp (simplr::envPointParam (0, 1, simplr::kPtTime), 200.0);
            setp (simplr::envPointParam (0, 1, simplr::kPtLevel), 0.8);
            setp (simplr::envPointParam (0, 1, simplr::kPtCurve), 0.5);
            setp (simplr::kLoopOn, 0.0);
            setp (simplr::kLength, 0.7);
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
            CHECK (std::abs (r.getWidth () * 724 - r.getHeight () * 1110) < 1110, "aspect %dx%d", r.getWidth (),
                   r.getHeight ());
            v->release ();
        }

        rig.provider = nullptr;
        rig.module = nullptr;
        return finish ("host test");
    }
}
