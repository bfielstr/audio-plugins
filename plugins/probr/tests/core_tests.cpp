// Headless tests for probr's core: the ring, the pass-through (bit for bit, recording or not), the WAV (the
// samples bit for bit), take splitting on transport start and stop, the time map's ppq, MIDI capture, the
// session folder shared by probes, no drops at 192 kHz with the writer thread, a full disk and a writer
// that falls behind (a fake file system), and the CPU budget.
// Run: ./probr_tests [filter]     ./probr_tests --make-session <dir>   (a session for the align test)
#include "FileSystem.h"
#include "Params.h"
#include "Probe.h"
#include "Ring.h"
#include "Session.h"
#include "Writer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace probr;
namespace stdfs = std::filesystem;

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

struct TestCase
{
    const char* name;
    std::function<void ()> fn;
};
static std::vector<TestCase>& tests ()
{
    static std::vector<TestCase> t;
    return t;
}
struct Reg
{
    Reg (const char* n, std::function<void ()> f) { tests ().push_back ({n, std::move (f)}); }
};
#define TEST(name)                       \
    static void name ();                 \
    static Reg reg_##name (#name, name); \
    static void name ()

namespace {

// ---- helpers ------------------------------------------------------------------------------------------

struct TempDir
{
    std::string path;
    TempDir ()
    {
        static int n = 0;
        std::random_device rd;
        path = (stdfs::temp_directory_path () / ("probr-test-" + std::to_string (rd ()) + "-" + std::to_string (++n))).string ();
        stdfs::create_directories (path);
    }
    ~TempDir ()
    {
        std::error_code ec;
        stdfs::remove_all (path, ec);
    }
};

std::string readFile (const std::string& p)
{
    std::ifstream f (p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf ();
    return ss.str ();
}

// the files of the (only) session folder in `folder`
std::string sessionDir (const std::string& folder)
{
    std::error_code ec;
    for (const auto& e : stdfs::directory_iterator (folder, ec))
        if (e.is_directory ())
            return e.path ().string ();
    return {};
}

// A WAV probr wrote: its rate, frames and the interleaved samples.
struct Wav
{
    bool ok = false;
    uint32_t rate = 0;
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t factFrames = 0;
    std::vector<float> samples;
};

uint32_t le32 (const std::string& s, size_t at)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= (uint32_t)(uint8_t)s[at + (size_t)i] << (8 * i);
    return v;
}
uint16_t le16 (const std::string& s, size_t at) { return (uint16_t)((uint8_t)s[at] | ((uint8_t)s[at + 1] << 8)); }

Wav parseWav (const std::string& s)
{
    Wav w;
    if (s.size () < 12 || s.compare (0, 4, "RIFF") != 0 || s.compare (8, 4, "WAVE") != 0)
        return w;
    size_t at = 12;
    while (at + 8 <= s.size ())
    {
        const std::string id = s.substr (at, 4);
        const uint32_t size = le32 (s, at + 4);
        const size_t body = at + 8;
        if (id == "fmt ")
        {
            w.format = le16 (s, body);
            w.channels = le16 (s, body + 2);
            w.rate = le32 (s, body + 4);
            w.bits = le16 (s, body + 14);
        }
        else if (id == "fact")
            w.factFrames = le32 (s, body);
        else if (id == "data")
        {
            const size_t n = std::min<size_t> (size, s.size () - body) / 4;
            w.samples.resize (n);
            std::memcpy (w.samples.data (), s.data () + body, n * 4);
            w.ok = true;
            return w;
        }
        at = body + size + (size & 1);
    }
    return w;
}

// A string field of a JSON probr wrote ("" when missing).
std::string jsonString (const std::string& j, const std::string& key)
{
    const std::string k = "\"" + key + "\": \"";
    const size_t a = j.find (k);
    if (a == std::string::npos)
        return {};
    const size_t b = j.find ('"', a + k.size ());
    return j.substr (a + k.size (), b - a - k.size ());
}
double jsonNumber (const std::string& j, const std::string& key)
{
    const std::string k = "\"" + key + "\": ";
    const size_t a = j.find (k);
    if (a == std::string::npos)
        return std::numeric_limits<double>::quiet_NaN ();
    return std::strtod (j.c_str () + a + k.size (), nullptr);
}
// The rows of an array of arrays of numbers ("time_map"); null reads as NaN.
std::vector<std::vector<double>> jsonRows (const std::string& j, const std::string& key)
{
    std::vector<std::vector<double>> rows;
    size_t a = j.find ("\"" + key + "\": [");
    if (a == std::string::npos)
        return rows;
    a = j.find ('[', a) + 1;
    int depth = 1;
    std::vector<double> row;
    for (size_t i = a; i < j.size () && depth > 0; ++i)
    {
        const char c = j[i];
        if (c == '[')
        {
            ++depth;
            row.clear ();
        }
        else if (c == ']')
        {
            if (depth == 2)
                rows.push_back (row);
            --depth;
        }
        else if (depth == 2 && (c == '-' || (c >= '0' && c <= '9')))
        {
            char* end = nullptr;
            row.push_back (std::strtod (j.c_str () + i, &end));
            i = (size_t)(end - j.c_str ()) - 1;
        }
        else if (depth == 2 && j.compare (i, 4, "null") == 0)
        {
            row.push_back (std::numeric_limits<double>::quiet_NaN ());
            i += 3;
        }
    }
    return rows;
}

// A test signal with every kind of float the host may pass: noise, silence, denormals, -0, NaN, inf.
void fillTricky (std::vector<float>& l, std::vector<float>& r, uint32_t seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    for (size_t i = 0; i < l.size (); ++i)
    {
        l[i] = u (rng);
        r[i] = u (rng) * 4.0f;
    }
    if (l.size () > 8)
    {
        l[0] = std::numeric_limits<float>::denorm_min ();
        l[1] = -0.0f;
        l[2] = std::numeric_limits<float>::quiet_NaN ();
        l[3] = std::numeric_limits<float>::infinity ();
        r[4] = -std::numeric_limits<float>::infinity ();
        r[5] = 1e-40f;
        r[6] = 0.0f;
        r[7] = 3.4e38f;
    }
}

bool sameBits (const float* a, const float* b, size_t n) { return std::memcmp (a, b, n * sizeof (float)) == 0; }

// A transport playing from `ppq` at `tempo`.
Transport playing (double ppq, double tempo = 120.0, int64_t sample = 0, bool play = true)
{
    Transport t;
    t.flags = Transport::kValid | Transport::kPpqValid | Transport::kBarValid | Transport::kTempoValid | Transport::kSigValid |
              Transport::kSamplesValid | (play ? Transport::kPlaying : 0u);
    t.ppq = ppq;
    t.barStart = std::floor (ppq / 4.0) * 4.0;
    t.tempo = tempo;
    t.projectSample = sample;
    return t;
}

// A probe with its writer, on a file system: what the processor holds.
struct Rig
{
    Status status;
    Settings settings;
    Probe probe {&status};
    std::unique_ptr<FileSystem> owned;
    FileSystem* fs;
    std::unique_ptr<Writer> writer;
    double sr;
    explicit Rig (const std::string& folder, const std::string& label, double rate = 48000.0, FileSystem* fake = nullptr,
                  double bufferSeconds = 3.0)
    : sr (rate)
    {
        if (!fake)
            owned = makeDiskFileSystem ();
        fs = fake ? fake : owned.get ();
        settings.set (label, folder);
        probe.prepare (rate, 4096, bufferSeconds);
        writer = std::make_unique<Writer> (probe.ring (), status, settings, *fs, "probr test");
    }
    // one block through the probe (output checked against the input), then the writer drains the ring
    bool block (const float* l, const float* r, int n, const Transport& t, bool pump = true, const MidiRec* midi = nullptr, int nm = 0)
    {
        std::vector<float> ol ((size_t)n), orr ((size_t)n);
        probe.process (l, r, ol.data (), orr.data (), n, t, midi, nm);
        if (pump)
            writer->pump ();
        return sameBits (l, ol.data (), (size_t)n) && sameBits (r, orr.data (), (size_t)n);
    }
};

// ---- an in-memory file system that can fill up --------------------------------------------------------
class FakeFs : public FileSystem
{
public:
    int64_t limit = -1;    // bytes the disk takes in all (-1: no limit); a write past it fails
    int64_t reported = -1; // what freeBytes says: -1 the limit minus what is used, else this
    bool failCreate = false;
    std::map<std::string, std::string> files;
    std::map<int, std::string> open;
    int64_t used = 0;
    int next = 1;

    bool makeDirs (const std::string&) override { return !failCreate; }
    std::vector<std::string> list (const std::string& dir) override
    {
        std::vector<std::string> out;
        const std::string pre = joinPath (dir, "");
        for (const auto& [p, d] : files)
            if (p.compare (0, pre.size (), pre) == 0 && p.find_first_of ("/\\", pre.size ()) == std::string::npos)
                out.push_back (p.substr (pre.size ()));
        return out;
    }
    int create (const std::string& path, bool exclusive) override
    {
        if (failCreate || (exclusive && files.count (path)))
            return -1;
        used -= (int64_t)files[path].size ();
        files[path].clear ();
        open[next] = path;
        return next++;
    }
    bool write (int f, const void* data, size_t n) override
    {
        auto it = open.find (f);
        if (it == open.end ())
            return false;
        if (limit >= 0 && used + (int64_t)n > limit)
            return false;
        files[it->second].append ((const char*)data, n);
        used += (int64_t)n;
        return true;
    }
    bool writeAt (int f, uint64_t at, const void* data, size_t n) override
    {
        auto it = open.find (f);
        if (it == open.end ())
            return false;
        std::string& s = files[it->second];
        if (at + n > s.size ())
            return false;
        std::memcpy (s.data () + at, data, n);
        return true;
    }
    bool close (int f) override { return open.erase (f) == 1; }
    bool read (const std::string& path, std::string& out) override
    {
        auto it = files.find (path);
        if (it == files.end ())
            return false;
        out = it->second;
        return true;
    }
    int64_t freeBytes (const std::string&) override { return reported >= 0 ? reported : limit >= 0 ? limit - used : -1; }

    // the file whose name ends with `suffix`
    std::string find (const std::string& suffix) const
    {
        for (const auto& [p, d] : files)
            if (p.size () >= suffix.size () && p.compare (p.size () - suffix.size (), suffix.size (), suffix) == 0)
                return p;
        return {};
    }
};

// a file system that keeps nothing (the CPU test's writer)
class NullFs : public FakeFs
{
public:
    bool write (int, const void*, size_t) override { return true; }
    bool writeAt (int, uint64_t, const void*, size_t) override { return true; }
};

} // namespace

// ---- the tests ----------------------------------------------------------------------------------------

TEST (ring_records)
{
    Ring r;
    r.allocate (4096);
    std::vector<uint8_t> payload;
    Ring::Header h;
    CHECK (!r.pop (h, payload), "empty");
    // many records of changing sizes, so they wrap around the end many times
    std::mt19937 rng (7);
    uint64_t pushed = 0, popped = 0;
    for (int round = 0; round < 2000; ++round)
    {
        std::vector<uint8_t> data ((size_t)(rng () % 600));
        for (size_t i = 0; i < data.size (); ++i)
            data[i] = (uint8_t)(pushed + i);
        if (r.push ((uint32_t)round, {{data.data (), data.size ()}}))
        {
            ++pushed;
            CHECK (r.pop (h, payload), "pop %d", round);
            bool same = h.type == (uint32_t)round && h.bytes == data.size () && std::memcmp (payload.data (), data.data (), data.size ()) == 0;
            CHECK (same, "record %d back as it went in", round);
            ++popped;
        }
    }
    CHECK (pushed == 2000 && popped == 2000, "every record through (%llu)", (unsigned long long)pushed);
    // full: a push that does not fit leaves the ring as it was
    std::vector<uint8_t> big (3000, 1);
    CHECK (r.push (1, {{big.data (), big.size ()}}), "3000 bytes fit");
    CHECK (!r.push (2, {{big.data (), big.size ()}}), "another 3000 do not");
    CHECK (r.pop (h, payload) && h.type == 1 && !r.pop (h, payload), "only the first is there");
    // keepFree: room kept back
    CHECK (!r.push (3, {{big.data (), 3000}}, 2000), "3000 + 2000 kept free do not fit in 4096");
}

TEST (passthrough_bit_exact)
{
    // recording or not, in place or not, any block size: the output is the input, bit for bit
    TempDir dir;
    FakeFs fake;
    Rig rig (dir.path, "pass", 48000.0, &fake);
    rig.probe.setMode (kModeAlways);
    int differ = 0;
    for (int pass = 0; pass < 3; ++pass)
    {
        rig.probe.setRecord (pass == 1);
        for (int b = 0; b < 50; ++b)
        {
            const int n = 1 + (b * 97) % 2048;
            std::vector<float> l ((size_t)n), r ((size_t)n);
            fillTricky (l, r, (uint32_t)(b + 100 * pass));
            if (!rig.block (l.data (), r.data (), n, playing (b)))
                ++differ;
            // in place: the host's buffers are both in and out
            std::vector<float> l2 = l, r2 = r;
            rig.probe.process (l2.data (), r2.data (), l2.data (), r2.data (), n, playing (b));
            if (!sameBits (l.data (), l2.data (), (size_t)n) || !sameBits (r.data (), r2.data (), (size_t)n))
                ++differ;
        }
        rig.writer->pump ();
    }
    CHECK (differ == 0, "%d blocks changed the audio", differ);
}

TEST (wav_bit_identical)
{
    TempDir dir;
    Session::global ().reset ();
    Rig rig (dir.path, "after Trash MIDS", 44100.0);
    rig.probe.setMode (kModeAlways);
    rig.probe.setRecord (true);
    std::vector<float> allL, allR;
    for (int b = 0; b < 200; ++b)
    {
        const int n = 1 + (b * 131) % 1500;
        std::vector<float> l ((size_t)n), r ((size_t)n);
        fillTricky (l, r, (uint32_t)b);
        CHECK (rig.block (l.data (), r.data (), n, playing (b * 0.1)), "block %d passes", b);
        allL.insert (allL.end (), l.begin (), l.end ());
        allR.insert (allR.end (), r.begin (), r.end ());
    }
    rig.probe.setRecord (false);
    std::vector<float> z (64);
    rig.block (z.data (), z.data (), 64, playing (100));
    rig.writer->pump ();
    CHECK (!rig.writer->takeActive (), "the take ended with Record off");
    const std::string sdir = sessionDir (dir.path);
    CHECK (!sdir.empty () && stdfs::path (sdir).filename ().string () == Session::global ().current (), "the session folder (%s)", sdir.c_str ());
    const std::string base = (stdfs::path (sdir) / "after Trash MIDS_001").string ();
    const Wav w = parseWav (readFile (base + ".wav"));
    CHECK (w.ok && w.format == 3 && w.channels == 2 && w.bits == 32 && w.rate == 44100, "float stereo at 44.1 kHz (%u %u %u %u)", w.format,
           w.channels, w.bits, w.rate);
    CHECK (w.samples.size () == allL.size () * 2 && w.factFrames == allL.size (), "%zu frames (%zu)", w.samples.size () / 2, allL.size ());
    size_t differ = 0;
    for (size_t i = 0; i < allL.size () && 2 * i + 1 < w.samples.size (); ++i)
        differ += !sameBits (&w.samples[2 * i], &allL[i], 1) || !sameBits (&w.samples[2 * i + 1], &allR[i], 1);
    CHECK (differ == 0, "the WAV is the input bit for bit (%zu frames differ)", differ);
    const std::string j = readFile (base + ".json");
    CHECK (jsonString (j, "label") == "after Trash MIDS" && jsonString (j, "ended") == "record off" && jsonNumber (j, "take") == 1 &&
               jsonNumber (j, "frames") == (double)allL.size () && jsonNumber (j, "sample_rate") == 44100.0 && jsonString (j, "mode") == "Always",
           "the JSON: %s", j.substr (0, 300).c_str ());
    CHECK (jsonNumber (j, "tempo") == 120.0 && j.find ("\"time_signature\": [4, 4]") != std::string::npos, "tempo and time signature");
    CHECK (!stdfs::exists (base + ".midi.json"), "no MIDI file without MIDI");
    CHECK (rig.status.takesWritten.load () == 1 && rig.status.take.load () == 1, "one take written");
}

TEST (take_splitting)
{
    TempDir dir;
    Session::global ().reset ();
    Rig rig (dir.path, "split");
    rig.probe.setMode (kModeWhilePlaying);
    rig.probe.setRecord (true);
    const int n = 256;
    std::vector<float> l (n), r (n);
    double ppq = 0;
    int64_t pos = 0;
    // stopped, playing, stopped, playing, stopped again: two takes, of the playing blocks only
    const int plan[][2] = {{0, 3}, {1, 10}, {0, 4}, {1, 6}, {0, 2}};
    int takes = 0;
    for (const auto& step : plan)
        for (int b = 0; b < step[1]; ++b)
        {
            fillTricky (l, r, (uint32_t)pos);
            rig.block (l.data (), r.data (), n, playing (ppq, 120.0, pos, step[0] == 1));
            if (step[0] == 1)
            {
                ppq += n / 48000.0 * 2.0;
                pos += n;
            }
            if (b == 0 && step[0] == 1)
                CHECK (rig.status.state.load () == kStateRecording, "recording at play");
            if (step[0] == 0)
                CHECK (rig.status.state.load () == kStateArmed, "armed, waiting, while stopped");
        }
    for (const auto& e : stdfs::directory_iterator (sessionDir (dir.path)))
        takes += e.path ().extension () == ".wav";
    CHECK (takes == 2, "two takes (%d)", takes);
    const std::string sdir = sessionDir (dir.path);
    const Wav w1 = parseWav (readFile (sdir + "/split_001.wav")), w2 = parseWav (readFile (sdir + "/split_002.wav"));
    CHECK (w1.samples.size () == 10u * n * 2 && w2.samples.size () == 6u * n * 2, "take 1: %zu frames, take 2: %zu", w1.samples.size () / 2,
           w2.samples.size () / 2);
    const std::string j1 = readFile (sdir + "/split_001.json"), j2 = readFile (sdir + "/split_002.json");
    CHECK (jsonString (j1, "ended") == "transport stopped" && jsonString (j2, "ended") == "transport stopped", "ended by the stops");
    // the second take starts where the song was when it started again
    const auto t2 = jsonRows (j2, "time_map");
    CHECK (!t2.empty () && std::fabs (t2[0][1] - 10 * n / 48000.0 * 2.0) < 1e-9 && t2[0][7] == 10.0 * n, "take 2 starts at ppq %.6f",
           t2.empty () ? -1.0 : t2[0][1]);

    // Always: one take whatever the transport does, until Record goes Off
    Rig always (dir.path, "always");
    always.probe.setMode (kModeAlways);
    always.probe.setRecord (true);
    for (const auto& step : plan)
        for (int b = 0; b < step[1]; ++b)
            always.block (l.data (), r.data (), n, playing (0, 120.0, 0, step[0] == 1));
    always.probe.setRecord (false);
    always.block (l.data (), r.data (), n, playing (0));
    const Wav wa = parseWav (readFile (sdir + "/always_001.wav"));
    CHECK (wa.samples.size () == 25u * n * 2 && !stdfs::exists (sdir + "/always_002.wav"), "Always: one take of every block (%zu frames)",
           wa.samples.size () / 2);
    // no host transport at all: While Playing records nothing (and says so), Always records
    Rig none (dir.path, "none");
    none.probe.setRecord (true);
    none.block (l.data (), r.data (), n, Transport {});
    CHECK (none.status.state.load () == kStateArmed && none.status.noTransport.load (), "no transport: armed, and the editor is told");
    // deactivation ends a take (the processor calls endTake)
    none.probe.setMode (kModeAlways);
    none.block (l.data (), r.data (), n, Transport {});
    none.probe.endTake (kEndDeactivated);
    none.writer->pump ();
    CHECK (jsonString (readFile (sdir + "/none_001.json"), "ended") == "deactivated", "ended by deactivation");
    CHECK (jsonString (readFile (sdir + "/none_001.json"), "host_transport").empty () &&
               readFile (sdir + "/none_001.json").find ("\"host_transport\": false") != std::string::npos,
           "the JSON says the host gave no transport");
}

TEST (time_map_ppq)
{
    // 120 BPM, blocks of 256 and of 2048, a loop back to ppq 2 halfway and a tempo change: every block
    // start's ppq follows from the entry before it by the time map's rule
    TempDir dir;
    Session::global ().reset ();
    const double sr = 48000.0;
    Rig rig (dir.path, "map", sr);
    rig.probe.setRecord (true);
    std::vector<float> l (4096), r (4096);
    struct Block
    {
        int64_t at;
        double ppq;
    };
    std::vector<Block> starts;
    double ppq = 1.0, tempo = 120.0;
    int64_t at = 0, pos = 48000;
    for (int b = 0; b < 120; ++b)
    {
        const int n = b < 60 ? 256 : 2048;
        if (b == 40)
            ppq = 2.0; // a loop
        if (b == 80)
            tempo = 90.0;
        starts.push_back ({at, ppq});
        rig.block (l.data (), r.data (), n, playing (ppq, tempo, pos));
        ppq += n / sr * tempo / 60.0;
        at += n;
        pos += n;
    }
    rig.probe.setRecord (false);
    rig.block (l.data (), r.data (), 16, playing (ppq, tempo, pos));
    const std::string j = readFile (sessionDir (dir.path) + "/map_001.json");
    const auto rows = jsonRows (j, "time_map");
    CHECK (rows.size () > 100, "%zu entries", rows.size ());
    // spacing: never more than 512 samples apart (a long block gets entries inside it)
    int64_t widest = 0;
    for (size_t i = 1; i < rows.size (); ++i)
        widest = std::max<int64_t> (widest, (int64_t)(rows[i][0] - rows[i - 1][0]));
    CHECK (widest <= Probe::kTimeMapStep, "entries at most %d samples apart (%lld)", Probe::kTimeMapStep, (long long)widest);
    // each block start's ppq from the entry that holds there
    double worst = 0;
    for (const auto& s : starts)
    {
        size_t k = 0;
        while (k + 1 < rows.size () && rows[k + 1][0] <= (double)s.at)
            ++k;
        TimePoint p;
        p.sample = (int64_t)rows[k][0];
        p.t = playing (rows[k][1], rows[k][3]);
        const double got = ppqAfter (p, s.at - p.sample, sr);
        worst = std::max (worst, std::fabs (got - s.ppq));
    }
    CHECK (worst < 1e-9, "every block's ppq from the time map (worst %.3g beats off)", worst);
    // the loop has an entry where it happened
    bool loopEntry = false;
    for (const auto& row : rows)
        loopEntry = loopEntry || (row[0] == (double)starts[40].at && std::fabs (row[1] - 2.0) < 1e-12);
    CHECK (loopEntry, "an entry at the loop");
    bool tempoEntry = false;
    for (const auto& row : rows)
        tempoEntry = tempoEntry || (row[0] == (double)starts[80].at && row[3] == 90.0);
    CHECK (tempoEntry, "an entry at the tempo change");
    // bar starts follow the bars (4/4)
    bool bars = true;
    for (const auto& row : rows)
        bars = bars && row[2] <= row[1] + 1e-9 && row[1] - row[2] < 4.0 + 1e-9;
    CHECK (bars, "bar starts within a bar of the ppq");
}

TEST (midi_capture)
{
    TempDir dir;
    Session::global ().reset ();
    Rig rig (dir.path, "keys");
    rig.probe.setRecord (true);
    std::vector<float> l (512), r (512);
    MidiRec on {}, off {}, bend {};
    on.sample = 100;
    on.kind = kMidiNoteOn;
    on.pitch = 60;
    on.value = 0.75f;
    bend.sample = 300;
    bend.kind = kMidiBend;
    bend.channel = 2;
    bend.value = -0.5f;
    off.sample = 10;
    off.kind = kMidiNoteOff;
    off.pitch = 60;
    rig.block (l.data (), r.data (), 512, playing (4.0));
    const MidiRec block2[] = {bend, on};
    rig.block (l.data (), r.data (), 512, playing (4.0 + 512 / 48000.0 * 2), true, block2, 2);
    rig.block (l.data (), r.data (), 512, playing (4.0 + 1024 / 48000.0 * 2), true, &off, 1);
    CHECK (rig.status.midiEvents.load () == 3, "three events (%d)", rig.status.midiEvents.load ());
    rig.probe.setRecord (false);
    rig.block (l.data (), r.data (), 512, playing (5.0));
    const std::string sdir = sessionDir (dir.path);
    const std::string j = readFile (sdir + "/keys_001.json"), m = readFile (sdir + "/keys_001.midi.json");
    CHECK (jsonString (j, "midi") == "keys_001.midi.json", "the take points at its MIDI");
    // sorted by sample: the bend in block 2 comes after the note on at 100 within it
    const size_t a = m.find ("\"sample\": 612"), b = m.find ("\"sample\": 812"), c = m.find ("\"sample\": 1034");
    CHECK (a != std::string::npos && b != std::string::npos && c != std::string::npos && a < b && b < c, "the events at their samples:\n%s",
           m.c_str ());
    char want[64];
    std::snprintf (want, sizeof (want), "\"ppq\": %.12g", 4.0 + 612 / 48000.0 * 2);
    CHECK (m.find (want) != std::string::npos, "the note on at ppq %s", want);
    CHECK (m.find ("\"type\": \"note_on\", \"note\": 60, \"velocity\": 0.75") != std::string::npos, "note on 60 at 0.75");
    CHECK (m.find ("\"channel\": 2, \"type\": \"pitch_bend\", \"value\": -0.5") != std::string::npos, "pitch bend -0.5 on channel 2");
    CHECK (m.find ("note_off") != std::string::npos, "note off");
}

TEST (session_shared)
{
    TempDir dir;
    Session::global ().reset ();
    std::vector<float> l (256), r (256);
    // two probes and a third with the first one's label: one session folder, no file written over
    Rig a (dir.path, "1 dry"), b (dir.path, "2 after EQ"), c (dir.path, "1 dry");
    for (Rig* x : {&a, &b, &c})
    {
        x->probe.setMode (kModeAlways);
        x->probe.setRecord (true);
    }
    for (int i = 0; i < 10; ++i)
        for (Rig* x : {&a, &b, &c})
            x->block (l.data (), r.data (), 256, playing (i));
    for (Rig* x : {&a, &b, &c})
    {
        x->probe.setRecord (false);
        x->block (l.data (), r.data (), 256, playing (11));
    }
    int folders = 0;
    for (const auto& e : stdfs::directory_iterator (dir.path))
        folders += e.is_directory ();
    CHECK (folders == 1, "one session folder (%d)", folders);
    const std::string sdir = sessionDir (dir.path);
    CHECK (stdfs::exists (sdir + "/1 dry_001.wav") && stdfs::exists (sdir + "/1 dry_002.wav") && stdfs::exists (sdir + "/2 after EQ_001.wav"),
           "1 dry_001, 1 dry_002, 2 after EQ_001");
    CHECK (stdfs::exists (dir.path + "/.probr-session"), "the coordination file");
    // a probe in another process (no session in its memory) joins a session used a moment ago...
    const std::string id = Session::global ().current ();
    FakeFs fake;
    fake.writeFile (joinPath ("/f", ".probr-session"), id + " 1000\n");
    Session::global ().reset ();
    CHECK (Session::global ().id (fake, "/f", 1000 + 60) == id, "joined within %lld s", (long long)Session::kJoinSeconds);
    // ...but not an old one
    Session::global ().reset ();
    fake.writeFile (joinPath ("/f", ".probr-session"), id + " 1000\n");
    CHECK (Session::global ().id (fake, "/f", 1000 + 600) == Session::format (1600), "a new session after ten minutes");
    Session::global ().reset ();
    // labels as file names
    CHECK (fileLabel ("after Trash MIDS") == "after Trash MIDS", "plain");
    CHECK (fileLabel ("a/b:c*?\"<>|") == "a_b_c______", "%s", fileLabel ("a/b:c*?\"<>|").c_str ());
    CHECK (fileLabel ("  ..  ") == "probr" && fileLabel ("") == "probr", "empty");
    CHECK (fileLabel ("CON") == "_CON", "a device name");
    CHECK (fileLabel (std::string (100, 'x')).size () == 80, "at most 80 bytes");
    CHECK (nextTake ({"a_001.wav", "a_003.json", "a_009.midi.json", "ab_020.wav", "a_x.wav"}, "a") == 10, "next take after 9");
}

TEST (no_drops_192k)
{
    // ten seconds at 192 kHz in blocks of 512, played four times as fast as real time, with the writer
    // thread writing to the disk: no drop, every sample in the WAV
    TempDir dir;
    Session::global ().reset ();
    const double sr = 192000.0;
    Rig rig (dir.path, "fast", sr);
    rig.probe.setMode (kModeAlways);
    rig.writer->start ();
    rig.probe.setRecord (true);
    const int n = 512, blocks = (int)(10.0 * sr / n);
    std::vector<float> l (n), r (n), ol (n), orr (n);
    auto sample = [] (int64_t i, int ch) { return (float)std::sin (0.001 * (double)i + ch) * 0.5f + (float)((i * 7919 + ch) % 1000) * 1e-6f; };
    const auto t0 = std::chrono::steady_clock::now ();
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < n; ++i)
        {
            l[(size_t)i] = sample ((int64_t)b * n + i, 0);
            r[(size_t)i] = sample ((int64_t)b * n + i, 1);
        }
        rig.probe.process (l.data (), r.data (), ol.data (), orr.data (), n, playing (b * n / sr * 2));
        std::this_thread::sleep_until (t0 + std::chrono::microseconds ((int64_t)((b + 1) * n / sr * 1e6 / 4)));
    }
    rig.probe.setRecord (false);
    rig.probe.process (l.data (), r.data (), ol.data (), orr.data (), n, playing (0));
    rig.writer->stop ();
    CHECK (rig.status.fault.load () == kFaultNone, "no fault (%d)", rig.status.fault.load ());
    const Wav w = parseWav (readFile (sessionDir (dir.path) + "/fast_001.wav"));
    CHECK (w.samples.size () == (size_t)blocks * n * 2 && w.rate == 192000, "%zu of %d frames", w.samples.size () / 2, blocks * n);
    size_t differ = 0;
    for (size_t i = 0; i * 2 + 1 < w.samples.size (); ++i)
        differ += w.samples[2 * i] != sample ((int64_t)i, 0) || w.samples[2 * i + 1] != sample ((int64_t)i, 1);
    CHECK (differ == 0, "every sample as it passed (%zu differ)", differ);
}

TEST (disk_full)
{
    std::vector<float> l (1024), r (1024);
    // the disk fills up: the take stops at once, the WAV holds what fitted (its header says so), the audio
    // passes untouched all along, no new take until Record goes off and on
    {
        Session::global ().reset ();
        FakeFs fake;
        fake.limit = 300000;
        fake.reported = 1LL << 40; // (the disk says it has room, as a quota or a network share may, but a write fails)
        Rig rig ("/disk", "full", 48000.0, &fake);
        rig.probe.setMode (kModeAlways);
        rig.probe.setRecord (true);
        int differ = 0;
        for (int b = 0; b < 100; ++b)
        {
            fillTricky (l, r, (uint32_t)b);
            differ += !rig.block (l.data (), r.data (), 1024, playing (b));
        }
        CHECK (differ == 0, "the audio passes untouched (%d blocks changed)", differ);
        CHECK (rig.status.fault.load () == kFaultDiskFull && rig.status.state.load () == kStateStopped, "stopped: disk full (%d, %d)",
               rig.status.fault.load (), rig.status.state.load ());
        CHECK (!rig.writer->takeActive () && !rig.probe.takeOpen (), "the take is closed");
        const Wav w = parseWav (fake.files[fake.find ("full_001.wav")]);
        CHECK (w.ok && w.samples.size () * 4 + kWavHeaderBytes <= 300000 && w.samples.size () > 0 && w.factFrames * 2 == w.samples.size (),
               "the WAV's header matches what fitted (%zu samples, %u frames)", w.samples.size (), w.factFrames);
        CHECK (fake.find ("full_002.wav").empty (), "no second take while stopped");
        // Record off and on: the problem is cleared, a new take tries again
        fake.limit = -1;
        rig.probe.setRecord (false);
        rig.block (l.data (), r.data (), 1024, playing (0));
        rig.probe.setRecord (true);
        rig.block (l.data (), r.data (), 1024, playing (0));
        CHECK (rig.status.fault.load () == kFaultNone && rig.status.state.load () == kStateRecording && !fake.find ("full_002.wav").empty (),
               "armed again: a new take");
    }
    // free space below what probr keeps: a clean stop before the disk is full, the JSON says why
    {
        Session::global ().reset ();
        FakeFs fake;
        fake.limit = kStopSpaceBytes + 600000; // 1.5 s of audio over the line
        Rig rig ("/disk", "low", 48000.0, &fake);
        rig.probe.setMode (kModeAlways);
        rig.probe.setRecord (true);
        for (int b = 0; b < 300; ++b)
            rig.block (l.data (), r.data (), 1024, playing (b));
        CHECK (rig.status.fault.load () == kFaultDiskFull, "stopped below %lld MB free", (long long)(kStopSpaceBytes >> 20));
        const std::string j = fake.files[fake.find ("low_001.json")];
        CHECK (jsonString (j, "ended") == "disk full" && j.find ("\"complete\": false") != std::string::npos, "the JSON: ended by a full disk");
        CHECK (jsonNumber (j, "frames") >= 48000 && jsonNumber (j, "frames") < 48000 * 3, "%.0f frames before it stopped", jsonNumber (j, "frames"));
        CHECK (fake.used < fake.limit, "the disk never filled");
    }
    // the folder cannot be written: stopped at the take's start
    {
        Session::global ().reset ();
        FakeFs fake;
        fake.failCreate = true;
        Rig rig ("/ro", "ro", 48000.0, &fake);
        rig.probe.setMode (kModeAlways);
        rig.probe.setRecord (true);
        for (int b = 0; b < 4; ++b)
            CHECK (rig.block (l.data (), r.data (), 1024, playing (b)), "passes");
        CHECK (rig.status.fault.load () == kFaultWrite && rig.status.state.load () == kStateStopped, "stopped: cannot write");
    }
}

TEST (writer_falls_behind)
{
    // the writer does not drain the ring (stalled): the ring fills, the take ends cleanly (the end always
    // fits), the audio is never touched; once the writer catches up the files are whole
    Session::global ().reset ();
    FakeFs fake;
    Rig rig ("/slow", "slow", 48000.0, &fake, 0.5);
    rig.probe.setMode (kModeAlways);
    rig.probe.setRecord (true);
    std::vector<float> l (1024), r (1024);
    int differ = 0;
    for (int b = 0; b < 100; ++b)
    {
        fillTricky (l, r, (uint32_t)b);
        differ += !rig.block (l.data (), r.data (), 1024, playing (b), false);
    }
    CHECK (differ == 0, "the audio passes untouched (%d blocks changed)", differ);
    CHECK (rig.status.fault.load () == kFaultOverrun && rig.status.state.load () == kStateStopped, "stopped: the writer fell behind");
    rig.writer->pump ();
    const std::string j = fake.files[fake.find ("slow_001.json")];
    CHECK (jsonString (j, "ended") == "writer fell behind", "the JSON says so: %s", jsonString (j, "ended").c_str ());
    const Wav w = parseWav (fake.files[fake.find ("slow_001.wav")]);
    CHECK (w.ok && w.samples.size () / 2 == (size_t)jsonNumber (j, "frames") && w.samples.size () % 2048 == 0,
           "the WAV holds whole blocks up to the stop (%zu frames)", w.samples.size () / 2);
    // Record off and on: it records again
    rig.probe.setRecord (false);
    rig.block (l.data (), r.data (), 1024, playing (0));
    rig.probe.setRecord (true);
    for (int b = 0; b < 20; ++b)
        rig.block (l.data (), r.data (), 1024, playing (b));
    CHECK (rig.status.state.load () == kStateRecording && rig.status.fault.load () == kFaultNone, "recording again");
}

TEST (cpu_budget)
{
    // the audio thread's part: 60 s at 48 kHz in blocks of 128, armed and recording, the writer thread
    // draining into a file system that keeps nothing. Tiny: under 1 % of a core.
    Session::global ().reset ();
    NullFs nul;
    Rig rig ("/null", "cpu", 48000.0, &nul);
    rig.probe.setMode (kModeAlways);
    rig.writer->start ();
    rig.probe.setRecord (true);
    const int n = 128, blocks = (int)(60.0 * 48000 / n);
    std::vector<float> l (n), r (n), ol (n), orr (n);
    fillTricky (l, r, 3);
    double best = 1e9;
    for (int round = 0; round < 3; ++round)
    {
        double spent = 0;
        for (int b = 0; b < blocks; ++b)
        {
            const auto t0 = std::chrono::steady_clock::now ();
            rig.probe.process (l.data (), r.data (), ol.data (), orr.data (), n, playing (b * n / 24000.0, 120.0, (int64_t)b * n));
            spent += std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
            if (b % 64 == 0)
                std::this_thread::yield (); // (lets the writer drain)
            if (rig.probe.ring ().used () > rig.probe.ring ().capacity () / 2)
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        best = std::min (best, spent);
    }
    rig.probe.setRecord (false);
    rig.probe.process (l.data (), r.data (), ol.data (), orr.data (), n, playing (0));
    rig.writer->stop ();
    std::printf ("    CPU: %.3f%% of one core (recording)\n", 100.0 * best / 60.0);
    CHECK (rig.status.fault.load () == kFaultNone, "no drops");
    CHECK (best / 60.0 < 0.01, "too slow: %.3f%%", 100.0 * best / 60.0);
}

// ---- a session for scripts/probr_align.py's test ------------------------------------------------------
// Two probes recorded with the writer at 48 kHz, 120 BPM, 4/4: "1 source" (a tone with harmonics that
// plays a note per beat, with its MIDI) from ppq 0, "2 half" (the same at half the level, -6.02 dB, side
// removed) armed a beat later, so its take starts at ppq 1. Eight beats.
static int makeSession (const std::string& folder)
{
    Session::global ().reset ();
    const double sr = 48000.0;
    Rig a (folder, "1 source", sr), b (folder, "2 half", sr);
    a.probe.setRecord (true);
    const int n = 480;
    const int total = (int)(8 * 0.5 * sr / n);
    std::vector<float> l (n), r (n), hl (n), hr (n);
    const int notes[] = {45, 52, 57, 60, 64, 57, 52, 48};
    int64_t pos = 0;
    for (int k = 0; k < total; ++k)
    {
        const double ppq = pos / sr * 2.0;
        std::vector<MidiRec> midi;
        for (int i = 0; i < n; ++i)
        {
            const double t = (double)(pos + i) / sr, beatPos = t * 2.0;
            const int beat = (int)std::floor (beatPos);
            const double f0 = 440.0 * std::pow (2.0, (notes[beat % 8] - 69) / 12.0);
            double s = 0;
            for (int h = 1; h <= 8; ++h)
                s += std::sin (2 * M_PI * f0 * h * t) / h;
            const double env = std::exp (-3.0 * (beatPos - beat));
            l[(size_t)i] = (float)(0.3 * env * s);
            r[(size_t)i] = (float)(0.25 * env * s);
            hl[(size_t)i] = 0.5f * 0.5f * (l[(size_t)i] + r[(size_t)i]);
            hr[(size_t)i] = hl[(size_t)i];
            if (std::floor ((double)(pos + i - 1) / sr * 2.0) != beat || pos + i == 0)
            {
                MidiRec on {};
                on.sample = i;
                on.kind = kMidiNoteOn;
                on.pitch = notes[beat % 8];
                on.value = 0.8f;
                midi.push_back (on);
            }
            const double before = (double)(pos + i - 1) / sr * 2.0;
            if (pos + i > 0 && std::floor (before) == beat && before - beat < 0.9 && beatPos - beat >= 0.9)
            {
                MidiRec off {};
                off.sample = i;
                off.kind = kMidiNoteOff;
                off.pitch = notes[beat % 8];
                midi.push_back (off);
            }
        }
        if (k == (int)(0.5 * sr / n))
            b.probe.setRecord (true); // armed a beat in
        a.block (l.data (), r.data (), n, playing (ppq, 120.0, pos), true, midi.data (), (int)midi.size ());
        b.block (hl.data (), hr.data (), n, playing (ppq, 120.0, pos));
        pos += n;
    }
    a.probe.setRecord (false);
    b.probe.setRecord (false);
    a.block (l.data (), r.data (), n, playing (8.0, 120.0, pos, false));
    b.block (l.data (), r.data (), n, playing (8.0, 120.0, pos, false));
    std::printf ("session %s in %s\n", Session::global ().current ().c_str (), folder.c_str ());
    return a.status.takesWritten.load () == 1 && b.status.takesWritten.load () == 1 ? 0 : 1;
}

int main (int argc, char** argv)
{
    if (argc == 3 && std::strcmp (argv[1], "--make-session") == 0)
    {
        std::error_code ec;
        stdfs::remove_all (argv[2], ec);
        stdfs::create_directories (argv[2], ec);
        return makeSession (argv[2]);
    }
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& t : tests ())
    {
        // (the CPU budget runs on its own, as probr_cpu)
        if (filter ? std::string (t.name).find (filter) == std::string::npos : std::string (t.name) == "cpu_budget")
            continue;
        const int before = gFailures;
        std::printf ("%s\n", t.name);
        t.fn ();
        std::printf ("  %s\n", gFailures == before ? "ok" : "FAILED");
        ++ran;
    }
    std::printf ("\n%d tests, %d checks, %d failures\n", ran, gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
