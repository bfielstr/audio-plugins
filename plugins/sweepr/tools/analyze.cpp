// sweepr_analyze: compares two recordings of the same material, for calibrating sweepr's bells against a
// reference EQ (src/core/BellShape.h).
//
//   sweepr_analyze <a.wav> <b.wav>
//       b's magnitude response against a per 1/12 octave (20 Hz to 20 kHz, dB, with the coherence) and how
//       deep b and a null once aligned. With a the dry signal and b the reference EQ's render of it, that is
//       the reference's response; with a sweepr's render and b the reference's, how far apart they are.
//   sweepr_analyze <dry.wav> <reference.wav> --preset <settings.txt> [--static] [--bpm <tempo>] [--out <wav>]
//       renders dry.wav through sweepr at the settings (a factory preset's text format; Init plus its
//       lines), then prints the reference's response, sweepr's and their difference per 1/12 octave, and
//       how deep the two renders null. --static sets both LFO Depths to 0 and Tape off, whatever the
//       settings say (the bells alone, as a static EQ). --bpm: the song's tempo for synced LFOs (120 by
//       default; the render starts at the song's start). --out writes sweepr's render (32-bit float, the latency removed).
//
// Noise, sweeps or music all work as the signal (the response is the ratio of averaged spectra; the
// coherence says where it can be trusted). Mono analysis: each file's channels are averaged.
#include "Analyze.h"

#include "Engine.h"
#include "Params.h"

#include "pluginkit/PresetStore.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace sweepr;
namespace az = sweepr::analyze;

namespace {

int usage ()
{
    std::fprintf (stderr,
                  "usage: sweepr_analyze <a.wav> <b.wav>\n"
                  "       sweepr_analyze <dry.wav> <reference.wav> --preset <settings.txt> [--static] [--bpm <tempo>] [--out <wav>]\n");
    return 2;
}

bool writeFloatWav (const std::string& path, const std::vector<std::vector<float>>& ch, double sr)
{
    std::ofstream f (path, std::ios::binary);
    if (!f)
        return false;
    auto u32 = [&] (uint32_t v) { f.write ((const char*)&v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*)&v, 2); };
    const uint32_t c = (uint32_t)ch.size (), n = (uint32_t)ch[0].size (), data = n * c * 4;
    f.write ("RIFF", 4);
    u32 (36 + data);
    f.write ("WAVEfmt ", 8);
    u32 (16);
    u16 (3);
    u16 ((uint16_t)c);
    u32 ((uint32_t)sr);
    u32 ((uint32_t)sr * c * 4);
    u16 ((uint16_t)(c * 4));
    u16 (32);
    f.write ("data", 4);
    u32 (data);
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t k = 0; k < c; ++k)
            f.write ((const char*)&ch[k][i], 4);
    return (bool)f;
}

// dry through sweepr at the settings: both channels (a mono file on both), the latency removed
std::vector<std::vector<float>> render (const az::Audio& dry, const pk::SettingValues& values, bool still, double bpm)
{
    Engine e;
    e.prepare (dry.sampleRate, 512);
    for (const auto& [id, norm] : values)
        e.setParam (id, toPlain (id, norm));
    if (still)
    {
        e.setParam (bandParam (0, kLfoDepth), 0.0);
        e.setParam (bandParam (1, kLfoDepth), 0.0);
        e.setParam (kTapeOn, 0.0);
    }
    e.reset ();
    const size_t n = dry.frames (), lat = (size_t)e.latency ();
    const std::vector<float>& inL = dry.channels[0];
    const std::vector<float>& inR = dry.channels.size () > 1 ? dry.channels[1] : dry.channels[0];
    std::vector<float> l (n + lat, 0.0f), r (n + lat, 0.0f), xl (n + lat, 0.0f), xr (n + lat, 0.0f);
    std::copy (inL.begin (), inL.end (), xl.begin ());
    std::copy (inR.begin (), inR.end (), xr.begin ());
    for (size_t a = 0; a < n + lat; a += 512)
    {
        const int m = (int)std::min<size_t> (512, n + lat - a);
        e.setTransport (bpm, (double)a / dry.sampleRate * bpm / 60.0, true);
        e.process (xl.data () + a, xr.data () + a, l.data () + a, r.data () + a, m);
    }
    return {std::vector<float> (l.begin () + (long)lat, l.end ()), std::vector<float> (r.begin () + (long)lat, r.end ())};
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
        return usage ();
    std::string presetPath, outPath;
    bool still = false;
    double bpm = 120.0;
    for (int i = 3; i < argc; ++i)
    {
        if (!std::strcmp (argv[i], "--preset") && i + 1 < argc)
            presetPath = argv[++i];
        else if (!std::strcmp (argv[i], "--out") && i + 1 < argc)
            outPath = argv[++i];
        else if (!std::strcmp (argv[i], "--bpm") && i + 1 < argc)
            bpm = std::atof (argv[++i]);
        else if (!std::strcmp (argv[i], "--static"))
            still = true;
        else
            return usage ();
    }
    az::Audio a, b;
    std::string err;
    if (!az::readWav (argv[1], a, err) || !az::readWav (argv[2], b, err))
    {
        std::fprintf (stderr, "%s\n", err.c_str ());
        return 1;
    }
    if (a.sampleRate != b.sampleRate)
        std::fprintf (stderr, "warning: the sample rates differ (%.0f and %.0f Hz)\n", a.sampleRate, b.sampleRate);
    const std::vector<float> am = a.mono (), bm = b.mono ();

    if (presetPath.empty ())
    {
        const long d = az::delayOf (am, bm);
        const auto bands = az::response (am, bm, d, a.sampleRate);
        const az::Null nl = az::nullDepth (am, bm, d);
        std::printf ("# %s against %s: b %ld samples later\n# Hz        dB    coherence\n", argv[2], argv[1], d);
        for (const auto& x : bands)
            std::printf ("%8.1f  %+7.2f  %.3f\n", x.centreHz, x.db, x.coherence);
        std::printf ("# null depth %.1f dB (with a scaled by %.3f: %.1f dB)\n", nl.raw, nl.gain, nl.matched);
        return 0;
    }

    std::ifstream f (presetPath);
    if (!f)
    {
        std::fprintf (stderr, "cannot open %s\n", presetPath.c_str ());
        return 1;
    }
    std::stringstream ss;
    ss << f.rdbuf ();
    pk::presets::FactoryPreset fp;
    if (!pk::presets::parseFactoryPreset (ss.str (), "Analyze/settings.txt", paramTable (), fp, err))
    {
        std::fprintf (stderr, "%s: %s\n", presetPath.c_str (), err.c_str ());
        return 1;
    }
    az::Audio s;
    s.sampleRate = a.sampleRate;
    s.channels = render (a, fp.values, still, bpm);
    if (!outPath.empty () && !writeFloatWav (outPath, s.channels, s.sampleRate))
        std::fprintf (stderr, "cannot write %s\n", outPath.c_str ());
    const std::vector<float> sm = s.mono ();
    const long dRef = az::delayOf (am, bm), dSw = az::delayOf (am, sm), dPair = az::delayOf (sm, bm);
    const auto ref = az::response (am, bm, dRef, a.sampleRate), sw = az::response (am, sm, dSw, a.sampleRate);
    const az::Null nl = az::nullDepth (sm, bm, dPair);
    std::printf ("# the reference (%s, %ld samples later than the dry) and sweepr (%s%s) against the dry signal\n", argv[2], dRef,
                 presetPath.c_str (), still ? ", static" : "");
    std::printf ("# Hz      reference   sweepr    difference (sweepr - reference)   coherence (reference)\n");
    double worst = 0.0, sum2 = 0.0;
    for (size_t i = 0; i < ref.size () && i < sw.size (); ++i)
    {
        const double diff = sw[i].db - ref[i].db;
        worst = std::max (worst, std::fabs (diff));
        sum2 += diff * diff;
        std::printf ("%8.1f  %+8.2f  %+8.2f  %+8.2f  %.3f\n", ref[i].centreHz, ref[i].db, sw[i].db, diff, ref[i].coherence);
    }
    std::printf ("# difference: at most %.2f dB, RMS %.2f dB over %zu bands\n", worst, std::sqrt (sum2 / std::max<size_t> (1, ref.size ())),
                 ref.size ());
    std::printf ("# null depth sweepr against the reference: %.1f dB (reference %ld samples later; with sweepr scaled by %.3f: %.1f dB)\n", nl.raw,
                 dPair, nl.gain, nl.matched);
    return 0;
}
