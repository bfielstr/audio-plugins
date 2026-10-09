// Probr's saved state (plugin/State.cpp) and its processor (plugin/Processor.cpp) on their own, without a
// host: a round trip of Mode, the Label and the Folder (Record is never saved), a state without the texts,
// one from a newer build, a stream that is not probr's, the parameter table's fixed points; then the VST3
// processor itself: the audio passes bit for bit through process (), recording or not, and the take it
// writes is the input bit for bit, with the Label and the Folder from its state.
// Run: ./probr_state_tests
#include "Params.h"
#include "Session.h"
#include "plugin/Processor.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace probr;

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        ++gChecks;                                                         \
        if (!(cond))                                                       \
        {                                                                  \
            ++gFailures;                                                   \
            std::printf ("    FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
            std::printf (__VA_ARGS__);                                     \
            std::printf ("\n");                                            \
        }                                                                  \
    } while (0)

static State roundTrip (const State& st, bool& ok)
{
    MemoryStream s;
    ok = writeState (&s, st);
    s.seek (0, IBStream::kIBSeekSet, nullptr);
    State back;
    ok = readState (&s, back) && ok;
    return back;
}

static void stateTests ()
{
    // Mode, the Label and the Folder there and back; Record is not saved
    {
        State st;
        st.norm[kMode] = toNormalized (kMode, kModeAlways);
        st.norm[kRecord] = 1.0;
        st.has.fill (true);
        st.label = "after Trash MIDS \xc3\xa9\xe2\x86\x92"; // (UTF-8: é and an arrow)
        st.folder = "/Volumes/Work/probr sessions";
        bool ok = false;
        const State back = roundTrip (st, ok);
        CHECK (ok, "write and read");
        CHECK (std::lround (toPlain (kMode, back.norm[kMode])) == kModeAlways && back.has[kMode], "Mode back");
        CHECK (back.label == st.label && back.folder == st.folder, "the texts back: '%s' '%s'", back.label.c_str (), back.folder.c_str ());
        CHECK (!back.has[kRecord] && back.norm[kRecord] == 0.0, "Record not saved: a project opens with the probe off");
    }
    // the default folder is saved as "" (so a project keeps using the default folder of the machine it opens on)
    {
        State st;
        st.has[kMode] = true;
        st.label = "";
        bool ok = false;
        const State back = roundTrip (st, ok);
        CHECK (ok && back.folder.empty () && back.label.empty (), "empty texts back as empty");
    }
    // a state without the texts (cut off after the parameters): the label a new probe has
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x52425250);
            w.writeInt32 (1);
            w.writeInt32 (1);
            w.writeInt32u (kMode);
            w.writeDouble (1.0);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "reads");
        CHECK (back.label == kDefaultLabel && back.folder.empty () && back.norm[kMode] == 1.0, "the defaults for the texts");
    }
    // a newer build's state: its extra parameters skipped, the rest read
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x52425250);
            w.writeInt32 (9);
            w.writeInt32 (2);
            w.writeInt32u (kNumParams + 3);
            w.writeDouble (0.5);
            w.writeInt32u (kMode);
            w.writeDouble (1.0);
            w.writeInt32 (3);
            w.writeRaw ("abc", 3);
            w.writeInt32 (0);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back) && back.norm[kMode] == 1.0 && back.label == "abc", "a newer version reads");
    }
    // not probr's
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x525A4D53);
            w.writeInt32 (1);
            w.writeInt32 (0);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (!readState (&s, back), "another plug-in's state is refused");
    }
    // the table
    CHECK (paramTable ().size () == kNumParams, "the table has every parameter");
    CHECK (paramTable ().toText (kRecord, 0) == "Off" && paramTable ().toText (kRecord, 1) == "Armed", "Record: Off / Armed");
    CHECK (paramTable ().toText (kMode, 0) == "While Playing" && paramTable ().toText (kMode, 1) == "Always", "Mode: While Playing / Always");
    CHECK (defaultNormalized (kRecord) == 0.0 && defaultNormalized (kMode) == 0.0, "off and While Playing by default");
}

static std::string readFile (const std::string& p)
{
    std::ifstream f (p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf ();
    return ss.str ();
}

static void processorTests ()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path () / ("probr-state-test-" + std::to_string (std::random_device {}()));
    fs::create_directories (dir);
    Session::global ().reset ();

    auto* p = new Processor ();
    CHECK (p->initialize (nullptr) == kResultOk, "initialize");
    CHECK (p->getLatencySamples () == 0, "no latency");
    CHECK (p->getBusCount (kEvent, kInput) == 1, "a MIDI input");
    CHECK (p->canProcessSampleSize (kSample32) == kResultTrue, "32-bit");
    SpeakerArrangement st = SpeakerArr::kStereo, mono = SpeakerArr::kMono;
    CHECK (p->setBusArrangements (&st, 1, &st, 1) == kResultOk && p->setBusArrangements (&mono, 1, &mono, 1) != kResultOk, "stereo only");
    // the Label and the Folder (and Mode Always) from a state, as a project gives them
    {
        State s;
        s.has.fill (true);
        s.norm[kMode] = 1.0;
        s.label = "bus out";
        s.folder = dir.string ();
        MemoryStream ms;
        writeState (&ms, s);
        ms.seek (0, IBStream::kIBSeekSet, nullptr);
        CHECK (p->setState (&ms) == kResultOk, "setState");
    }
    ProcessSetup setup {kRealtime, kSample32, 512, 48000.0};
    CHECK (p->setupProcessing (setup) == kResultOk && p->setActive (true) == kResultOk, "start");
    p->setProcessing (true);

    const int n = 512;
    std::vector<float> inL (n), inR (n), outL (n), outR (n), allL, allR;
    float* ins[2] = {inL.data (), inR.data ()};
    float* outs[2] = {outL.data (), outR.data ()};
    AudioBusBuffers in {}, out {};
    in.numChannels = out.numChannels = 2;
    in.channelBuffers32 = ins;
    out.channelBuffers32 = outs;
    ProcessContext ctx {};
    ctx.sampleRate = 48000.0;
    ctx.tempo = 120.0;
    ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
    ParameterChanges changes (4);
    ProcessData data {};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = n;
    data.numInputs = data.numOutputs = 1;
    data.inputs = &in;
    data.outputs = &out;
    data.processContext = &ctx;
    data.inputParameterChanges = &changes;
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    int differ = 0;
    for (int b = 0; b < 60; ++b)
    {
        changes.clearQueue ();
        if (b == 20 || b == 50)
        {
            // Record armed at block 20, off at 50
            int32 qi = 0, pi = 0;
            if (IParamValueQueue* q = changes.addParameterData (kRecord, qi))
                q->addPoint (0, b == 20 ? 1.0 : 0.0, pi);
        }
        for (int i = 0; i < n; ++i)
        {
            inL[(size_t)i] = u (rng);
            inR[(size_t)i] = b == 30 && i < 4 ? std::nanf ("") : u (rng) * 1e-39f;
        }
        ctx.projectTimeMusic = b * n / 48000.0 * 2.0;
        ctx.projectTimeSamples = (int64)b * n;
        CHECK (p->process (data) == kResultOk, "process");
        differ += std::memcmp (inL.data (), outL.data (), n * 4) != 0 || std::memcmp (inR.data (), outR.data (), n * 4) != 0;
        if (b >= 20 && b < 50)
        {
            allL.insert (allL.end (), inL.begin (), inL.end ());
            allR.insert (allR.end (), inR.begin (), inR.end ());
        }
        if (b == 35)
            CHECK (p->getShared ()->status.state.load () == kStateRecording, "recording while armed");
    }
    CHECK (differ == 0, "process () passes the audio bit for bit (%d blocks changed)", differ);
    p->setProcessing (false);
    p->setActive (false);
    // what it wrote: <folder>/<session>/bus out_001.wav, the input of blocks 20 .. 49 bit for bit
    const fs::path wav = dir / Session::global ().current () / "bus out_001.wav";
    const std::string w = readFile (wav.string ());
    const size_t frames = w.size () > 58 ? (w.size () - 58) / 8 : 0;
    CHECK (frames == allL.size (), "%s: %zu frames (%zu)", wav.string ().c_str (), frames, allL.size ());
    size_t bad = 0;
    for (size_t i = 0; i < frames && i < allL.size (); ++i)
        bad += std::memcmp (w.data () + 58 + 8 * i, &allL[i], 4) != 0 || std::memcmp (w.data () + 62 + 8 * i, &allR[i], 4) != 0;
    CHECK (bad == 0, "the WAV is the input bit for bit (%zu frames differ)", bad);
    // getState keeps the texts and Mode, not Record
    {
        MemoryStream ms;
        CHECK (p->getState (&ms) == kResultOk, "getState");
        ms.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&ms, back) && back.label == "bus out" && back.folder == dir.string () && back.norm[kMode] == 1.0 && !back.has[kRecord],
               "the state: '%s' '%s'", back.label.c_str (), back.folder.c_str ());
    }
    p->terminate ();
    p->release ();
    std::error_code ec;
    fs::remove_all (dir, ec);
    Session::global ().reset ();
}

int main ()
{
    stateTests ();
    processorTests ();
    std::printf ("%s: %d checks, %d failed\n", gFailures ? "FAILED" : "OK", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
