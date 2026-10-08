// End-to-end test of the built Probr.vst3. usage: probr_hosttest <Probr.vst3> <output dir>
// The audio passes bit for bit, armed or not; armed While Playing it writes a take per play start into
// the folder of its state (the WAV the input bit for bit, the JSON, the MIDI with notes and pitch bend);
// the state keeps Mode, the Label and the Folder but not Record; the editor's Record button and Mode
// switch, in Classic and in Wide.
#include "Params.h"
#include "plugin/State.h"
#include "pluginkit/testing/HostRig.h"
#include "ui/Editor.h"

#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pk::testing;
using namespace probr;
#define CHECK PK_CHECK
namespace fs = std::filesystem;

static double plainOf (Rig& rig, uint32_t id) { return toPlain (id, rig.controller->getParamNormalized (id)); }

// a 1 kHz sine at -12 dBFS left, 3 kHz at -18 dBFS right
static float toneAt (long long pos, int ch)
{
    return ch == 0 ? (float)(0.25 * std::sin (2 * M_PI * 1000.0 * (double)pos / 48000.0))
                   : (float)(0.125 * std::sin (2 * M_PI * 3000.0 * (double)pos / 48000.0));
}
static InputFn tone ()
{
    return [] (int, int ch, float* buf, int n, long long pos) {
        for (int i = 0; i < n; ++i)
            buf[i] = toneAt (pos + i, ch);
    };
}

static std::string readFile (const fs::path& p)
{
    std::ifstream f (p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf ();
    return ss.str ();
}

int main (int argc, char** argv)
{
    @autoreleasepool
    {
        if (argc < 3)
            return 2;
        initHost ();
        const std::string outDir = argv[2];
        const fs::path folder = fs::path (outDir) / "probr-sessions";
        std::error_code ec;
        fs::remove_all (folder, ec);
        Rig rig;
        CHECK (rig.load (argv[1]), "load");
        if (gFail)
            return finish ("probr host test");
        CHECK (rig.controller->getParameterCount () == (int32)kNumParams + 16, "param count (+ 16 hidden pitch bends)");
        CHECK (rig.component->getBusCount (kEvent, kInput) == 1, "an event input (MIDI is optional)");
        CHECK (countNonAutomatable (rig.controller) == 0, "non-automatable parameters");
        checkPresetMenu (rig.controller); // Init first, Save as Default, factory presets
        CHECK (std::lround (plainOf (rig, kRecord)) == kRecordOff, "a new probe is off");

        // the Label and the Folder as a project gives them
        State st;
        st.has.fill (true);
        st.norm[kMode] = toNormalized (kMode, kModeWhilePlaying);
        st.label = "host test";
        st.folder = folder.string ();
        CHECK (rig.applyState ([&] (IBStream* s) { return writeState (s, st); }), "setState");
        CHECK (rig.start (), "start");
        CHECK (rig.processor->getLatencySamples () == 0, "no latency");

        // off: the tone passes bit for bit
        std::vector<float> out, outR;
        rig.render (0.2, out, &outR, tone ());
        size_t differ = 0;
        for (size_t i = 0; i < out.size (); ++i)
            differ += out[i] != toneAt ((long long)i, 0) || outR[i] != toneAt ((long long)i, 1);
        CHECK (differ == 0, "off: the input bit for bit (%zu samples differ)", differ);

        // armed while the song plays: take 1 (with a note and a pitch bend), stopped, take 2, off
        rig.param (kRecord, 1.0);
        const long long take1At = rig.position;
        rig.note (60, 0.8f, 10);
        rig.param (kMidiPitchBend, 0.75);
        out.clear ();
        outR.clear ();
        rig.render (1.0, out, &outR, tone ());
        const long long take1Frames = rig.position - take1At;
        differ = 0;
        for (size_t i = 0; i < out.size (); ++i)
            differ += out[i] != toneAt (take1At + (long long)i, 0) || outR[i] != toneAt (take1At + (long long)i, 1);
        CHECK (differ == 0, "recording: the input bit for bit (%zu samples differ)", differ);
        rig.ctx.state &= ~ProcessContext::kPlaying;
        rig.render (0.2, out, &outR, tone ());
        rig.ctx.state |= ProcessContext::kPlaying;
        const long long take2At = rig.position;
        rig.render (0.5, out, &outR, tone ());
        const long long take2Frames = rig.position - take2At;
        rig.param (kRecord, 0.0);
        rig.render (0.1, out, &outR, tone ());

        // the state: Mode, the texts, not Record
        rig.param (kRecord, 1.0);
        rig.param (kMode, toNormalized (kMode, kModeAlways));
        rig.render (0.02, out, nullptr, tone ());
        MemoryStream saved;
        CHECK (rig.component->getState (&saved) == kResultOk, "getState");
        saved.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&saved, back), "readState");
        CHECK (back.label == "host test" && back.folder == folder.string () && !back.has[kRecord] &&
                   std::lround (toPlain (kMode, back.norm[kMode])) == kModeAlways,
               "saved: the texts and Mode, not Record");
        rig.param (kRecord, 0.0);
        rig.render (0.02, out, nullptr, tone ());
        pump (0.3); // (the writer thread finishes the takes)

        // the files
        fs::path session;
        for (const auto& e : fs::directory_iterator (folder, ec))
            if (e.is_directory ())
                session = e.path ();
        CHECK (!session.empty (), "a session folder in %s", folder.string ().c_str ());
        const std::string w1 = readFile (session / "host test_001.wav"), w2 = readFile (session / "host test_002.wav");
        CHECK (w1.size () == 58 + (size_t)take1Frames * 8 && w2.size () == 58 + (size_t)take2Frames * 8, "take 1: %zu bytes, take 2: %zu",
               w1.size (), w2.size ());
        size_t bad = 0;
        for (long long i = 0; i < take1Frames && 58 + (size_t)i * 8 + 8 <= w1.size (); ++i)
        {
            float l, r;
            std::memcpy (&l, w1.data () + 58 + i * 8, 4);
            std::memcpy (&r, w1.data () + 62 + i * 8, 4);
            bad += l != toneAt (take1At + i, 0) || r != toneAt (take1At + i, 1);
        }
        CHECK (bad == 0, "take 1 is the input bit for bit (%zu frames differ)", bad);
        const std::string j1 = readFile (session / "host test_001.json"), m1 = readFile (session / "host test_001.midi.json");
        CHECK (j1.find ("\"ended\": \"transport stopped\"") != std::string::npos && j1.find ("\"tempo\": 120") != std::string::npos,
               "take 1's JSON");
        CHECK (m1.find ("\"type\": \"note_on\", \"note\": 60") != std::string::npos && m1.find ("\"type\": \"pitch_bend\", \"value\": 0.5") != std::string::npos,
               "take 1's MIDI: the note and the pitch bend:\n%s", m1.c_str ());
        CHECK (readFile (session / "host test_002.json").find ("\"ended\": \"record off\"") != std::string::npos, "take 2 ended by Record off");

        // editor (Classic): Record and Mode clicked, then Wide
        {
            EditorWindow win (rig.controller);
            CHECK (win.ok (), "editor");
            ViewRect r;
            CHECK (win.view () && win.view ()->getSize (&r) == kResultOk && r.getWidth () == (int32)Editor::kWidth &&
                       r.getHeight () == (int32)(Editor::kHeight + pk::EditorBase::kInfoHeight),
                   "editor size %d x %d", r.getWidth (), r.getHeight ());
            pump (0.1);
            const double rx = Editor::kRecLeft + Editor::kRecordLeft + Editor::kRecordW / 2, ry = Editor::kTop + Editor::kRecordTop + Editor::kRecordH / 2;
            win.click (rx, ry);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kRecord)) == kRecordArmed, "Record clicked: armed");
            win.click (rx, ry);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kRecord)) == kRecordOff, "clicked again: off");
            const double my = Editor::kTop + Editor::kModeTop + Editor::kModeH / 2;
            win.click (Editor::kRecLeft + Editor::kModeLeft + Editor::kModeW / 4, my);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kMode)) == kModeWhilePlaying, "While Playing clicked");
            win.click (Editor::kRecLeft + Editor::kModeLeft + Editor::kModeW * 3 / 4, my);
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kMode)) == kModeAlways, "Always clicked");
            for (int i = 0; i < 10; ++i)
            {
                out.clear ();
                rig.render (0.05, out, nullptr, tone ());
                pump (0.03);
            }
            CHECK (win.savePng (outDir + "/ui_probr.png"), "screenshot");

            // Wide: Record found, and it works there
            win.allowResize (true);
            ControlRect before, now;
            CHECK (findControl (rig.controller, kRecord, before), "Classic: Record found");
            setLayout (rig.controller, "wide", "Wide");
            pump (0.2);
            // (PROBE and RECORD already sit side by side in Classic, so Record may keep its place in Wide)
            CHECK (findControl (rig.controller, kRecord, now), "Wide: Record found");
            win.click (now.cx (), now.cy ());
            pump (0.05);
            CHECK (std::lround (plainOf (rig, kRecord)) == kRecordArmed, "Wide: Record clicked: armed");
            rig.param (kRecord, 0.0);
            CHECK (win.savePng (outDir + "/ui_probr_wide.png"), "screenshot, Wide");
            setLayout (rig.controller, "default", "Classic");
            pump (0.2);
        }
        rig.stop ();
        return finish ("probr host test");
    }
}
