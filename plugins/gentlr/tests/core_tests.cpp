// Headless tests for the Gentlr DSP. Run: ./gentlr_tests [filter]
#include "Engine.h"
#include "Params.h"

#include "smacheratr/src/core/ClarityBand.h"
#include "smacheratr/src/core/Glue.h"
#include "smacheratr/src/core/NoOverlap.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace gentlr;

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
#define TEST(name)                     \
    static void name ();               \
    static Reg reg_##name (#name, name); \
    static void name ()

constexpr double kSr = 48000.0;

struct Sig
{
    std::vector<float> l, r;
};

// Gentlr alone (no end saturator), with `set` applied before it starts. Its bands with the 12 / 12 Slope
// (the default up to 0.24; Signature now): the cuts the tests expect are that shape's, exactly the Range
// at a band's centre (the Slopes have tests of their own).
static std::unique_ptr<Engine> engine (const std::function<void (Engine&)>& set = {}, double sr = kSr, Meters* meters = nullptr)
{
    auto e = std::make_unique<Engine> (false);
    e->setParam (kSlope, smacheratr::kSlope12);
    if (set)
        set (*e);
    e->setMeters (meters);
    e->prepare (sr, 512);
    return e;
}

// sines (Hz, dB peak) on both channels, `secs` long
static Sig tones (const std::vector<std::pair<double, double>>& parts, double secs, double sr = kSr)
{
    Sig s;
    const size_t n = (size_t)(secs * sr);
    s.l.assign (n, 0.0f);
    for (const auto& [hz, db] : parts)
    {
        const double a = std::pow (10.0, db / 20.0);
        for (size_t i = 0; i < n; ++i)
            s.l[i] += (float)(a * std::sin (2.0 * M_PI * hz * (double)i / sr));
    }
    s.r = s.l;
    return s;
}

static Sig noise (double secs, double amp, uint32_t seed = 5)
{
    Sig s;
    const size_t n = (size_t)(secs * kSr);
    s.l.resize (n);
    s.r.resize (n);
    for (size_t i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        s.l[i] = (float)(amp * ((double)(seed >> 8) / 8388608.0 - 1.0));
        seed = seed * 1664525u + 1013904223u;
        s.r[i] = (float)(amp * ((double)(seed >> 8) / 8388608.0 - 1.0));
    }
    return s;
}

static Sig run (Engine& e, const Sig& in, int block = 480)
{
    Sig out = in;
    for (size_t pos = 0; pos < in.l.size (); pos += (size_t)block)
    {
        const int m = (int)std::min ((size_t)block, in.l.size () - pos);
        e.process (out.l.data () + pos, out.r.data () + pos, out.l.data () + pos, out.r.data () + pos, m);
    }
    return out;
}

// a tone's level (dB peak) in x from sample `from` on
static double toneDb (const std::vector<float>& x, double hz, size_t from, double sr = kSr)
{
    std::complex<double> acc (0.0, 0.0);
    double wsum = 0.0;
    const size_t n = x.size () - from;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double)i / (double)n);
        const double ph = -2.0 * M_PI * hz * (double)(from + i) / sr;
        acc += w * (double)x[from + i] * std::complex<double> (std::cos (ph), std::sin (ph));
        wsum += w;
    }
    return 20.0 * std::log10 (2.0 * std::abs (acc) / wsum + 1e-12);
}

// the largest difference between out and the input delayed by `lat`
static double diffDelayed (const Sig& in, const Sig& out, int lat)
{
    double m = 0.0;
    for (size_t i = (size_t)lat; i < in.l.size (); ++i)
    {
        m = std::max (m, (double)std::fabs (out.l[i] - in.l[i - (size_t)lat]));
        m = std::max (m, (double)std::fabs (out.r[i] - in.r[i - (size_t)lat]));
    }
    return m;
}

static double maxStep (const std::vector<float>& x, size_t a, size_t b)
{
    double m = 0.0;
    for (size_t i = std::max<size_t> (a, 1); i < std::min (b, x.size ()); ++i)
        m = std::max (m, (double)std::fabs (x[i] - x[i - 1]));
    return m;
}

TEST (parameters_and_defaults)
{
    const auto& t = paramTable ();
    CHECK (t.size () == kNumParams, "every parameter: %u of %u", (unsigned)t.size (), (unsigned)kNumParams);
    for (uint32_t id = 0; id < t.size (); ++id)
        CHECK (t.info (id).id == id, "id %u in its place", id);
    CHECK (kTailExtBase == kTailBase + pk::kTailFields && kTailExt2Base == kTailExtBase + pk::kTailExtFields &&
               kHighOn == kTailExt2Base + pk::kTailExt2Fields && kSlope == kTailExt3Base + pk::kTailExt3Fields && kGlue12 == kSlope + 1 &&
               kTailExt4Base == kGlue2High + 1 && kNumParams == kTailExt4Base + pk::kTailExt4Fields,
           "the end saturator's three blocks, one after the other, then the High band and No Overlap, then its fourth block, then the "
           "Slope, the glue switches and the end saturator's fifth block");
    CHECK (std::string (t.info (kTailExt2Base + pk::kTailExt2Advanced).name) == "Saturator Gentlr Advanced", "the third block");
    CHECK (std::string (t.info (kTailExt3Base + pk::kTailExt3High).name) == "Saturator Gentlr High (unused)", "the last block");
    // every end saturator parameter reaches the tail's field it stands for, and only those are tail parameters
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        const int f = smacheratr::tailFieldIn (id, {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, kTailExt4Base});
        CHECK (isTailParam (id) == (f >= 0) && (f < 0 || tailField (id) == (uint32_t)f), "ID %u: tail field %d", id, f);
    }
    CHECK (t.info (kTailBase + pk::kTailOn).def == 1.0, "the end Smacheratr on");
    CHECK (t.info (kAdvanced).def == 1.0 && t.info (kDrive).def == 0.0 && t.info (kDriveAmount).def == 12.0, "Advanced on (the Thresholds show), Drive off (12 dB)");
    CHECK (t.info (kAttack).def == 15.0 && t.info (kRelease).def == 150.0, "Smacheratr's detector times");
    CHECK (t.info (kStereo).def == (double)kStereoLinked && t.info (kMix).def == 1.0 && t.info (kOutput).def == 0.0, "stereo, 100 %%, 0 dB");
    CHECK (t.info (bandParam (0, kOn)).def == 1.0 && t.info (bandParam (0, kFreq)).def == 250.0 && t.info (bandParam (0, kRange)).def == 8.0 &&
               t.info (bandParam (1, kOn)).def == 1.0 && t.info (bandParam (1, kFreq)).def == 3000.0 && t.info (bandParam (1, kRange)).def == 6.0,
           "band 1 on the low mids, band 2 on the upper mids");
    // the Thresholds share Smacheratr's range exactly (its sliders work on normalized values)
    const auto& sm = smacheratr::paramTable ();
    for (int k = 0; k < kBands; ++k)
    {
        const auto& a = t.info (bandParam (k, kThreshold));
        const auto& b = sm.info (smacheratr::kClarityThresholdIds[k]);
        CHECK (a.min == b.min && a.max == b.max && a.def == b.def && a.curve == b.curve, "band %d's Threshold is Smacheratr's", k + 1);
        CHECK (fromSmacheratr (smacheratr::kClarityThresholdIds[k]) == (int64_t)bandParam (k, kThreshold), "slider %d mapped", k + 1);
    }
    // the Sub band: Smacheratr's, its Range 0 by default (it cuts nothing until it gets one; no button)
    CHECK (t.info (kSubOn).def == 0.0 && t.info (kSubFreq).def == smacheratr::kSubDefaultHz && t.info (kSubRange).def == 0.0 &&
               t.info (kSubThreshold).def == smacheratr::kClarityThresholdDb,
           "Sub at Range 0, 40 Hz, -18 dB");
    CHECK (!hasOn (kSub) && !hasOn (kHigh) && hasOn (0) && hasOn (1), "Sub and High have no On of their own");
    for (uint32_t id : {(uint32_t)kSubFreq, (uint32_t)kSubRange, (uint32_t)kSubThreshold})
    {
        const auto& a = t.info (id);
        const auto& b = sm.info (id == kSubFreq    ? smacheratr::kClaritySubFreq
                                 : id == kSubRange ? smacheratr::kClaritySubRange
                                                   : smacheratr::kClaritySubThreshold);
        CHECK (a.min == b.min && a.max == b.max && a.curve == b.curve, "%s is Smacheratr's", a.name);
    }
    CHECK (fromSmacheratr (smacheratr::kClaritySubThreshold) == (int64_t)kSubThreshold && fromSmacheratr (smacheratr::kClarityAdvanced) == kAdvanced &&
               fromSmacheratr (smacheratr::kDrive) == -1,
           "the Sub slider mapped");
    for (int k = 0; k < kAllBands; ++k)
        CHECK (fromSmacheratr (smacheratr::kGentlrThresholdIds[k]) == (int64_t)thresholdParam (k), "slider %d on its band", k);
    // the IDs are persisted in projects (also pinned with static_asserts in Params.h): each name at its number
    const std::pair<uint32_t, const char*> pinned[] = {
        {0, "Advanced"},          {1, "Drive"},           {2, "Drive Amount"},          {3, "Attack"},
        {4, "Release"},           {5, "Stereo"},          {6, "Mix"},                   {7, "Output"},
        {8, "Band 1 On"},         {9, "Band 1 Frequency"}, {10, "Band 1 Width"},        {11, "Band 1 Range"},
        {12, "Band 1 Threshold"}, {13, "Band 2 On"},      {17, "Band 2 Threshold"},     {18, "Sub (unused)"},
        {19, "Sub Frequency"},    {20, "Sub Range"},      {21, "Sub Threshold"},        {22, "Saturator"},
        {28, "Saturator Output"}, {45, "Saturator Gentlr Advanced"}, {53, "Saturator Gentlr Sub Threshold"},
        {54, "High (unused)"},             {55, "High Frequency"}, {56, "High Range"},           {57, "High Threshold"},
        {58, "No Overlap"},       {59, "Saturator Gentlr High (unused)"}, {63, "Saturator Gentlr No Overlap"},
        {64, "Saturator Gentlr Slope"}, {65, "Slope"}, {66, "Glue 1 / 2"}, {67, "Glue Sub / 1"}, {68, "Glue Sub / 2"},
        {69, "Glue 1 / High"}, {70, "Glue 2 / High"}, {71, "Saturator Gentlr Glue 1 / 2"}, {75, "Saturator Gentlr Glue 2 / High"}};
    for (const auto& [id, name] : pinned)
        CHECK (std::string (t.info (id).name) == name, "ID %u is %s (%s)", id, name, t.info (id).name);
    CHECK (kNumParams == 76, "76 parameters: %u", (unsigned)kNumParams);
    std::printf ("    %u parameters (bands at %u, Sub at %u, tail at %u, tail ext at %u, Gentlr block at %u)\n", (unsigned)kNumParams,
                 (unsigned)kBandBase, (unsigned)kSubOn, (unsigned)kTailBase, (unsigned)kTailExtBase, (unsigned)kTailExt2Base);
}

TEST (bands_off_is_the_input_delayed)
{
    // with both bands' Range at 0 (or off) the output is the input, delayed by the latency, bit for bit
    const Sig in = noise (0.5, 0.8);
    for (int how = 0; how < 3; ++how)
    {
        auto e = engine ([how] (Engine& en) {
            en.setParam (kSubOn, how == 0 ? 1.0 : 0.0); // (the Sub band's old On: unused, its Range 0 is what counts)
            for (int k = 0; k < kAllBands; ++k)
            {
                if (how == 0)
                    en.setParam (rangeParam (k), 0.0);
                else
                    en.setParam (onParam (k), 0.0);
            }
            if (how == 2)
            {
                en.setParam (kAdvanced, 1.0); // (the region Drive needs a working band)
                en.setParam (kDrive, 1.0);
                en.setParam (kDriveAmount, 36.0);
            }
        });
        const Sig out = run (*e, in, 333);
        const double d = diffDelayed (in, out, e->latency ());
        std::printf ("    %s: latency %d, largest difference %g\n", how == 0 ? "Range 0" : how == 1 ? "bands off" : "bands off, Drive on",
                     e->latency (), d);
        CHECK (d == 0.0, "bit for bit: %g", d);
    }
    // quiet enough that the default bands never cut: untouched as well
    {
        auto e = engine ();
        const Sig quiet = noise (0.5, 0.02);
        const double d = diffDelayed (quiet, run (*e, quiet), e->latency ());
        CHECK (d == 0.0, "under the threshold, bit for bit: %g", d);
    }
    // the Mid/Side modes make a round trip through mid and side: all but exact
    for (int mode : {kMidSide, kMidOnly, kSideOnly})
    {
        auto e = engine ([mode] (Engine& en) {
            en.setParam (kStereo, mode);
            for (int k = 0; k < kBands; ++k)
                en.setParam (bandParam (k, kRange), 0.0);
        });
        const double d = diffDelayed (in, run (*e, in), e->latency ());
        CHECK (d < 1e-6, "mode %d: %g", mode, d);
    }
}

TEST (a_loud_band_is_cut_by_its_range)
{
    // band 1 at 250 Hz with Range 8 dB, band 2 off; a loud tone at the band's centre is turned down by
    // about the Range, and a tone far above it is left alone
    Meters m;
    auto measure = [&] (double hz, double db, double range, double* meterDb = nullptr) {
        auto e = engine (
            [range] (Engine& en) {
                en.setParam (bandParam (0, kRange), range);
                en.setParam (bandParam (1, kOn), 0.0);
            },
            kSr, &m);
        const Sig in = tones ({{hz, db}}, 1.0);
        const Sig out = run (*e, in);
        if (meterDb)
            *meterDb = m.bands.clarityDb.load ();
        return toneDb (out.l, hz, 24000) - toneDb (in.l, hz, 24000);
    };
    double meter = 0.0;
    const double cut = measure (250.0, -1.0, 8.0, &meter);
    // the band's cut is 8 dB at its peak; the band's phase there decides the tone's drop (the default
    // Slope, 12 / 12, is symmetric: its phases cancel at the centre and the drop is the whole 8 dB)
    const smacheratr::ClarityBand b = smacheratr::clarityBand (kSr, 250.0, 2.0, smacheratr::kSlope12);
    const double g = std::pow (10.0, -8.0 / 20.0);
    std::complex<double> h (1.0, 0.0);
    {
        const std::complex<double> z1 = std::polar (1.0, -2.0 * M_PI * 250.0 / kSr), z2 = z1 * z1;
        auto H = [&] (const smacheratr::BiquadCoeffs& c) { return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2); };
        h = H (b.hp) * (b.hp2On ? H (b.hp2) : 1.0) * H (b.lp) * b.norm;
    }
    const double expect = 20.0 * std::log10 (std::abs (1.0 - (1.0 - g) * h));
    std::printf ("    250 Hz at -1 dB: %.2f dB (the band's maths: %.2f dB), meter %.2f dB\n", cut, expect, meter);
    CHECK (std::fabs (cut - expect) < 0.3, "cut as the band's maths says: %.2f vs %.2f dB", cut, expect);
    CHECK (cut < -5.0 && cut > -8.5, "about the Range: %.2f dB", cut);
    CHECK (std::fabs (meter + 8.0) < 0.01, "the meter reads the Range: %.2f dB", meter);
    // a smaller Range limits the cut
    const double cut4 = measure (250.0, -1.0, 4.0, &meter);
    CHECK (cut4 < -2.0 && cut4 > -4.3 && std::fabs (meter + 4.0) < 0.01, "Range 4: %.2f dB (meter %.2f)", cut4, meter);
    // a tone far above the band (5 kHz, -6 dB): the band hears it 20 dB down, under the threshold
    const double other = measure (5000.0, -6.0, 8.0, &meter);
    std::printf ("    5 kHz at -6 dB: %.3f dB, meter %.2f dB\n", other, meter);
    CHECK (std::fabs (other) < 0.01 && meter == 0.0, "untouched: %.3f dB", other);
    // a quiet tone in the band (-24 dB, under -18): untouched
    const double quiet = measure (250.0, -24.0, 8.0, &meter);
    CHECK (std::fabs (quiet) < 0.01 && meter == 0.0, "quiet in the band: %.3f dB", quiet);
    // Smacheratr's law: 3 dB for every 5 the band's level (as the detector reads it: it rides the
    // peaks, fast up and slow down, so a steady tone reads about 2 dB over its peak level) is over the
    // threshold (-18 dB)
    measure (250.0, -15.0, 8.0, &meter);
    const double level = m.bands.clarityLevelDb.load ();
    std::printf ("    250 Hz at -15 dB: read as %.2f dB, cut %.2f dB\n", level, meter);
    CHECK (level > -15.5 && level < -12.0, "the level read: %.2f dB", level);
    CHECK (std::fabs (meter + 0.6 * (level + 18.0)) < 0.3, "3 dB for every 5 over: %.2f at %.2f dB over", meter, level + 18.0);
}

TEST (band_two_and_width)
{
    // band 2 at 3 kHz (default), a loud tone there is cut; band 1 (250 Hz) stays out of it (it hears
    // the 3 kHz tone through its top, 12 dB/oct with the default Slope: under its threshold)
    Meters m;
    auto e = engine ({}, kSr, &m);
    const Sig in = tones ({{3000.0, -8.0}, {250.0, -30.0}}, 1.0);
    const Sig out = run (*e, in);
    const double c2 = toneDb (out.l, 3000.0, 24000) - toneDb (in.l, 3000.0, 24000);
    const double c1 = toneDb (out.l, 250.0, 24000) - toneDb (in.l, 250.0, 24000);
    std::printf ("    3 kHz: %.2f dB (meter %.2f), 250 Hz: %.2f dB (meter %.2f)\n", c2, m.bands.clarity2Db.load (), c1,
                 m.bands.clarityDb.load ());
    CHECK (c2 < -4.0 && std::fabs (m.bands.clarity2Db.load () + 6.0) < 0.01, "band 2 cuts its tone: %.2f dB", c2);
    CHECK (std::fabs (c1) < 0.3 && m.bands.clarityDb.load () == 0.0f, "band 1 hears nothing loud: %.2f dB", c1);
    // a narrow band 2 hardly touches a tone an octave and a half below it; a wide one cuts it too (the
    // narrow one lifts it a little: under its 12 dB/oct high-pass the band's phase turns past 90
    // degrees, so taking the band away adds a touch there; Smacheratr's band does the same)
    auto away = [&] (double width) {
        auto en = engine ([width] (Engine& x) {
            x.setParam (bandParam (0, kOn), 0.0);
            x.setParam (bandParam (1, kWidth), width);
            x.setParam (bandParam (1, kRange), 12.0);
        });
        const Sig a = tones ({{3000.0, -1.0}, {1060.0, -20.0}}, 1.0);
        const Sig o = run (*en, a);
        return toneDb (o.l, 1060.0, 24000) - toneDb (a.l, 1060.0, 24000);
    };
    const double narrow = away (0.5), wide = away (4.0);
    std::printf ("    1.06 kHz beside a loud 3 kHz: width 0.5: %.2f dB, width 4: %.2f dB\n", narrow, wide);
    CHECK (std::fabs (narrow) < 1.5 && wide < -0.5, "the width reaches: %.2f vs %.2f dB", narrow, wide);
}

// what a band does to a tone at hz while it cuts cutDb at its peak (x + (g - 1) * band, with its phase)
static double bandMathDb (const smacheratr::ClarityBand& b, double hz, double cutDb)
{
    const std::complex<double> z1 = std::polar (1.0, -2.0 * M_PI * hz / kSr), z2 = z1 * z1;
    auto H = [&] (const smacheratr::BiquadCoeffs& c) { return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2); };
    const double g = std::pow (10.0, -cutDb / 20.0);
    return 20.0 * std::log10 (std::abs (1.0 - (1.0 - g) * H (b.hp) * H (b.lp) * b.norm));
}

TEST (sub_band)
{
    // the Sub band alone (bands 1 and 2 off): from 20 Hz up to its Freq, then tapering off
    Meters m;
    auto measure = [&] (double hz, double db, const std::function<void (Engine&)>& set) {
        auto e = engine (
            [&] (Engine& en) {
                en.setParam (bandParam (0, kOn), 0.0);
                en.setParam (bandParam (1, kOn), 0.0);
                en.setParam (kSubRange, 8.0); // (the band works once it has a Range)
                if (set)
                    set (en);
            },
            kSr, &m);
        const Sig in = tones ({{hz, db}}, 1.5);
        const Sig out = run (*e, in);
        return toneDb (out.l, hz, 36000) - toneDb (in.l, hz, 36000);
    };
    // at Range 0 by default: a loud 40 Hz passes (bit for bit: the bands off too)
    {
        auto e = engine (
            [] (Engine& en) {
                en.setParam (bandParam (0, kOn), 0.0);
                en.setParam (bandParam (1, kOn), 0.0);
            },
            kSr, &m);
        const Sig in = tones ({{40.0, -1.0}}, 0.5);
        const double d = diffDelayed (in, run (*e, in), e->latency ());
        CHECK (d == 0.0 && m.bands.claritySubDb.load () == 0.0f, "Sub at its default Range 0: untouched (%g)", d);
    }
    // with a Range: a loud 40 Hz tone is cut by the Range (8 dB at the band's peak), as the band's maths says
    const double cut = measure (40.0, -1.0, {});
    const double meter = m.bands.claritySubDb.load ();
    const double expect = bandMathDb (smacheratr::subBand (kSr, smacheratr::kSubDefaultHz), 40.0, 8.0);
    std::printf ("    40 Hz at -1 dB: %.2f dB (the band's maths %.2f dB), meter %.2f dB, level %.2f dB\n", cut, expect, meter,
                 (double)m.bands.claritySubLevelDb.load ());
    CHECK (std::fabs (cut - expect) < 0.3 && cut < -6.0, "cut by the Range: %.2f vs %.2f dB", cut, expect);
    CHECK (std::fabs (meter + 8.0) < 0.01, "the meter reads the Range: %.2f dB", meter);
    const double cut3 = measure (40.0, -1.0, [] (Engine& en) { en.setParam (kSubRange, 3.0); });
    CHECK (std::fabs (cut3 - bandMathDb (smacheratr::subBand (kSr, smacheratr::kSubDefaultHz), 40.0, 3.0)) < 0.3, "Range 3 dB: %.2f dB", cut3);
    // a loud tone well above it (1 kHz), and a quiet one in it (-24 dB): untouched
    const double high = measure (1000.0, -1.0, {});
    CHECK (std::fabs (high) < 0.01 && m.bands.claritySubDb.load () == 0.0f, "1 kHz untouched: %.3f dB", high);
    const double quiet = measure (40.0, -24.0, {});
    CHECK (std::fabs (quiet) < 0.01, "a quiet sub untouched: %.3f dB", quiet);
    // the Freq sets where it tapers: a loud 90 Hz tone is nearly left alone with the shelf tapering at
    // 20 Hz (its tail, two octaves up), cut with it reaching 100 Hz
    const double at20 = measure (90.0, -6.0, [] (Engine& en) { en.setParam (kSubFreq, 20.0); });
    const double at100 = measure (90.0, -6.0, [] (Engine& en) { en.setParam (kSubFreq, 100.0); });
    std::printf ("    90 Hz at -6 dB: Sub Freq 20 Hz %.2f dB, 100 Hz %.2f dB\n", at20, at100);
    CHECK (std::fabs (at20) < 1.0 && at100 < -6.0, "the taper point: %.2f / %.2f dB", at20, at100);
    // the Range at 0: nothing; its old On is unused (off, the band still works)
    CHECK (std::fabs (measure (40.0, -1.0, [] (Engine& en) { en.setParam (kSubRange, 0.0); })) < 1e-4, "Range 0");
    CHECK (std::fabs (measure (40.0, -1.0, [] (Engine& en) { en.setParam (kSubOn, 0.0); }) - cut) < 1e-4, "its old On does nothing");
    // Advanced: its Threshold (a -24 dB sub: under -18 without it, the whole Range with the Threshold at -40)
    const double plain = measure (40.0, -24.0, [] (Engine& en) {
        en.setParam (kAdvanced, 0.0); // (on by default)
        en.setParam (kSubThreshold, -40.0);
    });
    const double adv = measure (40.0, -24.0, [] (Engine& en) {
        en.setParam (kAdvanced, 1.0);
        en.setParam (kSubThreshold, -40.0);
    });
    std::printf ("    40 Hz at -24 dB, Threshold -40: Advanced off %.2f dB, on %.2f dB (meter %.2f)\n", plain, adv,
                 (double)m.bands.claritySubDb.load ());
    CHECK (std::fabs (plain) < 0.01 && adv < -6.0 && std::fabs (m.bands.claritySubDb.load () + 8.0) < 0.01,
           "the Sub Threshold works with Advanced");
    // with bands 1 and 2 on, a loud sub and a loud low mid: each band cuts its own
    {
        Meters mm;
        auto e = engine ([] (Engine& en) { en.setParam (kSubRange, 8.0); }, kSr, &mm);
        const Sig in = tones ({{40.0, -6.0}, {250.0, -6.0}}, 1.5);
        const Sig out = run (*e, in);
        const double c40 = toneDb (out.l, 40.0, 36000) - toneDb (in.l, 40.0, 36000);
        const double c250 = toneDb (out.l, 250.0, 36000) - toneDb (in.l, 250.0, 36000);
        std::printf ("    40 + 250 Hz: %.2f / %.2f dB (meters Sub %.2f, band 1 %.2f, band 2 %.2f)\n", c40, c250,
                     (double)mm.bands.claritySubDb.load (), (double)mm.bands.clarityDb.load (), (double)mm.bands.clarity2Db.load ());
        CHECK (c40 < -4.0 && c250 < -4.0 && mm.bands.claritySubDb.load () < -6.0 && mm.bands.clarityDb.load () < -6.0 &&
                   mm.bands.clarity2Db.load () == 0.0f,
               "both cut, band 2 idle");
    }
    // the region Drive takes the Sub band's region too: harmonics of a loud 40 Hz (120 Hz)
    auto drive = [&] (bool on) {
        auto e = engine ([on] (Engine& en) {
            en.setParam (bandParam (0, kOn), 0.0);
            en.setParam (bandParam (1, kOn), 0.0);
            en.setParam (kSubRange, 8.0);
            en.setParam (kAdvanced, 1.0);
            en.setParam (kSubThreshold, -40.0);
            en.setParam (kDrive, on ? 1.0 : 0.0);
            en.setParam (kDriveAmount, 30.0);
        });
        const Sig in = tones ({{40.0, -12.0}}, 1.5);
        return toneDb (run (*e, in).l, 120.0, 36000);
    };
    const double h0 = drive (false), h1 = drive (true);
    std::printf ("    120 Hz (the 3rd of 40 Hz): Drive off %.1f dB, on %.1f dB\n", h0, h1);
    CHECK (h1 > h0 + 20.0 && h1 > -60.0, "the Sub's region driven: %.1f vs %.1f dB", h1, h0);
}

TEST (high_band)
{
    // the High band alone (bands 1 and 2 off): from its Freq up to the very top, the Sub band's mirror
    Meters m;
    auto measure = [&] (double hz, double db, const std::function<void (Engine&)>& set) {
        auto e = engine (
            [&] (Engine& en) {
                en.setParam (bandParam (0, kOn), 0.0);
                en.setParam (bandParam (1, kOn), 0.0);
                en.setParam (kHighRange, 6.0); // (the band works once it has a Range)
                if (set)
                    set (en);
            },
            kSr, &m);
        const Sig in = tones ({{hz, db}}, 1.5);
        const Sig out = run (*e, in);
        return toneDb (out.l, hz, 36000) - toneDb (in.l, hz, 36000);
    };
    const auto& t = paramTable ();
    CHECK (t.info (kHighOn).def == 0.0 && t.info (kHighFreq).def == smacheratr::kHighDefaultHz && t.info (kHighFreq).min == 2000.0 &&
               t.info (kHighFreq).max == 16000.0 && t.info (kHighRange).def == 0.0 && t.info (kHighThreshold).def == -18.0 &&
               t.info (kNoOverlap).def == 0.0,
           "High at Range 0, 7 kHz (2 - 16 kHz), Threshold -18 dB; No Overlap off");
    // at Range 0 by default: a loud 10 kHz passes (bit for bit: the bands off too)
    {
        auto e = engine (
            [] (Engine& en) {
                en.setParam (bandParam (0, kOn), 0.0);
                en.setParam (bandParam (1, kOn), 0.0);
            },
            kSr, &m);
        const Sig in = tones ({{10000.0, -1.0}}, 0.5);
        const double d = diffDelayed (in, run (*e, in), e->latency ());
        CHECK (d == 0.0 && m.bands.clarityHighDb.load () == 0.0f, "High at its default Range 0: untouched (%g)", d);
    }
    // with a Range: a loud 10 kHz tone is cut by the Range (6 dB), as the band's maths says
    const double cut = measure (10000.0, -1.0, {});
    const double meter = m.bands.clarityHighDb.load ();
    const double expect = bandMathDb (smacheratr::highBand (kSr, smacheratr::kHighDefaultHz), 10000.0, 6.0);
    std::printf ("    10 kHz at -1 dB: %.2f dB (the band's maths %.2f dB), meter %.2f dB, level %.2f dB\n", cut, expect, meter,
                 (double)m.bands.clarityHighLevelDb.load ());
    CHECK (std::fabs (cut - expect) < 0.3 && cut < -4.5, "cut by the Range: %.2f vs %.2f dB", cut, expect);
    CHECK (std::fabs (meter + 6.0) < 0.01, "the meter reads the Range: %.2f dB", meter);
    // a loud tone well below it (300 Hz), and a quiet one in it (-24 dB): untouched
    const double low = measure (300.0, -1.0, {});
    CHECK (std::fabs (low) < 0.01 && m.bands.clarityHighDb.load () == 0.0f, "300 Hz untouched: %.3f dB", low);
    CHECK (std::fabs (measure (10000.0, -24.0, {})) < 0.01, "a quiet top untouched");
    // the Freq sets where it tapers: a loud 3 kHz tone nearly left alone with the shelf from 12 kHz, cut from 2 kHz
    const double at12k = measure (3000.0, -6.0, [] (Engine& en) { en.setParam (kHighFreq, 12000.0); });
    const double at2k = measure (3000.0, -6.0, [] (Engine& en) { en.setParam (kHighFreq, 2000.0); });
    std::printf ("    3 kHz at -6 dB: High Freq 12 kHz %.2f dB, 2 kHz %.2f dB\n", at12k, at2k);
    CHECK (std::fabs (at12k) < 1.0 && at2k < -4.0, "the taper point: %.2f / %.2f dB", at12k, at2k);
    CHECK (std::fabs (measure (10000.0, -1.0, [] (Engine& en) { en.setParam (kHighRange, 0.0); })) < 1e-4, "Range 0");
    CHECK (std::fabs (measure (10000.0, -1.0, [] (Engine& en) { en.setParam (kHighOn, 0.0); }) - cut) < 1e-4, "its old On does nothing");
    // Advanced: its Threshold
    const double adv = measure (10000.0, -24.0, [] (Engine& en) {
        en.setParam (kAdvanced, 1.0);
        en.setParam (kHighThreshold, -40.0);
    });
    CHECK (adv < -4.5 && std::fabs (m.bands.clarityHighDb.load () + 6.0) < 0.01, "the High Threshold works with Advanced (%.2f dB)", adv);
    // High at Range 0 is Gentlr as it was: the same output (to the bit) whatever its other controls say
    // (its old On too), with every other band working
    auto render = [] (bool on, double freq, double threshold) {
        auto e = engine ([&] (Engine& en) {
            en.setParam (kSubRange, 8.0);
            en.setParam (kHighOn, on ? 0.0 : 1.0); // (unused: the opposite, to show it)
            en.setParam (kHighFreq, freq);
            en.setParam (kHighRange, on ? 6.0 : 0.0);
            en.setParam (kHighThreshold, threshold);
        });
        return run (*e, tones ({{45.0, -6.0}, {220.0, -6.0}, {3200.0, -8.0}, {9000.0, -8.0}}, 0.5)).l;
    };
    CHECK (render (false, 7000.0, -18.0) == render (false, 2500.0, -50.0), "High at Range 0 ignores its controls, to the bit");
    CHECK (render (true, 7000.0, -18.0) != render (false, 7000.0, -18.0), "High with a Range changes the sound");
}

TEST (no_overlap)
{
    // band 1 at 250 Hz and band 2 at 400 Hz, both 2 octaves wide: they overlap (200 - 500 Hz)
    auto set = [] (bool noOverlap, double f1, double w1, double f2, double w2) {
        return [=] (Engine& en) {
            en.setParam (bandParam (0, kFreq), f1);
            en.setParam (bandParam (0, kWidth), w1);
            en.setParam (bandParam (1, kFreq), f2);
            en.setParam (bandParam (1, kWidth), w2);
            en.setParam (kNoOverlap, noOverlap ? 1.0 : 0.0);
        };
    };
    const Sig in = tones ({{150.0, -6.0}, {320.0, -6.0}, {700.0, -6.0}, {3000.0, -6.0}}, 0.8);
    auto render = [&] (const std::function<void (Engine&)>& s) {
        auto e = engine (s);
        return run (*e, in).l;
    };
    // apart (the defaults: 250 Hz and 3 kHz), No Overlap changes nothing, to the bit
    CHECK (render (set (true, 250.0, 2.0, 3000.0, 2.0)) == render (set (false, 250.0, 2.0, 3000.0, 2.0)), "bands apart: the same sound");
    // overlapping: the engine keeps them apart, as smacheratr::resolveOverlaps says
    const auto kept = render (set (true, 250.0, 2.0, 400.0, 2.0));
    CHECK (kept != render (set (false, 250.0, 2.0, 400.0, 2.0)), "overlapping: No Overlap moves them");
    smacheratr::GentlrLayout l;
    l.on[0] = l.on[1] = true;
    l.freq[0] = 250.0, l.width[0] = 2.0, l.freq[1] = 400.0, l.width[1] = 2.0;
    l.freq[kSub] = smacheratr::kSubDefaultHz, l.freq[kHigh] = smacheratr::kHighDefaultHz;
    smacheratr::resolveOverlaps (l);
    CHECK (!smacheratr::bandsOverlap (l), "resolved");
    CHECK (kept == render (set (false, l.freq[0], l.width[0], l.freq[1], l.width[1])), "as if the bands had been set apart");
    // a band that does not work (off) takes no part
    auto off2 = [&] (bool noOverlap) {
        return render ([&] (Engine& en) {
            set (noOverlap, 250.0, 2.0, 400.0, 2.0) (en);
            en.setParam (bandParam (1, kOn), 0.0);
        });
    };
    CHECK (off2 (true) == off2 (false), "band 2 off: band 1 stays where it is");
    // the High band and band 2 overlapping: High's Freq is moved up to where band 2 ends (split at the middle)
    auto withHigh = [&] (bool noOverlap) {
        return render ([&] (Engine& en) {
            set (noOverlap, 250.0, 2.0, 5000.0, 2.0) (en);
            en.setParam (kHighRange, 6.0);
            en.setParam (kHighFreq, 4000.0);
        });
    };
    CHECK (withHigh (true) != withHigh (false), "High over band 2: kept apart");
}

TEST (glue)
{
    // band 1 (125 - 500 Hz) glued to band 2 (1414 - 2828 Hz, apart): the engine holds band 2's low edge on
    // band 1's high edge (smacheratr::applyGlue); off, or touching, it changes nothing
    auto set = [] (bool glued, double f1, double w1, double f2, double w2) {
        return [=] (Engine& en) {
            en.setParam (bandParam (0, kFreq), f1);
            en.setParam (bandParam (0, kWidth), w1);
            en.setParam (bandParam (1, kFreq), f2);
            en.setParam (bandParam (1, kWidth), w2);
            en.setParam (kGlue12, glued ? 1.0 : 0.0);
        };
    };
    const Sig in = tones ({{150.0, -6.0}, {320.0, -6.0}, {700.0, -6.0}, {3000.0, -6.0}}, 0.8);
    auto render = [&] (const std::function<void (Engine&)>& s) {
        auto e = engine (s);
        return run (*e, in).l;
    };
    CHECK (render (set (true, 250.0, 2.0, 1000.0, 2.0)) == render (set (false, 250.0, 2.0, 1000.0, 2.0)), "touching: the same sound");
    const auto held = render (set (true, 250.0, 2.0, 2000.0, 1.0));
    smacheratr::GentlrLayout l;
    l.on[0] = l.on[1] = true;
    l.freq[0] = 250.0, l.width[0] = 2.0, l.freq[1] = 2000.0, l.width[1] = 1.0;
    l.freq[kSub] = smacheratr::kSubDefaultHz, l.freq[kHigh] = smacheratr::kHighDefaultHz;
    bool g[smacheratr::kGluePairs] {};
    g[smacheratr::kGlue12] = true;
    smacheratr::applyGlue (l, g);
    CHECK (held != render (set (false, 250.0, 2.0, 2000.0, 1.0)) && held == render (set (false, l.freq[0], l.width[0], l.freq[1], l.width[1])),
           "apart: as if band 2 had been set to the border");
    CHECK (fromSmacheratr (smacheratr::kClarityGlueSub2) == kGlueSub2 && kGlueIds[smacheratr::kGlue2High] == kGlue2High, "Smacheratr's glue IDs");
}

TEST (advanced_thresholds)
{
    Meters m;
    auto measure = [&] (bool advanced, double thresholdDb, double toneLevelDb, double* level = nullptr) {
        auto e = engine (
            [=] (Engine& en) {
                en.setParam (kAdvanced, advanced ? 1.0 : 0.0);
                en.setParam (bandParam (0, kThreshold), thresholdDb);
                en.setParam (bandParam (1, kOn), 0.0);
            },
            kSr, &m);
        const Sig in = tones ({{250.0, toneLevelDb}}, 0.8);
        const Sig out = run (*e, in);
        if (level)
            *level = m.bands.clarityLevelDb.load ();
        return std::make_pair ((double)m.bands.clarityDb.load (), diffDelayed (in, out, e->latency ()));
    };
    // a tone at -24 dB (read as about -22): under -18 without Advanced; with a Threshold of -10 under
    // it too; at -40 it is cut by the Range, at -27 by 3 dB for every 5 over
    double level = 0.0;
    auto [plainCut, plainDiff] = measure (false, -40.0, -24.0, &level);
    std::printf ("    a tone at -24 dB reads %.2f dB\n", level);
    CHECK (plainCut == 0.0 && plainDiff == 0.0, "Advanced off: the band's Threshold is not used (-18 dB): %.2f dB", plainCut);
    CHECK (level > -24.5 && level < -21.0, "the band's level reads the tone's: %.2f dB", level);
    auto [highCut, highDiff] = measure (true, -10.0, -24.0);
    CHECK (highCut == 0.0 && highDiff == 0.0, "under its Threshold: no cut (%.2f dB), the output untouched (%g)", highCut, highDiff);
    auto [lowCut, lowDiff] = measure (true, -40.0, -24.0);
    CHECK (std::fabs (lowCut + 8.0) < 0.01, "far over its Threshold: the whole Range: %.2f dB", lowCut);
    auto [midCut, midDiff] = measure (true, -27.0, -24.0);
    CHECK (std::fabs (midCut + 0.6 * (level + 27.0)) < 0.3, "3 dB for every 5 over: %.2f dB at %.2f dB over", midCut, level + 27.0);
    // a loud tone at -6 dB: cut without Advanced, not with the Threshold at 0 dB
    auto [loudPlain, d1] = measure (false, 0.0, -6.0);
    auto [loudHigh, d2] = measure (true, 0.0, -6.0);
    CHECK (loudPlain < -6.0 && loudHigh == 0.0, "-6 dB: %.2f dB without Advanced, %.2f dB with the Threshold at 0", loudPlain, loudHigh);
    (void)midDiff;
    (void)lowDiff;
    (void)d1;
    (void)d2;
}

TEST (region_drive_adds_harmonics_in_the_band)
{
    // a tone in band 1 (250 Hz) and one far above it (5.1 kHz): the region Drive gives the band
    // harmonics and hardly touches the other
    const Sig in = tones ({{250.0, -14.0}, {5100.0, -14.0}}, 1.0);
    auto render = [&] (bool drive) {
        auto e = engine ([drive] (Engine& en) {
            en.setParam (bandParam (0, kWidth), 1.0);
            en.setParam (bandParam (1, kOn), 0.0);
            en.setParam (kAdvanced, 1.0);
            en.setParam (bandParam (0, kThreshold), -40.0);
            en.setParam (kDrive, drive ? 1.0 : 0.0);
            en.setParam (kDriveAmount, 30.0);
        });
        return run (*e, in);
    };
    const Sig off = render (false), on = render (true);
    auto db = [] (const Sig& s, double f) { return toneDb (s.l, f, 24000); };
    std::printf ("    750 Hz (3rd harmonic): %.1f -> %.1f dB, 1250 Hz (5th): %.1f -> %.1f dB\n", db (off, 750.0), db (on, 750.0),
                 db (off, 1250.0), db (on, 1250.0));
    std::printf ("    5.1 kHz: %.2f -> %.2f dB, 15.3 kHz (its 3rd): %.1f -> %.1f dB, 250 Hz: %.2f -> %.2f dB\n", db (off, 5100.0),
                 db (on, 5100.0), db (off, 15300.0), db (on, 15300.0), db (off, 250.0), db (on, 250.0));
    CHECK (db (on, 750.0) > db (off, 750.0) + 20.0 && db (on, 750.0) > -50.0, "harmonics of the band: %.1f vs %.1f dB", db (on, 750.0),
           db (off, 750.0));
    CHECK (std::fabs (db (on, 5100.0) - db (off, 5100.0)) < 0.5, "the tone outside the band: %.2f vs %.2f dB", db (on, 5100.0),
           db (off, 5100.0));
    CHECK (db (on, 15300.0) < db (on, 750.0) - 20.0, "harmonics mostly from the band: 15.3 kHz %.1f dB, 750 Hz %.1f dB", db (on, 15300.0),
           db (on, 750.0));
    // level matched: the band gets denser, not louder
    CHECK (db (on, 250.0) < db (off, 250.0) + 0.5, "the band no louder: %.2f vs %.2f dB", db (on, 250.0), db (off, 250.0));

    // quiet, the region passes the Drive untouched (the curve is linear there)
    const Sig quiet = tones ({{60.0, -40.0}, {250.0, -40.0}, {1000.0, -40.0}, {9000.0, -46.0}}, 0.5);
    auto renderQuiet = [&] (bool drive) {
        auto e = engine ([drive] (Engine& en) {
            en.setParam (kAdvanced, 1.0);
            en.setParam (bandParam (0, kThreshold), -60.0);
            en.setParam (kDrive, drive ? 1.0 : 0.0);
            en.setParam (kDriveAmount, 12.0);
        });
        return run (*e, quiet, 333);
    };
    const Sig qOff = renderQuiet (false), qOn = renderQuiet (true);
    double err = 0.0;
    for (size_t i = 4800; i < qOn.l.size (); ++i)
        err = std::max (err, (double)std::fabs (qOn.l[i] - qOff.l[i]));
    CHECK (err < 1e-5, "quiet through the region Drive: %g", err);
}

TEST (latency_is_constant)
{
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        const int base = engine ({}, sr)->latency ();
        int worst = base;
        for (int variant = 0; variant < 4; ++variant)
        {
            auto e = engine (
                [variant] (Engine& en) {
                    en.setParam (kAdvanced, variant & 1 ? 1.0 : 0.0);
                    en.setParam (kDrive, variant & 2 ? 1.0 : 0.0);
                    en.setParam (kStereo, variant);
                },
                sr);
            if (e->latency () != base)
                worst = e->latency ();
        }
        Engine withTail (true);
        withTail.prepare (sr, 512);
        smacheratr::Tail t;
        t.prepare (sr, 512);
        std::printf ("    %.0f Hz: %d samples (with the end Smacheratr %d)\n", sr, base, withTail.latency ());
        CHECK (worst == base && base > 0 && base < 100, "the same with every setting: %d / %d", base, worst);
        CHECK (withTail.latency () == base + t.latency (), "with the end Smacheratr: %d", withTail.latency ());
    }
    // an impulse comes out at the latency, the Drive on (and the region lined up with it: a loud click
    // in the band comes out as one click)
    auto e = engine ([] (Engine& en) {
        en.setParam (kAdvanced, 1.0);
        en.setParam (kDrive, 1.0);
        en.setParam (bandParam (0, kThreshold), -60.0);
    });
    Sig in;
    in.l.assign (9600, 0.0f);
    in.l[4800] = 0.3f;
    in.r = in.l;
    const Sig out = run (*e, in);
    size_t peak = 0;
    for (size_t i = 0; i < out.l.size (); ++i)
        if (std::fabs (out.l[i]) > std::fabs (out.l[peak]))
            peak = i;
    CHECK ((int)peak - 4800 == e->latency (), "the impulse at %d, the latency %d", (int)peak - 4800, e->latency ());
}

TEST (stereo_modes)
{
    // left loud in band 1, right quiet: linked, both are cut alike; Mid/Side, the side (L - R) is cut
    // as well as the mid; Mid, only the mid
    Sig in = tones ({{250.0, -2.0}}, 1.0);
    for (size_t i = 0; i < in.r.size (); ++i)
        in.r[i] *= 0.1f;
    auto render = [&] (int mode, Meters* m = nullptr) {
        auto e = engine (
            [mode] (Engine& en) {
                en.setParam (kStereo, mode);
                en.setParam (bandParam (1, kOn), 0.0);
            },
            kSr, m);
        return run (*e, in);
    };
    auto drop = [&] (const Sig& o, bool right) {
        return toneDb (right ? o.r : o.l, 250.0, 24000) - toneDb (right ? in.r : in.l, 250.0, 24000);
    };
    const Sig linked = render (kStereoLinked);
    std::printf ("    linked: left %.2f dB, right %.2f dB\n", drop (linked, false), drop (linked, true));
    CHECK (drop (linked, false) < -4.0 && std::fabs (drop (linked, false) - drop (linked, true)) < 0.05, "linked: the same cut");

    // the same tone on both sides (all mid, no side): Side leaves it alone, Mid cuts it
    const Sig mono = tones ({{250.0, -2.0}}, 1.0);
    auto renderMono = [&] (int mode) {
        auto e = engine ([mode] (Engine& en) {
            en.setParam (kStereo, mode);
            en.setParam (bandParam (1, kOn), 0.0);
        });
        return run (*e, mono);
    };
    const Sig side = renderMono (kSideOnly), mid = renderMono (kMidOnly), both = renderMono (kMidSide);
    const double dSide = toneDb (side.l, 250.0, 24000) - toneDb (mono.l, 250.0, 24000);
    const double dMid = toneDb (mid.l, 250.0, 24000) - toneDb (mono.l, 250.0, 24000);
    const double dBoth = toneDb (both.l, 250.0, 24000) - toneDb (mono.l, 250.0, 24000);
    std::printf ("    a mono tone: Side %.2f dB, Mid %.2f dB, Mid/Side %.2f dB\n", dSide, dMid, dBoth);
    CHECK (std::fabs (dSide) < 0.01 && dMid < -4.0 && std::fabs (dMid - dBoth) < 0.05, "Side / Mid / Mid/Side");
    // a pure side signal (L = -R): Mid leaves it, Side cuts it, and it stays a side signal
    Sig anti = mono;
    for (auto& v : anti.r)
        v = -v;
    auto renderAnti = [&] (int mode) {
        auto e = engine ([mode] (Engine& en) {
            en.setParam (kStereo, mode);
            en.setParam (bandParam (1, kOn), 0.0);
        });
        return run (*e, anti);
    };
    const Sig aMid = renderAnti (kMidOnly), aSide = renderAnti (kSideOnly);
    const double adMid = toneDb (aMid.l, 250.0, 24000) - toneDb (anti.l, 250.0, 24000);
    const double adSide = toneDb (aSide.l, 250.0, 24000) - toneDb (anti.l, 250.0, 24000);
    double sum = 0.0;
    for (size_t i = 24000; i < aSide.l.size (); ++i)
        sum = std::max (sum, (double)std::fabs (aSide.l[i] + aSide.r[i]));
    CHECK (std::fabs (adMid) < 0.01 && adSide < -4.0 && sum < 1e-6, "a side tone: Mid %.2f dB, Side %.2f dB (mid left %g)", adMid, adSide,
           sum);
}

TEST (changes_are_click_free)
{
    // a loud tone in band 1: switching the stereo mode, turning the band off and back on, Advanced and
    // the Drive on and off: no step in the output much above the tone's own
    const Sig in = tones ({{250.0, -3.0}}, 4.0);
    auto e = engine ([] (Engine& en) {
        en.setParam (bandParam (1, kOn), 0.0);
        en.setParam (bandParam (0, kThreshold), -40.0);
        en.setParam (kSubThreshold, -60.0);
        en.setParam (kDriveAmount, 36.0);
    });
    Sig out = in;
    const int block = 256;
    double worst = 0.0;
    const double normal = maxStep (in.l, 1, in.l.size ());
    int blockNo = 0;
    for (size_t pos = 0; pos < in.l.size (); pos += block, ++blockNo)
    {
        switch (blockNo)
        {
            case 60: e->setParam (kStereo, kMidSide); break;
            case 110: e->setParam (kStereo, kMidOnly); break;
            case 160: e->setParam (bandParam (0, kOn), 0.0); break;
            case 210: e->setParam (bandParam (0, kOn), 1.0); break;
            case 260: e->setParam (kAdvanced, 1.0); break;
            case 330: e->setParam (kDrive, 1.0); break;
            case 400: e->setParam (kDrive, 0.0); break;
            case 470: e->setParam (kStereo, kStereoLinked); break;
            case 520: e->setParam (kSubRange, 8.0); break; // (the Sub band starts: it has a Range)
            case 560: e->setParam (kSubFreq, 100.0); break;
            case 600: e->setParam (kSubRange, 0.0); break;
            default: break;
        }
        const int m = (int)std::min ((size_t)block, in.l.size () - pos);
        e->process (out.l.data () + pos, out.r.data () + pos, out.l.data () + pos, out.r.data () + pos, m);
    }
    worst = maxStep (out.l, 4800, out.l.size ());
    std::printf ("    largest step %.4f; the tone's own %.4f\n", worst, normal);
    CHECK (worst < 1.3 * normal, "no clicks: %.4f vs %.4f", worst, normal);
}

TEST (silence_after_a_burst)
{
    // a loud burst, then silence: the output dies away to nothing (no denormal crawl), and fast
    auto e = engine ([] (Engine& en) {
        en.setParam (kAdvanced, 1.0);
        en.setParam (kDrive, 1.0);
        en.setParam (bandParam (0, kThreshold), -40.0);
    });
    std::vector<float> l (512), r (512);
    uint32_t seed = 3;
    for (int b = 0; b < 20; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            l[(size_t)i] = r[(size_t)i] = (float)((double)(seed >> 8) / 8388608.0 - 1.0);
        }
        e->process (l.data (), r.data (), l.data (), r.data (), 512);
    }
    float last = 0.0f;
    const auto t0 = std::chrono::steady_clock::now ();
    const int blocks = (int)(5.0 * kSr / 512);
    for (int b = 0; b < blocks; ++b)
    {
        std::fill (l.begin (), l.end (), 0.0f);
        std::fill (r.begin (), r.end (), 0.0f);
        e->process (l.data (), r.data (), l.data (), r.data (), 512);
        if (b == blocks - 1)
            for (float v : l)
                last = std::max (last, std::fabs (v));
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
    std::printf ("    after 5 s of silence: %g at most; %.2f%% of real time\n", last, 100.0 * secs / 5.0);
    CHECK (last < 1e-20f, "silent: %g", last);
}

TEST (fuzz_and_cpu)
{
    uint32_t seed = 11;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (double)((seed >> 8) & 0xFFFFFF) / 16777216.0;
    };
    Engine e;
    e.prepare (kSr, 1024);
    std::vector<float> l (1024), r (1024);
    bool finite = true;
    for (int blk = 0; blk < 3000; ++blk)
    {
        if (blk % 15 == 0)
            for (int k = 0; k < 5; ++k)
            {
                const uint32_t id = (uint32_t)(rnd () * (double)kNumParams) % kNumParams;
                const auto& info = paramTable ().info (id);
                e.setParam (id, info.min + rnd () * (info.max - info.min));
            }
        const int n = 1 + (int)(rnd () * 1023);
        for (int i = 0; i < n; ++i)
        {
            l[(size_t)i] = (float)(rnd () * 2.0 - 1.0) * 0.9f;
            r[(size_t)i] = (float)(rnd () * 2.0 - 1.0) * 0.9f;
        }
        e.process (l.data (), r.data (), l.data (), r.data (), n);
        for (int i = 0; i < n; ++i)
            finite &= std::isfinite (l[(size_t)i]) && std::isfinite (r[(size_t)i]) && std::fabs (l[(size_t)i]) < 100.0f;
    }
    CHECK (finite, "finite and bounded");

    // CPU: both bands cutting (and the Sub band with the region Drive); plain, with the region Drive, Mid/Side, and with the end Smacheratr on
    struct Case
    {
        const char* name;
        bool drive, ms, tail;
    };
    for (const Case& cs : {Case {"both bands", false, false, false}, Case {"both bands and Sub, region Drive", true, false, false},
                           Case {"both bands, Mid/Side, region Drive", true, true, false},
                           Case {"both bands, region Drive, end saturator on", true, false, true}})
    {
        Engine c (cs.tail);
        c.setParam (kAdvanced, 1.0);
        c.setParam (bandParam (0, kThreshold), -40.0);
        c.setParam (bandParam (1, kThreshold), -40.0);
        c.setParam (kSubRange, cs.drive ? 8.0 : 0.0);
        c.setParam (kSubThreshold, -40.0);
        c.setParam (kDrive, cs.drive ? 1.0 : 0.0);
        c.setParam (kStereo, cs.ms ? kMidSide : kStereoLinked);
        c.setParam (kTailBase + pk::kTailOn, cs.tail ? 1.0 : 0.0);
        c.prepare (kSr, 512);
        const int blocks = (int)(10.0 * kSr / 512);
        const auto t0 = std::chrono::steady_clock::now ();
        for (int b = 0; b < blocks; ++b)
        {
            if (b % 8 == 0)
                c.setParam (bandParam (1, kFreq), 2000.0 + 2000.0 * rnd ());
            for (int i = 0; i < 512; ++i)
            {
                l[(size_t)i] = (float)(rnd () * 2.0 - 1.0) * 0.5f;
                r[(size_t)i] = (float)(rnd () * 2.0 - 1.0) * 0.5f;
            }
            c.process (l.data (), r.data (), l.data (), r.data (), 512);
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
        std::printf ("    %s: %.2f%% of real time (stereo, 48 kHz)\n", cs.name, 100.0 * secs / 10.0);
    }
}

// ---------------------------------------------------------------------------
// the band Slope (smacheratr::ClaritySlope): bands 1 and 2 only

// Gentlr's output with every band at work (Advanced, Thresholds, the region Drive), hashed (FNV-1a
// over the output's bits)
static uint64_t slopeRenderHash (int slope)
{
    const size_t n = 48000;
    std::vector<float> l (n), r (n), ol (n), orr (n);
    uint32_t seed = 12345;
    for (size_t i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const double noise = ((seed >> 8) / 16777216.0 - 0.5) * 0.2;
        const double t = (double)i / 48000.0;
        l[i] = (float)(0.4 * std::sin (2.0 * M_PI * 80.0 * t) + 0.3 * std::sin (2.0 * M_PI * 320.0 * t) + 0.2 * std::sin (2.0 * M_PI * 3100.0 * t) + noise);
        r[i] = (float)(0.35 * std::sin (2.0 * M_PI * 110.0 * t) + 0.25 * std::sin (2.0 * M_PI * 9000.0 * t) + noise);
    }
    Engine e (false);
    e.setParam (bandParam (0, kRange), 12.0);
    e.setParam (bandParam (0, kFreq), 300.0);
    e.setParam (bandParam (1, kRange), 9.0);
    e.setParam (bandParam (1, kWidth), 3.0);
    e.setParam (kSubRange, 6.0);
    e.setParam (kHighRange, 6.0);
    e.setParam (bandParam (0, kThreshold), -30.0);
    e.setParam (bandParam (1, kThreshold), -30.0);
    e.setParam (kDrive, 1.0);
    e.setParam (kSlope, slope);
    e.prepare (48000.0, 512);
    for (size_t p = 0; p < n; p += 512)
    {
        const int m = (int)std::min<size_t> (512, n - p);
        e.process (l.data () + p, r.data () + p, ol.data () + p, orr.data () + p, m);
    }
    uint64_t h = 1469598103934665603ull;
    for (const auto* v : {&ol, &orr})
        for (float f : *v)
        {
            uint32_t b;
            std::memcpy (&b, &f, 4);
            for (int i = 0; i < 4; ++i)
            {
                h ^= (b >> (8 * i)) & 0xff;
                h *= 1099511628211ull;
            }
        }
    return h;
}

TEST (slope)
{
    const auto& t = paramTable ();
    CHECK (std::lround (t.info (kSlope).def) == smacheratr::kSlopeSignature && std::string (t.info (kSlope).name) == "Slope" &&
               t.toText (kSlope, smacheratr::kSlopeSignature) == "Signature",
           "Slope: Signature for a new instance");
    CHECK (fromSmacheratr (smacheratr::kClaritySlope) == (int64_t)kSlope, "Smacheratr's Slope is Gentlr's (the shared displays)");
    CHECK (!isTailParam (kSlope) && isTailParam (kTailExt3Base + pk::kTailExt3Slope), "Gentlr's own, and the end saturator's");

    // a tone two octaves under band 1's low edge, the band cutting all it can: 12 / 12 (12 dB/oct there)
    // moves it a little (the band's phase lifts it), Signature (24 dB/oct) leaves it alone; an octave
    // over its high edge, Classic's 6 dB/oct reaches it, the others' 12 much less
    auto drop = [] (int slope, double hz) {
        auto e = engine ([slope] (Engine& en) {
            en.setParam (kSlope, slope);
            en.setParam (bandParam (0, kFreq), 400.0);
            en.setParam (bandParam (0, kRange), 24.0);
            en.setParam (bandParam (0, kThreshold), -60.0);
            en.setParam (bandParam (1, kOn), 0.0);
            en.setParam (kAdvanced, 1.0);
        });
        // the band itself loud (so it is cut to its Range), the probe quieter beside it
        const Sig in = tones ({{400.0, -6.0}, {hz, -30.0}}, 1.0);
        const Sig out = run (*e, in);
        return toneDb (out.l, hz, 24000) - toneDb (in.l, hz, 24000);
    };
    const double above12 = drop (smacheratr::kSlope12, 1600.0), aboveSig = drop (smacheratr::kSlopeSignature, 1600.0),
                 aboveClassic = drop (smacheratr::kSlopeClassic, 1600.0);
    const double below12 = drop (smacheratr::kSlope12, 50.0), belowSig = drop (smacheratr::kSlopeSignature, 50.0);
    std::printf ("    an octave over the band: 12 / 12 %.2f, Signature %.2f, Classic %.2f dB; two octaves under: 12 / 12 %.2f, Signature %.2f dB\n",
                 above12, aboveSig, aboveClassic, below12, belowSig);
    CHECK (aboveClassic < above12 - 1.0 && std::fabs (above12 - aboveSig) < 1.0, "above the band: Classic cuts more there, the other two alike");
    CHECK (std::fabs (belowSig) < std::fabs (below12) && std::fabs (belowSig) < 0.3, "below the band: Signature leaves it more alone (%.2f / %.2f)",
           belowSig, below12);

    // Classic renders bit for bit what Gentlr rendered before the Slope (the hash taken from the engine
    // before it, 0.11; pinned for the Linux x86-64 GCC build, see smacheratr's test of the same)
    const uint64_t classic = slopeRenderHash (smacheratr::kSlopeClassic), twelve = slopeRenderHash (smacheratr::kSlope12);
    std::printf ("    Classic %016llx, 12 / 12 %016llx\n", (unsigned long long)classic, (unsigned long long)twelve);
    CHECK (classic != twelve, "each slope sounds its own");
#if defined(__linux__) && defined(__x86_64__) && defined(__GNUC__) && !defined(__clang__)
    CHECK (classic == 0x9737d2db291ffcddull, "Classic: Gentlr before the Slope, bit for bit (%016llx)", (unsigned long long)classic);
#endif
}

int main (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& t : tests ())
    {
        if (filter && std::string (t.name).find (filter) == std::string::npos)
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
