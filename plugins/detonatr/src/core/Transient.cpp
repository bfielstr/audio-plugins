#include "Transient.h"

#include <algorithm>
#include <cmath>

namespace detonatr {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kLookMs = 5.0;     // look-ahead: room to find the onset and ramp the gain up before it
constexpr double kRampUpMs = 0.5;   // the gain comes back up to 0 dB over this, ending where the spike starts
constexpr double kHoldOffMs = 40.0; // one hit triggers once: no new onset this soon after the last
// The spike starts this much before the onset found. A hit landing on a tail only shows in the level once
// it rises over the tail's peaks, a few tenths of a ms after it really starts; better the spike begins a
// hair early (on a little of the tail, or on silence) than shaves the hit's first samples.
constexpr double kPreRollMs = 0.2;
constexpr double kFloor = 3.16e-4;  // -70 dBFS: the level jump is measured from at least this, so hiss can't trigger
constexpr double kDbToLn = 0.11512925464970228; // ln (10) / 20

inline double smoothCoef (double ms, double sr) { return 1.0 - std::exp (-1000.0 / (ms * sr)); }
inline double dbToGain (double db) { return std::exp (db * kDbToLn); }
inline double halfCos (double p) { return 0.5 - 0.5 * std::cos (kPi * p); } // 0 -> 1 with flat ends
} // namespace

void Transient::prepare (double sampleRate, int)
{
    sr = sampleRate;
    look = std::max (8, (int)std::lround (kLookMs * 0.001 * sr));
    rampUp = std::max (2, (int)std::lround (kRampUpMs * 0.001 * sr));
    holdOff = (int)std::lround (kHoldOffMs * 0.001 * sr);
    preRoll = (int)std::lround (kPreRollMs * 0.001 * sr);
    size = 1;
    while (size <= look + 1)
        size <<= 1;
    bufL.assign ((size_t)size, 0.0f);
    bufR.assign ((size_t)size, 0.0f);
    hist.assign ((size_t)size, 0.0f);
    slowHist.assign ((size_t)size, 0.0f);

    // Level detection. Fast: the peak level, held 15 ms and then falling in 15 ms, so it sits steady on the
    // waveform's peaks down to 40 Hz (no ripple for a held tone to retrigger on). Slow: the fast level
    // smoothed (up in 8 ms, down in 20 ms): where the level was just before. An onset is the fast level
    // jumping Sensitivity dB over the slow one. The slow release is short enough that on a decaying tail
    // it isn't left far above the tail (which would hide a new hit landing on it).
    fastRel = std::exp (-1000.0 / (15.0 * sr));
    holdN = (int)std::lround (0.015 * sr);
    slowAtk = smoothCoef (8.0, sr);
    slowRel = smoothCoef (20.0, sr);
    dropSmooth = smoothCoef (10.0, sr);
    reset ();
}

void Transient::reset ()
{
    std::fill (bufL.begin (), bufL.end (), 0.0f);
    std::fill (bufR.begin (), bufR.end (), 0.0f);
    std::fill (hist.begin (), hist.end (), 0.0f);
    std::fill (slowHist.begin (), slowHist.end (), 0.0f);
    now = 0;
    fresh = true;
    fast = slow = 0.0;
    holdLeft = 0;
    armed = true;
    lastTrigger = -1000000000;
    pendingAt = -1;
    spikeAt = -1000000000; // no hit yet: the gain sits at the Drop, as it does for the rest of a hit
    dropNow = dropDb;
    dropGainDb = dropNow;
    dropGain = (float)dbToGain (-dropNow);
    lastGain = rampFrom = dropGain;
}

void Transient::setSpike (double ms) { spikeMs = std::clamp (ms, 0.1, 20.0); }
void Transient::setDrop (double db) { dropDb = std::clamp (db, 0.0, 48.0); }
void Transient::setFall (double ms) { fallMs = std::clamp (ms, 0.1, 50.0); }
void Transient::setSensitivity (double db) { sensDb = std::clamp (db, 3.0, 24.0); }
int Transient::latency () const { return look; }

int64_t Transient::findOnset (int64_t t) const
{
    // The detector fires a little after the hit really starts (the level has to climb Sensitivity dB
    // first, and on a tail the hit's first peaks take a few ms to rise that far). The level from before the
    // hit is the one at the far end of the look-ahead (the slow level, or the fast one if higher, i.e. the
    // tail's held peak). Walk back through the fast level's history to where it last sat within 0.5 dB of
    // that: the hit starts just after.
    const int64_t earliest = t - (look - rampUp);
    const int mask = size - 1;
    const int64_t from = std::max (earliest, (int64_t)0); // (before the first sample it was silent)
    const size_t e = (size_t)(from & mask);
    const double before = earliest < 0 ? 0.0 : std::max ((double)hist[e], (double)slowHist[e]);
    const double th = std::max (before, kFloor) * dbToGain (0.5);
    for (int64_t k = t; k >= from; --k)
        if (hist[(size_t)(k & mask)] <= th)
            return k + 1;
    return from;
}

float Transient::gainAt (int64_t t)
{
    if (pendingAt >= 0)
    {
        if (t >= pendingAt)
        {
            // each hit keeps the Spike and Fall it started with, so moving them mid-fall can't step the gain
            spikeAt = pendingAt;
            pendingAt = -1;
            hitSpikeN = spikeN;
            hitFallN = fallN;
        }
        else if (t >= pendingAt - rampUp)
        {
            // back up to 0 dB just before the onset, from wherever the gain was (a raised cosine, so
            // neither end has a corner); the ramp's last step lands on 0 dB at the onset itself
            if (t == pendingAt - rampUp)
                rampFrom = lastGain;
            const double p = (double)(t - (pendingAt - rampUp) + 1) / (double)(rampUp + 1);
            return rampFrom + (float)((1.0 - rampFrom) * halfCos (p));
        }
    }
    const int64_t pos = t - spikeAt;
    if (pos < hitSpikeN)
        return 1.0f;
    if (pos < (int64_t)hitSpikeN + hitFallN)
    {
        // down to the Drop over Fall, a raised cosine in dB: it leaves the spike and meets the Drop
        // without a corner (a corner in the gain is a click)
        const double p = (double)(pos - hitSpikeN + 1) / (double)hitFallN;
        return (float)dbToGain (-dropNow * halfCos (p));
    }
    if (dropGainDb != dropNow)
    {
        dropGainDb = dropNow;
        dropGain = (float)dbToGain (-dropNow);
    }
    return dropGain;
}

void Transient::process (float* L, float* R, int n)
{
    if (size == 0)
        return; // not prepared
    spikeN = std::max (1, (int)std::lround (spikeMs * 0.001 * sr)) + preRoll; // Spike counts from the onset
    fallN = std::max (1, (int)std::lround (fallMs * 0.001 * sr));
    const double sens = dbToGain (sensDb), rearm = dbToGain (0.5 * sensDb);
    const int mask = size - 1;
    if (fresh)
    {
        // the first block after prepare / reset starts at the settings rather than gliding to them
        fresh = false;
        dropNow = dropDb;
        lastGain = rampFrom = (float)dbToGain (-dropNow);
    }

    for (int i = 0; i < n; ++i, ++now)
    {
        const int w = (int)(now & mask), r = (int)((now - look) & mask);
        const float dl = bufL[(size_t)r], dr = bufR[(size_t)r];
        bufL[(size_t)w] = L[i];
        bufR[(size_t)w] = R[i];

        // detection runs on the undelayed input, both channels together (one gain for both)
        double x = std::max (std::fabs ((double)L[i]), std::fabs ((double)R[i]));
        if (!(x < 1e9))
            x = 0.0; // inf / NaN in: don't let it stick in the level followers
        if (x >= fast)
        {
            fast = x;
            holdLeft = holdN;
        }
        else if (holdLeft > 0)
            --holdLeft;
        else
        {
            fast *= fastRel;
            if (fast < 1e-20)
                fast = 0.0;
        }
        hist[(size_t)w] = (float)fast;
        slowHist[(size_t)w] = (float)slow;
        const double ref = std::max (slow, kFloor);
        if (armed && now - lastTrigger >= holdOff && fast > ref * sens)
        {
            // an onset: schedule its spike to start where the hit starts in the delayed output, a hair
            // early (see kPreRollMs), and no later than leaves room for the ramp up before it
            pendingAt = std::max (findOnset (now) - preRoll, now - (look - rampUp)) + look;
            lastTrigger = now;
            armed = false;
        }
        else if (!armed && fast < ref * rearm)
            armed = true; // the jump is over (the slow level caught up, or the sound died away)
        slow += (fast - slow) * (fast > slow ? slowAtk : slowRel);
        if (slow < 1e-20)
            slow = 0.0;

        // the gain for the sample leaving the delay now
        if (dropNow != dropDb)
        {
            dropNow += (dropDb - dropNow) * dropSmooth;
            if (std::fabs (dropNow - dropDb) < 1e-4)
                dropNow = dropDb;
        }
        const float g = gainAt (now);
        lastGain = g;
        if (g == 1.0f)
        {
            L[i] = dl;
            R[i] = dr;
        }
        else
        {
            L[i] = dl * g;
            R[i] = dr * g;
        }
    }
}

} // namespace detonatr
