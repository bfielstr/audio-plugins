// Each band's drive: a gain into a curve, oversampled with Smacheratr's oversampler (4x, 2x or not at
// all: the drives' Oversampling, setFactor), left and right apart. The curves (small-signal gain 1 each, so a quiet signal goes through level):
//   Analog     Smacheratr's curve (smacheratr::analogClip): clean up to 0.5, a soft knee to 1 at 1.5
//   Tape       tanh: soft all the way, odd harmonics
//   Tube       tanh leaning to one side (tanh (x + 0.35), re-centred): the top squashes before the
//              bottom, so it adds even harmonics (the second above all); the DC that leaves is
//              filtered out (5 Hz)
//   Hard Clip  +-1, a flat top: the most edge
//   Fold       sin: folds back over past +-1 (pi/2): hollow, metallic as it's pushed
// Auto gain: after the curve each type is brought back so a sine at -12 dBFS (peak) comes out at the
// level it went in at, at every Drive (a table made once, DriveMakeup). So the types sound about
// equally loud at the same Drive, turning Drive up adds colour rather than level, and a hotter band
// (its Gain up) pushes into the curve: louder parts squash, quieter ones come up a little.
//   The stage's latency is the oversampler's, always (none with Oversampling off): Levlr delays every band
// by it whether its drive is on or not (Engine). Drive at 0 dB is off: the band skips the curve (bit-exact, only delayed). Turning
// it on or off crossfades (15 ms) between the clean band and the curve's output, a new type
// crossfades between the two curves (inside the oversampled signal, so both line up), and Drive
// glides (10 ms).
#pragma once

#include "Params.h"

#include "smacheratr/src/core/Oversampler.h"
#include "smacheratr/src/core/Shaper.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace levlr {

constexpr double kDriveRefPeak = 0.25;   // the auto gain's sine (-12 dBFS peak)
constexpr double kTubeBias = 0.35;        // how far Tube leans
constexpr double kTubeDcHz = 5.0;         // the DC filter after it

// tanh (kTubeBias), in float as the curve works it out, so the curve is exactly 0 at 0 (silence stays silence)
inline const float kTubeCentre = std::tanh ((float)kTubeBias);
inline const float kTubeNorm = 1.0f / (1.0f - kTubeCentre * kTubeCentre); // (its slope at 0: 1)

// The curves, without their auto gain.
inline float driveCurve (int type, float x)
{
    switch (type)
    {
        case kDriveTape: return std::tanh (x);
        case kDriveTube:
            return (std::tanh (x + (float)kTubeBias) - kTubeCentre) * kTubeNorm;
        case kDriveHard: return std::clamp (x, -1.0f, 1.0f);
        case kDriveFold: return std::sin (x);
        default: return (float)smacheratr::analogClip (x);
    }
}

// Each type's auto gain against Drive: the gain that brings the reference sine back to its level.
class DriveMakeup
{
public:
    static constexpr int kPerDb = 4;
    static constexpr int kSteps = (int)kMaxDriveDb * kPerDb + 1;

    static const DriveMakeup& get ()
    {
        static const DriveMakeup m;
        return m;
    }
    float at (int type, double db) const
    {
        const double p = std::clamp (db, 0.0, kMaxDriveDb) * kPerDb;
        const int i = std::min ((int)p, kSteps - 2);
        const float t = (float)(p - i);
        const float* row = table[std::clamp (type, 0, kNumDriveTypes - 1)];
        return row[i] + (row[i + 1] - row[i]) * t;
    }

private:
    DriveMakeup ()
    {
        constexpr int kPoints = 1024;
        const double rmsIn = kDriveRefPeak / std::sqrt (2.0);
        for (int type = 0; type < kNumDriveTypes; ++type)
            for (int s = 0; s < kSteps; ++s)
            {
                const double g = std::pow (10.0, ((double)s / kPerDb) / 20.0);
                double sum = 0.0, sq = 0.0;
                for (int i = 0; i < kPoints; ++i)
                {
                    const double y = driveCurve (type, (float)(g * kDriveRefPeak * std::sin (2.0 * M_PI * (i + 0.5) / kPoints)));
                    sum += y;
                    sq += y * y;
                }
                const double mean = sum / kPoints; // (Tube's DC, which its filter takes out)
                const double rms = std::sqrt (std::max (1e-12, sq / kPoints - mean * mean));
                table[type][s] = (float)(rmsIn / rms);
            }
    }
    float table[kNumDriveTypes][kSteps] {};
};

// One band's drive, stereo. The engine feeds it the band (its level applied) and mixes its output
// with the band delayed by latency (): out = dry + (wet - dry) * mix.
class BandDrive
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        maxBlock = std::max (1, maxBlockSize);
        DriveMakeup::get (); // (the table is made here, not on the audio thread)
        for (auto& o : os)
            o.prepare (sampleRate, maxBlock);
        osBuf.assign ((size_t)maxBlock * 4, 0.0f);
        preGain.assign ((size_t)maxBlock, 0.0f);
        mNow.assign ((size_t)maxBlock, 0.0f);
        mFrom.assign ((size_t)maxBlock, 0.0f);
        fadeStep = (float)(1.0 / (0.015 * sampleRate));
        dbSmooth = 1.0 - std::exp (-1.0 / (0.010 * sampleRate));
        sr = sampleRate;
        dcR = (float)std::exp (-2.0 * M_PI * kTubeDcHz / (factor * sr));
        reset ();
    }
    void reset ()
    {
        for (auto& o : os)
            o.reset ();
        resetDc ();
        amt = amtT;
        idleNow = amt <= 0.0f;
        warm = 0;
        db = dbT;
        gainNow = (float)std::pow (10.0, db / 20.0);
        typeNow = typeFrom = typeT;
        typeFade = 1.0f;
    }
    // Input samples; the same whether the drive is on or off.
    int latency () const { return os[0].latency (factor); }
    int latencyAt (int f) const { return os[0].latency (f); }
    int oversampling () const { return factor; }
    // The oversampling factor (1, 2 or 4). A new one starts the stage from silence (reset): the engine
    // changes every band's delay with it.
    void setFactor (int f)
    {
        f = f >= 4 ? 4 : f == 2 ? 2 : 1;
        if (f == factor)
            return;
        factor = f;
        dcR = (float)std::exp (-2.0 * M_PI * kTubeDcHz / (factor * sr));
        reset ();
    }

    // This block's settings. on: Drive above 0 and the band in use.
    void set (bool on, int type, double driveDb)
    {
        amtT = on ? 1.0f : 0.0f;
        dbT = std::clamp (driveDb, 0.0, kMaxDriveDb);
        typeT = std::clamp (type, 0, kNumDriveTypes - 1);
        if (idleNow)
        {
            if (!on)
                return;
            // waking: the oversampler starts from silence, so the curve's output is only mixed in
            // once its filters are full of the band (warm), from the settings as they are now
            idleNow = false;
            for (auto& o : os)
                o.reset ();
            resetDc ();
            warm = 2 * latency () + 8;
            db = dbT;
            gainNow = (float)std::pow (10.0, db / 20.0);
            typeNow = typeFrom = typeT;
            typeFade = 1.0f;
            primeDc = typeNow == kDriveTube;
        }
    }
    // Off and faded out: the band is the dry delay alone (process isn't needed).
    bool idle () const { return idleNow; }

    // in: the band into the drive; wet: the curve's output, latency () samples later; mix: how much of
    // it to use, per sample. n <= the prepared block size.
    void process (const float* const in[2], float* const wet[2], float* mix, int n)
    {
        const auto& makeup = DriveMakeup::get ();
        // a new type: crossfade from the old curve (one change at a time: the next waits for this one)
        if (typeFade >= 1.0f && typeT != typeNow)
        {
            typeFrom = typeNow;
            typeNow = typeT;
            typeFade = 0.0f;
            primeDc = primeDc || typeNow == kDriveTube;
        }
        const bool fading = typeFade < 1.0f;
        for (int i = 0; i < n; ++i)
        {
            if (warm > 0)
                --warm;
            else if (amt != amtT)
                amt = amtT > amt ? std::min (amtT, amt + fadeStep) : std::max (amtT, amt - fadeStep);
            mix[i] = amt;
            if (db != dbT)
            {
                db += (dbT - db) * dbSmooth;
                if (std::fabs (dbT - db) < 1e-4)
                    db = dbT;
                gainNow = (float)std::pow (10.0, db / 20.0);
            }
            preGain[(size_t)i] = gainNow;
            if (typeFade < 1.0f)
                typeFade = std::min (1.0f, typeFade + fadeStep);
            mNow[(size_t)i] = makeup.at (typeNow, db) * typeFade;
            mFrom[(size_t)i] = fading ? makeup.at (typeFrom, db) * (1.0f - typeFade) : 0.0f;
        }
        const int typeA = typeNow, typeB = typeFrom;
        const int m = factor * n, shift = factor == 4 ? 2 : factor == 2 ? 1 : 0;
        for (int c = 0; c < 2; ++c)
        {
            float* o = osBuf.data ();
            if (factor == 4)
                os[c].up (in[c], o, n);
            else if (factor == 2)
                os[c].up2x (in[c], o, n);
            else
                std::copy (in[c], in[c] + n, o);
            if (primeDc)
            {
                // the DC filter starts from Tube's DC in this block, so turning to Tube doesn't thump
                double sum = 0.0;
                for (int k = 0; k < m; ++k)
                    sum += driveCurve (kDriveTube, o[k] * preGain[(size_t)(k >> shift)]);
                dcX[c] = (float)(sum / m);
                dcY[c] = 0.0f;
            }
            for (int k = 0; k < m; ++k)
            {
                const int i = k >> shift;
                const float v = o[k] * preGain[(size_t)i];
                float y = mNow[(size_t)i] * curve (typeA, v, c);
                if (fading)
                    y += mFrom[(size_t)i] * curve (typeB, v, c);
                o[k] = y;
            }
            if (factor == 4)
                os[c].down (o, wet[c], n);
            else if (factor == 2)
                os[c].down2x (o, wet[c], n);
            else
                std::copy (o, o + n, wet[c]);
        }
        primeDc = false;
        if (amtT <= 0.0f && amt <= 0.0f && warm == 0)
            idleNow = true;
    }

private:
    // a curve, and Tube's DC filter (one Tube at a time: the old curve or the new one)
    inline float curve (int type, float v, int c)
    {
        const float y = driveCurve (type, v);
        if (type != kDriveTube)
            return y;
        const float out = y - dcX[c] + dcR * dcY[c];
        dcX[c] = y;
        dcY[c] = out;
        return out;
    }
    void resetDc ()
    {
        for (int c = 0; c < 2; ++c)
            dcX[c] = dcY[c] = 0.0f;
    }

    smacheratr::Oversampler os[2];
    std::vector<float> osBuf, preGain, mNow, mFrom;
    int maxBlock = 256, warm = 0, factor = 4;
    double sr = 48000.0;
    float amt = 0.0f, amtT = 0.0f, fadeStep = 0.001f, typeFade = 1.0f, gainNow = 1.0f;
    double db = 0.0, dbT = 0.0, dbSmooth = 0.01;
    int typeNow = kDriveAnalog, typeFrom = kDriveAnalog, typeT = kDriveAnalog;
    float dcX[2] {}, dcY[2] {}, dcR = 0.9999f;
    bool idleNow = true, primeDc = false;
};

} // namespace levlr
