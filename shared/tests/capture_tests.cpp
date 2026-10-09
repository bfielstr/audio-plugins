// The capture band's pure parts (no SDK): the capture buffer (nothing dropped across blocks of any size,
// the window in bars synced to the host's tempo or in seconds, the peaks), the WAV writer (the held audio
// bit for bit, the `clm ` chunk) and the wavetable slicer (a saw at a known pitch: its period, frames of
// 2048 starting at rising zero crossings, at most 256, evenly spaced). Run: ./pluginkit_capture_tests
#include "pluginkit/Capture.h"
#include "pluginkit/WavFile.h"
#include "pluginkit/Wavetable.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

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

namespace {

uint32_t rnd = 12345;
uint32_t next () { return rnd = rnd * 1664525u + 1013904223u; }

// a rising saw from -1 to 1, `hz` at `sr`, starting at `phase` (0 .. 1)
std::vector<float> saw (double hz, double sr, double seconds, double phase)
{
    std::vector<float> x ((size_t)(seconds * sr));
    for (size_t i = 0; i < x.size (); ++i)
    {
        const double p = std::fmod (phase + hz * (double)i / sr, 1.0);
        x[i] = (float)(2.0 * p - 1.0);
    }
    return x;
}

void captureKeepsEverything ()
{
    std::printf ("captureKeepsEverything\n");
    auto buf = std::make_unique<pk::CaptureBuffer> ();
    // before an editor enables it: only counted, nothing kept
    {
        std::vector<float> one (64, 1.0f), back (64, 5.0f);
        buf->push (one.data (), one.data (), 64, {}, 48000.0);
        CHECK (!buf->enabled () && buf->written () == 64 && buf->read (64, 64, back.data (), nullptr) == 0 && back[0] == 0.0f, "not enabled: zeros");
        buf = std::make_unique<pk::CaptureBuffer> ();
    }
    buf->enable ();
    CHECK (buf->enabled (), "enabled");
    // a count as the signal (exact in floats), in blocks of 1 .. 1024 frames
    std::vector<float> l (1024), r (1024);
    uint64_t f = 0;
    const uint64_t total = 3 * 48000 + 77;
    while (f < total)
    {
        const int n = (int)std::min<uint64_t> (total - f, 1 + next () % 1024);
        for (int i = 0; i < n; ++i)
        {
            l[(size_t)i] = (float)(f + (uint64_t)i);
            r[(size_t)i] = -(float)(f + (uint64_t)i);
        }
        buf->push (l.data (), r.data (), n, {}, 48000.0);
        f += (uint64_t)n;
    }
    CHECK (buf->written () == total, "all written");
    const pk::CaptureBuffer::Window w = buf->window (1); // stopped: 2 s
    CHECK (!w.bars && w.frames == 96000 && w.end == total, "2 s ending now (%lld, %llu)", (long long)w.frames, (unsigned long long)w.end);
    std::vector<float> a ((size_t)w.frames), b ((size_t)w.frames);
    CHECK (buf->read (w.end, w.frames, a.data (), b.data ()) == w.frames, "all real");
    bool contiguous = true;
    for (int64_t i = 0; i < w.frames; ++i)
        contiguous = contiguous && a[(size_t)i] == (float)(total - (uint64_t)w.frames + (uint64_t)i) && b[(size_t)i] == -a[(size_t)i];
    CHECK (contiguous, "no frame dropped or repeated");
    // more than was written: zeros before the start
    std::vector<float> c (10);
    CHECK (buf->read (5, 10, c.data (), nullptr) == 5 && c[0] == 0.0f && c[5] == 0.0f && c[9] == 4.0f, "before the start: 0");
    // the peaks: each column's extremes
    float mn[4], mx[4];
    buf->readPeaks (w.end, w.frames, 4, mn, mx);
    CHECK (mx[3] >= (float)(total - 300) && mn[3] <= -(float)(total - 300), "peaks of the last column (%g %g)", mn[3], mx[3]);
    // the ring wraps: after more than it holds, the window is still contiguous
    for (int rounds = 0; rounds < 1200; ++rounds)
    {
        for (int i = 0; i < 1024; ++i)
            l[(size_t)i] = r[(size_t)i] = (float)((f + (uint64_t)i) % 1000000);
        buf->push (l.data (), r.data (), 1024, {}, 48000.0);
        f += 1024;
    }
    const auto w4 = buf->window (2);
    std::vector<float> d ((size_t)w4.frames);
    CHECK (buf->read (w4.end, w4.frames, d.data (), nullptr) == w4.frames, "after wrapping, all real");
    bool ok = true;
    for (int64_t i = 0; i < w4.frames; ++i)
        ok = ok && d[(size_t)i] == (float)((w4.end - (uint64_t)w4.frames + (uint64_t)i) % 1000000);
    CHECK (ok, "after wrapping, contiguous");
}

void captureSyncsToBars ()
{
    std::printf ("captureSyncsToBars\n");
    auto buf = std::make_unique<pk::CaptureBuffer> ();
    buf->enable ();
    const double sr = 48000, bpm = 120, perBeat = sr * 60 / bpm; // 24000 frames a beat
    std::vector<float> z (480, 0.25f);
    uint64_t f = 0;
    const double startPpq = 3.0; // the transport started on beat 4 of bar 1
    while (f < (uint64_t)(5.3 * sr))
    {
        pk::CaptureBuffer::Transport t;
        t.bpm = bpm;
        t.playing = true;
        t.ppqValid = true;
        t.ppq = startPpq + (double)f / perBeat;
        buf->push (z.data (), z.data (), 480, t, sr);
        f += 480;
    }
    // ppq at the end: 3 + 254400 / 24000 = 13.6: the last bar line is at 12 (frame (12 - 3) * 24000)
    const auto w = buf->window (1);
    CHECK (w.bars && w.length == 2.0 && w.frames == (int64_t)(8 * perBeat), "2 bars (%lld frames)", (long long)w.frames);
    CHECK (w.end == (uint64_t)((12 - startPpq) * perBeat), "ending on the bar line (%llu)", (unsigned long long)w.end);
    const auto w1 = buf->window (0);
    CHECK (w1.frames == (int64_t)(4 * perBeat) && w1.end == w.end, "1 bar");
    // 4 bars at 120 BPM is 8 s: more than was played (zeros at its start), still from the same bar line
    const auto w4 = buf->window (2);
    CHECK (w4.frames == (int64_t)(16 * perBeat) && w4.end == w.end, "4 bars");
    // stopped: seconds, ending now
    pk::CaptureBuffer::Transport stopped;
    stopped.bpm = bpm;
    buf->push (z.data (), z.data (), 480, stopped, sr);
    const auto ws = buf->window (2);
    CHECK (!ws.bars && ws.frames == (int64_t)(4 * sr) && ws.end == buf->written (), "stopped: 4 s ending now");
    // a slow tempo past what the ring holds: cut to it
    pk::CaptureBuffer::Transport slow;
    slow.bpm = 20;
    slow.playing = true;
    buf->push (z.data (), z.data (), 480, slow, sr);
    CHECK (buf->window (2).frames == pk::CaptureBuffer::kMaxWindow, "held to the ring");
    buf->noteOn (69);
    CHECK (std::fabs (buf->noteHz () - 440.0) < 1e-9, "the last note's frequency");
}

void wavIsBitExact ()
{
    std::printf ("wavIsBitExact\n");
    std::vector<float> l (10007), r (10007);
    for (size_t i = 0; i < l.size (); ++i)
    {
        uint32_t u = next (), v = next ();
        // any finite floats, denormals and signed zeros too
        std::memcpy (&l[i], &u, 4);
        std::memcpy (&r[i], &v, 4);
        if (!std::isfinite (l[i]))
            l[i] = -0.0f;
        if (!std::isfinite (r[i]))
            r[i] = 1e-40f;
    }
    const auto bytes = pk::wav::encode ({l.data (), r.data ()}, l.size (), 44100.0);
    pk::wav::Info info;
    CHECK (pk::wav::parse (bytes, info), "parses");
    CHECK (info.format == 3 && info.bits == 32 && info.channels == 2 && info.sampleRate == 44100 && info.clm.empty (), "32-bit float stereo");
    bool same = info.samples.size () == l.size () * 2;
    for (size_t i = 0; same && i < l.size (); ++i)
        same = std::memcmp (&info.samples[2 * i], &l[i], 4) == 0 && std::memcmp (&info.samples[2 * i + 1], &r[i], 4) == 0;
    CHECK (same, "every sample bit for bit");
    CHECK (bytes.size () == 12 + 26 + 12 + 8 + l.size () * 8, "the size (%zu)", bytes.size ());
}

void wavetableFromASaw ()
{
    std::printf ("wavetableFromASaw\n");
    const double sr = 48000, hz = 220;
    const auto x = saw (hz, sr, 1.0, 0.3);
    const double period = pk::wavetable::detectPeriod (x.data (), x.size (), sr);
    CHECK (std::fabs (period - sr / hz) < sr / hz * 0.002, "the period %.3f (%.3f)", period, sr / hz);
    const auto t = pk::wavetable::make (x.data (), x.size (), sr, 0);
    CHECK (t.error.empty () && t.detected, "made (%s)", t.error.c_str ());
    CHECK (t.count >= 210 && t.count <= 219 && t.frames.size () == (size_t)t.count * 2048, "%d frames of 2048", t.count);
    // each frame starts on a rising zero crossing and is one cycle of the saw: up to the top at its middle
    bool aligned = true, shaped = true;
    for (int f = 0; f < t.count; ++f)
    {
        const float* fr = t.frames.data () + (size_t)f * 2048;
        aligned = aligned && std::fabs (fr[0]) < 0.01f && fr[1] > fr[0];
        // (scaled together to full scale, which the interpolation's overshoot at the saw's jump sets: the shape,
        // not the level) a straight ramp up from 0, and down from the jump to 0 again
        shaped = shaped && fr[512] > 0.4f && std::fabs (fr[512] / fr[256] - 2.0f) < 0.02f && std::fabs (fr[1536] + fr[512]) < 0.01f;
    }
    CHECK (aligned, "frames start at rising zero crossings");
    CHECK (shaped, "one cycle each (%g %g)", t.frames[512], t.frames[1536]);
    // as a file: mono, the clm chunk
    const auto bytes = pk::wav::encode ({t.frames.data ()}, t.frames.size (), sr, pk::wav::clmText (2048));
    pk::wav::Info info;
    CHECK (pk::wav::parse (bytes, info) && info.channels == 1 && info.samples == t.frames, "a mono WAV of the frames");
    CHECK (info.clm == "<!>2048 01000000 wavetable (www.xferrecords.com)", "the clm chunk: %s", info.clm.c_str ());
    // longer: at most 256 frames, evenly spaced
    const auto y = saw (hz, sr, 4.0, 0.0);
    const auto t4 = pk::wavetable::make (y.data (), y.size (), sr, 0);
    CHECK (t4.count == 256, "256 frames (%d)", t4.count);
    // no pitch (silence): none, unless a note is given
    std::vector<float> quiet (48000, 0.0f);
    CHECK (!pk::wavetable::make (quiet.data (), quiet.size (), sr, 0).error.empty (), "silence: none");
    // a pitch the detector cannot find (too low for it) with the note given
    const auto low = saw (20.0, sr, 1.0, 0.25);
    const auto th = pk::wavetable::make (low.data (), low.size (), sr, 20.0);
    CHECK (th.error.empty () && !th.detected && th.count >= 18, "the note's pitch when none is found (%d, %s)", th.count, th.error.c_str ());
}

} // namespace

int main ()
{
    captureKeepsEverything ();
    captureSyncsToBars ();
    wavIsBitExact ();
    wavetableFromASaw ();
    std::printf ("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
