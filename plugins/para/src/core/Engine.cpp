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
    driveHp.prepare (sr, maxBlock);
    driveLp.prepare (sr, maxBlock);
    for (int c = 0; c < 2; ++c)
    {
        for (auto* v : {&src[c], &dry[c], &hBuf[c], &lBuf[c]})
            v->assign ((size_t)maxBlock, 0.0f);
        bypassDelay[c].assign ((size_t)latency (), 0.0f);
        dryDelay[c].assign ((size_t)driveHp.latency (), 0.0f);
    }
    for (auto& b : hist)
        for (auto& h : b)
            h.assign ((size_t)std::max (1, (int)std::lround (0.03 * sr)), 0.0f);
    for (auto* v : {&gHp, &gLp, &gMix, &gOut, &scopeIn})
        v->assign ((size_t)maxBlock, 0.0f);
    duckStep = (float)(1.0 / (0.003 * sr)); // 3 ms fades around moving the drives
    slopeStep = 1.0 / (0.01 * sr);          // 10 ms from one slope to another
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
    hpF.hp = true;
    lpF.hp = false;
    hpF.reset (slopeOf (kHpSlope));
    lpF.reset (slopeOf (kLpSlope));
    env = 0.0;
    envRising = false;
    tail.reset ();
    driveHp.set (p[kHpDriveOn] >= 0.5, p[kHpDrive]);
    driveLp.set (p[kLpDriveOn] >= 0.5, p[kLpDrive]);
    driveHp.reset ();
    driveLp.reset ();
    drivePost = p[kDrivePos] >= 0.5;
    duck = 1.0f;
    duckHold = 0;
    for (auto& d : bypassDelay)
        std::fill (d.begin (), d.end (), 0.0f);
    bypassPos = 0;
    for (auto& d : dryDelay)
        std::fill (d.begin (), d.end (), 0.0f);
    dryPos = 0;
    for (auto& b : hist)
        for (auto& h : b)
            std::fill (h.begin (), h.end (), 0.0f);
    histPos = 0;
    prevHpBase = p[kHpFreq]; // the leader of Vocal movement is kept
    prevLpBase = p[kLpFreq];
    hpMul = lpMul = hpMulT = lpMulT = 1.0f;
    offset = targetOffset ();
    split = p[kSplit];
    liquidLeaderLp = leaderLp;
    liquidSlow = 12.0 * std::log2 (std::max (1.0, leaderLp ? p[kLpFreq] : p[kHpFreq]));
    mix = (float)std::clamp (p[kDryWet], 0.0, 1.0);
    out = dbToGain (p[kOutput]);
    hpG = gainTarget (kHpGain, kHpGainLock);
    lpG = gainTarget (kLpGain, kLpGainLock);
    curHp = rawHp = hpCutoff (p[kHpFreq], offset, split);
    curLp = rawLp = std::max (std::max (1.0, p[kLpFloor]), lpCutoff (p[kLpFreq], offset, split));
    setCoeffs (hpF, curHp, p[kHpRes], false);
    setCoeffs (lpF, curLp, lpRes (), false);
}

void Engine::setCoeffs (SlopeFade& f, double hz, double res, bool both)
{
    FilterSet& now = f.sets[f.cur];
    now.c.set (now.slope, hz, res, sr);
    if (both)
    {
        FilterSet& old = f.sets[f.cur ^ 1];
        old.c.set (old.slope, hz, res, sr);
    }
}

void Engine::startSlope (SlopeFade& f, int slope)
{
    // the other set takes the new slope; the filter as it is now, run over its input of the last 30 ms
    // (oldest first), so it fades in settled
    f.cur ^= 1;
    FilterSet& n = f.sets[f.cur];
    n.slope = slope;
    n.reset ();
    n.c.set (slope, f.hp ? curHp : curLp, f.hp ? p[kHpRes] : lpRes (), sr);
    const SlopeShape& sh = slopeShape (slope);
    const auto& h = hist[f.hp ? 0 : 1];
    const int len = (int)h[0].size ();
    for (int c = 0; c < 2; ++c)
        for (int k = 0, q = histPos; k < len; ++k)
        {
            filterTick (sh, n.c, n.st[c], h[c][(size_t)q], f.hp);
            if (++q >= len)
                q = 0;
        }
    f.fade = 0.0;
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
    const double hpBase = p[kHpFreq], lpBase = p[kLpFreq];
    const double resHp = p[kHpRes], resLp = lpRes ();
    const double envAmount = p[kEnvAmount];
    const double attackStep = 1.0 / std::max (1.0, p[kEnvAttack] * 0.001 * sr);
    const double decayCoef = std::exp (-1.0 / std::max (1.0, p[kEnvDecay] * 0.001 * sr));
    const float mixT = (float)std::clamp (p[kDryWet], 0.0, 1.0), outT = dbToGain (p[kOutput]);
    const float hpGT = gainTarget (kHpGain, kHpGainLock), lpGT = gainTarget (kLpGain, kLpGainLock);
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

    // the drives: moving them waits for the output to have faded out; they restart from silence in
    // their new place, and the output stays silent until the stages have filled again (and the filters
    // have settled after the jump in time), then fades back in
    driveHp.set (p[kHpDriveOn] >= 0.5, p[kHpDrive]);
    driveLp.set (p[kLpDriveOn] >= 0.5, p[kLpDrive]);
    const bool wantPost = std::lround (p[kDrivePos]) == kDrivePost;
    if (wantPost != drivePost && duck <= 0.0f)
    {
        drivePost = wantPost;
        driveHp.reset ();
        driveLp.reset ();
        duckHold = driveHp.latency () + (int)std::lround (0.002 * sr);
    }
    const bool moving = wantPost != drivePost;
    // a filter's new slope: its other set starts with it and fades in over this one (a change during a
    // fade waits for the fade to end); each filter on its own
    for (SlopeFade* f : {&hpF, &lpF})
    {
        const int slopeT = slopeOf (f->hp ? kHpSlope : kLpSlope);
        if (slopeT != f->sets[f->cur].slope && f->fade >= 1.0)
            startSlope (*f, slopeT);
    }
    FilterSet& hNow = hpF.sets[hpF.cur];
    FilterSet& hOld = hpF.sets[hpF.cur ^ 1];
    FilterSet& lNow = lpF.sets[lpF.cur];
    FilterSet& lOld = lpF.sets[lpF.cur ^ 1];
    const SlopeShape& shHNow = slopeShape (hNow.slope);
    const SlopeShape& shHOld = slopeShape (hOld.slope);
    const SlopeShape& shLNow = slopeShape (lNow.slope);
    const SlopeShape& shLOld = slopeShape (lOld.slope);

    // the input is copied, as the output may be the same buffer
    std::copy (xl, xl + n, src[0].data ());
    std::copy (xr, xr + n, src[1].data ());
    if (meters) // the spectrum's input is what came in, before the drives
        for (int i = 0; i < n; ++i)
            scopeIn[(size_t)i] = 0.5f * (xl[i] + xr[i]);
    // the dry part, delayed as much as the drives delay the filters' paths (on or off)
    const int dlen = (int)dryDelay[0].size ();
    for (int c = 0; c < 2; ++c)
    {
        int q = dryPos;
        for (int i = 0; i < n; ++i)
        {
            if (dlen > 0)
            {
                dry[c][(size_t)i] = dryDelay[c][(size_t)q];
                dryDelay[c][(size_t)q] = src[c][(size_t)i];
                if (++q >= dlen)
                    q = 0;
            }
            else
                dry[c][(size_t)i] = src[c][(size_t)i];
        }
    }
    if (dlen > 0)
        dryPos = (dryPos + n) % dlen;
    // Pre: each filter gets its own driven copy of the input (a drive off only delays it); Post: both
    // filter the input as it is and their outputs are driven below
    float* const hb[2] = {hBuf[0].data (), hBuf[1].data ()};
    float* const lb[2] = {lBuf[0].data (), lBuf[1].data ()};
    const float* hIn[2] = {src[0].data (), src[1].data ()};
    const float* lIn[2] = {src[0].data (), src[1].data ()};
    if (!drivePost)
    {
        for (int c = 0; c < 2; ++c)
        {
            std::copy (src[c].begin (), src[c].begin () + n, hBuf[c].begin ());
            std::copy (src[c].begin (), src[c].begin () + n, lBuf[c].begin ());
            hIn[c] = hb[c];
            lIn[c] = lb[c];
        }
        driveHp.process (hb, n);
        driveLp.process (lb, n);
    }

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
        const bool hFading = hpF.fade < 1.0, lFading = lpF.fade < 1.0;
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
            setCoeffs (hpF, hz, resHp, hFading);
            setCoeffs (lpF, lz, resLp, lFading);
        }
        mix += (mixT - mix) * smooth;
        out += (outT - out) * smooth;
        hpMul += (hpMulT - hpMul) * smooth;
        lpMul += (lpMulT - lpMul) * smooth;
        hpG += (hpGT - hpG) * smooth;
        lpG += (lpGT - lpG) * smooth;
        if (hFading)
            hpF.fade = std::min (1.0, hpF.fade + slopeStep);
        if (lFading)
            lpF.fade = std::min (1.0, lpF.fade + slopeStep);

        float* outs[2] = {yl, yr};
        for (int c = 0; c < 2; ++c)
        {
            // the filters, with the polarity that makes the pair sum flat (see Slopes.h)
            const double hx = hIn[c][i], lx = lIn[c][i];
            double h = filterTick (shHNow, hNow.c, hNow.st[c], hx, true);
            double l = filterTick (shLNow, lNow.c, lNow.st[c], lx, false);
            if (hFading)
            {
                const double ho = filterTick (shHOld, hOld.c, hOld.st[c], hx, true);
                h = ho + (h - ho) * hpF.fade;
            }
            if (lFading)
            {
                const double lo = filterTick (shLOld, lOld.c, lOld.st[c], lx, false);
                l = lo + (l - lo) * lpF.fade;
            }
            if (drivePost)
            {
                hb[c][i] = (float)h; // driven below, then summed
                lb[c][i] = (float)l;
            }
            else
            {
                const double wet = hpG * hpMul * h + lpG * lpMul * l;
                const double y = dry[c][(size_t)i] * (1.0 - mix) + wet * mix;
                outs[c][i] = (float)(y * out);
            }
        }
        gHp[(size_t)i] = hpG * hpMul;
        gLp[(size_t)i] = lpG * lpMul;
        gMix[(size_t)i] = mix;
        gOut[(size_t)i] = out;
    }
    // the filters' inputs, for a new slope's warm-up
    const int hlen = (int)hist[0][0].size ();
    for (int c = 0; c < 2; ++c)
        for (int i = 0, q = histPos; i < n; ++i)
        {
            hist[0][c][(size_t)q] = hIn[c][i];
            hist[1][c][(size_t)q] = lIn[c][i];
            if (++q >= hlen)
                q = 0;
        }
    histPos = (histPos + n) % hlen;
    if (drivePost)
    {
        // each filter's output driven, then its gain, the dry/wet and the output level
        driveHp.process (hb, n);
        driveLp.process (lb, n);
        for (int c = 0; c < 2; ++c)
        {
            float* o = c == 0 ? yl : yr;
            for (int i = 0; i < n; ++i)
            {
                const auto k = (size_t)i;
                const double wet = (double)gHp[k] * hb[c][i] + (double)gLp[k] * lb[c][i];
                o[i] = (float)((dry[c][k] * (1.0 - gMix[k]) + wet * gMix[k]) * gOut[k]);
            }
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
