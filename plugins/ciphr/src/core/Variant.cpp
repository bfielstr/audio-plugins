#include "Variant.h"

#include "Wavetable.h"

#include <algorithm>

namespace ciphr {

namespace {

// Each oscillator's place in the cluster: the intervals its list draws its base pitch from (oscillator 0
// is the root, the others add octaves and fifths), so every variant stays in tune with the note.
const double kBaseIntervals[kOscs][4] = {
    {0, 0, 0, 0}, {0, 12, -12, 0}, {7, 12, 0, 19}, {0, 12, 7, 24}, {-12, 0, 5, 12}, {0, 19, 12, 7},
};
// What an entry may add to its oscillator's base (most add nothing).
const double kEntryIntervals[8] = {0, 0, 0, 0, 12, -12, 7, 0};

// The tiers of the sets after Classic: an entry's draw (0 .. kNumClassicWaves - 1) picks draw % size of
// the tier of its place in the list. Each set's root (oscillator 0's first entry, at the note).
struct Tier
{
    int size;
    int waves[6];
};
struct SetDef
{
    int root;
    Tier tiers[kEntries];
};
const SetDef kSets[kNumWaveSets] = {
    {kWaveSaw, {}}, // Classic (not read: its draws are the waves themselves)
    // Alien: calm, then hollow and glassy, then metal and buzz, then screech and scrape
    {kWaveSoft,
     {{5, {kWaveSine, kWaveSoft, kWaveTriangle, kWaveVowelO, kWaveDeepOO}},
      {4, {kWaveHollow, kWaveGlass, kWavePlate, kWaveVowelA}},
      {4, {kWaveMetal, kWaveBuzz, kWaveThroat, kWavePlate}},
      {4, {kWaveScreech, kWaveScrape, kWaveScreech, kWaveMetal}}}},
    // Metal: glassy and struck, then clanging, then scraping
    {kWaveGlass,
     {{4, {kWaveGlass, kWavePlate, kWaveTriangle, kWaveOrgan}},
      {3, {kWavePlate, kWaveMetal, kWaveGlass}},
      {3, {kWaveMetal, kWaveScrape, kWavePlate}},
      {3, {kWaveScrape, kWaveMetal, kWaveScreech}}}},
    // Voice: closed, deep vowels opening up to "aa", the choir and the throat whistle
    {kWaveDeepOO,
     {{2, {kWaveDeepOO, kWaveDeepOH}},
      {4, {kWaveDeepOH, kWaveVowelO, kWaveDeepOO, kWaveThroat}},
      {3, {kWaveDeepAA, kWaveVowelA, kWaveChoir}},
      {3, {kWaveChoir, kWaveThroat, kWaveDeepAA}}}},
};

} // namespace

Patch makePatch (int variant, int waveSet)
{
    Patch p;
    p.variant = variant;
    p.waveSet = waveSet >= 0 && waveSet < kNumWaveSets ? waveSet : kSetClassic;
    const SetDef& set = kSets[p.waveSet];
    Rng rng (0xC1F4E5ull * (uint64_t)(uint32_t)variant + 0x51ull);
    for (int k = 0; k < kOscs; ++k)
    {
        const double base = kBaseIntervals[k][rng.below (4)];
        for (int e = 0; e < kEntries; ++e)
        {
            Entry& en = p.osc[k][e];
            const int draw = rng.below (kNumClassicWaves); // (the draw is the same in every set)
            en.wave = p.waveSet == kSetClassic ? draw : set.tiers[e].waves[draw % set.tiers[e].size];
            const double cents = rng.range (-7.0, 7.0);
            en.semis = base + kEntryIntervals[rng.below (8)] + cents / 100.0;
        }
        p.phase0[k] = rng.uniform ();
    }
    // the root's first entry is a plain wave at the note (Classic: a saw; the other sets their own root): every
    // variant starts from a solid fundamental
    p.osc[0][0] = {set.root, 0.0};

    // the taps: tap 0 the longest (Length itself), the others spread below it
    double times[kTaps];
    times[0] = 1.0;
    for (int i = 1; i < kTaps; ++i)
        times[i] = rng.range (0.08, 0.97);
    std::sort (times + 1, times + kTaps, [] (double a, double b) { return a > b; });
    for (int i = 0; i < kTaps; ++i)
    {
        Tap& t = p.taps[i];
        t.time = times[i];
        t.gain = i == 0 ? 0.9 : rng.range (0.35, 0.9);
        const double side = (i & 1) ? -1.0 : 1.0;
        t.pan = i == 0 ? 0.0 : side * rng.range (0.3, 1.0);
        t.lfoRate = rng.range (0.07, 0.4);
        t.lfoPhase = rng.uniform ();
    }
    return p;
}

} // namespace ciphr
