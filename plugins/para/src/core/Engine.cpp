#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace para {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
constexpr int kCoeffInterval = 8; // samples between cutoff updates
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    tail.prepare (sr, maxBlock);
    drive.prepare (sr, maxBlock);
    for (int c = 0; c < 2; ++c)
    {
        src[c].assign ((size_t)maxBlock, 0.0f);
        mixed[c].assign ((size_t)maxBlock, 0.0f);
        bypassDelay[c].assign ((size_t)latency (), 0.0f);
    }
    gOut.assign ((size_t)maxBlock, 0.0f);
    scopeIn.assign ((size_t)maxBlock, 0.0f);
    duckStep = (float)(1.0 / (0.003 * sr)); // 3 ms fades around moving the drive
    for (uint32_t f = 0; f < pk::kTailFields; ++f)
        tail.setParam (f, p[kTailBase + f]);
    for (uint32_t f = 0; f < pk::kTailExtFields; ++f)
        tail.setParam (pk::kTailFields + f, p[kTailExtBase + f]);
    for (uint32_t f = 0; f < pk::kTailExt2Fields; ++f)
        tail.setParam (pk::kTailFields + pk::kTailExtFields + f, p[kTailExt2Base + f]);
    smooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));
    semiSmooth = (float)(1.0 - std::exp (-1.0 / (0.005 * sr))); // 5 ms glide of the cutoffs
    liquidA = 1.0 - std::exp (-1.0 / (0.15 * sr));
    if (meters)
        meters->sampleRate.store ((float)sr);
    reset ();
}

void Engine::reset ()
{
    for (auto& c : hp)
        for (auto& s : c)
            s.reset ();
    for (auto& c : lp)
        for (auto& s : c)
            s.reset ();
    env = 0.0;
    envRising = false;
    tail.reset ();
    drive.set (p[kDriveOn] >= 0.5, p[kDrive]);
    drive.reset ();
    drivePost = p[kDrivePos] >= 0.5;
    duck = 1.0f;
    duckHold = 0;
    for (auto& d : bypassDelay)
        std::fill (d.begin (), d.end (), 0.0f);
    bypassPos = 0;
    prevHpBase = p[kHpFreq]; // the leader of Vocal movement is kept
    prevLpBase = p[kLpFreq];
    hpMul = lpMul = hpMulT = lpMulT = 1.0f;
    offset = targetOffset ();
    split = p[kSplit];
    liquidLeaderLp = leaderLp;
    liquidSlow = 12.0 * std::log2 (std::max (1.0, leaderLp ? p[kLpFreq] : p[kHpFreq]));
    mix = (float)std::clamp (p[kDryWet], 0.0, 1.0);
    out = dbToGain (p[kOutput]);
    hpG = (float)filterGain (p[kHpGain]);
    lpG = (float)filterGain (p[kLpGain]);
    for (int c = 0; c < 2; ++c)
    {
        hp1[c].reset ();
        lp1[c].reset ();
    }
    const int slope = (int)std::lround (p[kSlope]);
    hpC.set (hpCutoff (p[kHpFreq], offset, split), resonanceToQ (p[kHpRes], slope), sr);
    lpC.set (lpCutoff (p[kLpFreq], offset, split), resonanceToQ (lpRes (), slope), sr);
    hpG1 = onePoleG (hpCutoff (p[kHpFreq], offset, split), sr);
    lpG1 = onePoleG (lpCutoff (p[kLpFreq], offset, split), sr);
}

// The cutoffs do not follow the notes any more (the notes only trigger the envelope).
double Engine::targetOffset () const { return 0.0; }

void Engine::noteOn (int note)
{
    lastNote = std::clamp (note, 0, 127);
    envRising = true; // the envelope restarts from where it is
}

void Engine::noteOff (int)
{
    // the last note keeps being tracked, so a released note leaves the filters where they are
}

void Engine::processBypassed (float* l, float* r, int n)
{
    const int len = (int)bypassDelay[0].size ();
    if (len <= 0)
        return;
    float* ch[2] = {l, r};
    for (int c = 0; c < 2; ++c)
    {
        int p = bypassPos;
        for (int i = 0; i < n; ++i)
        {
            const float d = bypassDelay[c][(size_t)p];
            bypassDelay[c][(size_t)p] = ch[c][i];
            ch[c][i] = d;
            if (++p >= len)
                p = 0;
        }
    }
    bypassPos = (bypassPos + n) % len;
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    if (src[0].empty ())
        prepare (sr, maxBlock); // never prepared (hosts always do): the drive's buffers are needed
    // the drive works on whole blocks of at most the prepared size
    for (int pos = 0; pos < n; pos += maxBlock)
    {
        const int m = std::min (maxBlock, n - pos);
        processBlock (xl + pos, xr + pos, yl + pos, yr + pos, m);
    }
}

void Engine::processBlock (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    const int slope = std::clamp ((int)std::lround (p[kSlope]), (int)kSlope12, (int)kSlope24);
    const double hpBase = p[kHpFreq], lpBase = p[kLpFreq];
    const double qHp = resonanceToQ (p[kHpRes], slope), qLp = resonanceToQ (lpRes (), slope);
    const double envAmount = p[kEnvAmount];
    const double attackStep = 1.0 / std::max (1.0, p[kEnvAttack] * 0.001 * sr);
    const double decayCoef = std::exp (-1.0 / std::max (1.0, p[kEnvDecay] * 0.001 * sr));
    const float mixT = (float)std::clamp (p[kDryWet], 0.0, 1.0), outT = dbToGain (p[kOutput]);
    const float hpGT = (float)filterGain (p[kHpGain]), lpGT = (float)filterGain (p[kLpGain]);
    const double offsetT = targetOffset ();
    // Vocal movement: the filter whose cutoff moved last leads, and Split swings with its sweep
    const bool vocal = std::lround (p[kMovement]) == kVocal;
    const double lpFloor = std::max (1.0, p[kLpFloor]);
    if (p[kLpFreq] != prevLpBase)
        leaderLp = true;
    else if (p[kHpFreq] != prevHpBase)
        leaderLp = false;
    prevLpBase = p[kLpFreq];
    prevHpBase = p[kHpFreq];
    // the swing: the leader's position; a new leader starts from rest
    const double lead = 12.0 * std::log2 (std::max (1.0, leaderLp ? p[kLpFreq] : p[kHpFreq]));
    if (leaderLp != liquidLeaderLp)
    {
        liquidLeaderLp = leaderLp;
        liquidSlow = lead;
    }
    double envPeak = 0.0;

    // the drive: moving it waits for the output to have faded out; it restarts from silence in its
    // new place, and the output stays silent until the stage has filled again (and the filters have
    // settled after the jump in time), then fades back in
    drive.set (p[kDriveOn] >= 0.5, p[kDrive]);
    const bool wantPost = std::lround (p[kDrivePos]) == kDrivePost;
    if (wantPost != drivePost && duck <= 0.0f)
    {
        drivePost = wantPost;
        drive.reset ();
        duckHold = drive.latency () + (int)std::lround (0.002 * sr);
    }
    const bool moving = wantPost != drivePost;
    // the input is copied, as the output may be the same buffer; Pre drives it before the filters
    std::copy (xl, xl + n, src[0].data ());
    std::copy (xr, xr + n, src[1].data ());
    if (meters) // the spectrum's input is what came in, before the drive
        for (int i = 0; i < n; ++i)
            scopeIn[(size_t)i] = 0.5f * (xl[i] + xr[i]);
    float* const ins2[2] = {src[0].data (), src[1].data ()};
    float* const mix2[2] = {mixed[0].data (), mixed[1].data ()};
    if (!drivePost)
        drive.process (ins2, n);

    for (int i = 0; i < n; ++i)
    {
        // envelope: linear attack to 1, exponential decay
        if (envRising)
        {
            env += attackStep;
            if (env >= 1.0)
            {
                env = 1.0;
                envRising = false;
            }
        }
        else
            env = env > 1e-4 ? env * decayCoef : 0.0;
        envPeak = std::max (envPeak, env);

        // where the cutoffs are heading, glided
        offset += (offsetT - offset) * semiSmooth;
        liquidSlow += (lead - liquidSlow) * liquidA;
        // Vocal: Split swings with the sweep so the leader overshoots the way it moves (a positive
        // Split raises the high-pass and lowers the low-pass)
        const double swing = vocal ? std::clamp ((lead - liquidSlow) * (leaderLp ? -2.0 : 2.0), -36.0, 36.0) : 0.0;
        split += (p[kSplit] + envAmount * env + swing - split) * semiSmooth;
        if (i % kCoeffInterval == 0)
        {
            double hz = hpCutoff (hpBase, offset, split), lz = std::max (lpFloor, lpCutoff (lpBase, offset, split));
            rawHp = hz;
            rawLp = lz;
            hpMulT = lpMulT = 1.0f;
            if (vocal) // crossed: the follower sits at the leader's cutoff and fades out
                vocalPush (hz, lz, leaderLp, p[kFade], p[kDipStart], hpMulT, lpMulT);
            curHp = hz;
            curLp = lz;
            hpC.set (hz, qHp, sr);
            lpC.set (lz, qLp, sr);
            hpG1 = onePoleG (hz, sr);
            lpG1 = onePoleG (lz, sr);
        }
        mix += (mixT - mix) * smooth;
        out += (outT - out) * smooth;
        hpMul += (hpMulT - hpMul) * smooth;
        lpMul += (lpMulT - lpMul) * smooth;
        hpG += (hpGT - hpG) * smooth;
        lpG += (lpGT - lpG) * smooth;

        const float ins[2] = {src[0][(size_t)i], src[1][(size_t)i]};
        float* outs[2] = {yl, yr};
        for (int c = 0; c < 2; ++c)
        {
            const double x = ins[c];
            double l, h, l2, h2, tmp, wet;
            if (slope == kSlope18)
            {
                // Butterworth 3: a first-order and a second-order section (Q 1); the pair is in
                // quadrature, so it sums flat without inverting
                double h1, l1;
                hp1[c].tick (hpG1, x, tmp, h1);
                lp1[c].tick (lpG1, x, l1, tmp);
                hp[c][0].tick (hpC, h1, tmp, h);
                lp[c][0].tick (lpC, l1, l, tmp);
                wet = hpG * hpMul * h + lpG * lpMul * l;
            }
            else
            {
                hp[c][0].tick (hpC, x, tmp, h);
                lp[c][0].tick (lpC, x, l, tmp);
                if (slope == kSlope24)
                {
                    hp[c][1].tick (hpC, h, tmp, h2);
                    lp[c][1].tick (lpC, l, l2, tmp);
                    h = h2;
                    l = l2;
                }
                // second-order sections are 180 degrees apart at the crossing, so the high-pass is
                // inverted at 12 dB; the fourth-order pair is back in phase
                wet = slope == kSlope24 ? hpG * hpMul * h + lpG * lpMul * l : lpG * lpMul * l - hpG * hpMul * h;
            }
            const double y = x * (1.0 - mix) + wet * mix;
            if (drivePost)
                mixed[c][(size_t)i] = (float)y; // Output comes after the drive
            else
                outs[c][i] = (float)(y * out);
        }
        gOut[(size_t)i] = out;
    }
    if (drivePost)
    {
        drive.process (mix2, n);
        for (int c = 0; c < 2; ++c)
        {
            float* o = c == 0 ? yl : yr;
            for (int i = 0; i < n; ++i)
                o[i] = mixed[c][(size_t)i] * gOut[(size_t)i];
        }
    }
    if (moving || duck < 1.0f || duckHold > 0)
        for (int i = 0; i < n; ++i)
        {
            if (moving)
                duck = std::max (0.0f, duck - duckStep);
            else if (duckHold > 0)
                --duckHold;
            else
                duck = std::min (1.0f, duck + duckStep);
            yl[i] *= duck;
            yr[i] *= duck;
        }
    if (meters)
        for (int i = 0; i < n; ++i)
            meters->scope.push (scopeIn[(size_t)i], 0.5f * (yl[i] + yr[i]));
    if (hasTail)
        tail.process (yl, yr, n);
    if (meters)
    {
        meters->offset.store ((float)offset, std::memory_order_relaxed);
        meters->hpHz.store ((float)curHp, std::memory_order_relaxed);
        meters->hpMul.store (hpMul, std::memory_order_relaxed);
        meters->lpMul.store (lpMul, std::memory_order_relaxed);
        meters->lpHz.store ((float)curLp, std::memory_order_relaxed);
        // how far the engine has moved the filters from where they are set: the display adds this to
        // the current settings, so edits show at once and the movement shows too
        meters->hpShift.store ((float)(12.0 * std::log2 (rawHp / std::max (1.0, p[kHpFreq]))), std::memory_order_relaxed);
        meters->lpShift.store ((float)(12.0 * std::log2 (rawLp / std::max (1.0, p[kLpFreq]))), std::memory_order_relaxed);
        meters->leaderLp.store (leaderLp, std::memory_order_relaxed);
        meters->blocks.fetch_add (1, std::memory_order_relaxed);
        meters->env.store ((float)envPeak, std::memory_order_relaxed);
        meters->note.store (lastNote, std::memory_order_relaxed);
    }
}

} // namespace para
