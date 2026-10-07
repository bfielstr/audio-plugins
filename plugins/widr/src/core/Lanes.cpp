#include "Lanes.h"

#include <algorithm>
#include <cmath>

namespace widr {

namespace {
inline float clamp01 (float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
// 1 below a, 0 above b, a raised cosine between
float fallBetween (double f, double a, double b)
{
    if (f <= a)
        return 1.0f;
    if (f >= b)
        return 0.0f;
    return (float)(0.5 + 0.5 * std::cos (M_PI * (f - a) / (b - a)));
}
float coeff (double seconds, double rate) { return (float)(1.0 - std::exp (-1.0 / (seconds * rate))); }
inline float magOf (std::complex<float> c) { return std::sqrt (c.real () * c.real () + c.imag () * c.imag ()); }
inline float median5 (float a, float b, float c, float d, float e)
{
    // (a sorting network's median: 7 compare-and-swaps)
    auto cs = [] (float& x, float& y) {
        if (x > y)
            std::swap (x, y);
    };
    cs (a, b);
    cs (d, e);
    cs (a, d);
    cs (b, e);
    cs (b, c);
    cs (c, d);
    cs (b, c);
    return c;
}
} // namespace

void LaneSplitter::prepare (double sampleRate)
{
    sr = sampleRate;
    n = sr > 176000.0 ? 4096 : (sr > 80000.0 ? 2048 : 1024);
    hop = n / 2;
    nb = n / 2 + 1;
    mask_ = n - 1;
    fft.init (n);
    win.resize ((size_t)n);
    for (int i = 0; i < n; ++i)
        win[(size_t)i] = (float)std::sqrt (0.5 - 0.5 * std::cos (2.0 * M_PI * i / n)); // periodic Hann's square root
    olaScale = 2.0f * (float)hop / (float)n; // (the Hann windows overlap-add to n / (2 hop))
    for (auto* v : {&inL, &inR, &buf})
        v->assign ((size_t)n, 0.0f);
    for (auto& a : acc)
        a.assign ((size_t)n, 0.0f);
    for (auto& s : outSpec)
        s.assign ((size_t)nb, {});
    for (auto* v : {&xl, &xr})
        v->assign ((size_t)nb, {});
    for (auto* v : {&pLL, &pRR, &pMM, &pLRre, &pLRim, &bp1, &bp2, &modPow, &gVoice, &voiceNear, &onset, &perc, &percHold, &stab, &lmPrev, &aSm,
                    &floorNow, &curMin, &bassW, &voiceW})
        v->assign ((size_t)nb, 0.0f);
    for (auto& m : masks)
        m.assign ((size_t)nb, 0.0f);
    hist.assign ((size_t)(kHist * nb), 0.0f);
    subMin.assign ((size_t)(kSub * nb), 0.0f);
    const double fr = sr / hop; // the frame rate
    subLen = std::max (1, (int)std::lround (0.13 * fr));
    const double binHz = sr / n;
    for (int k = 0; k < nb; ++k)
    {
        const double f = k * binHz;
        bassW[(size_t)k] = fallBetween (f, 100.0, 160.0);
        voiceW[(size_t)k] = (1.0f - fallBetween (f, 90.0, 140.0)) * fallBetween (f, 6000.0, 8000.0);
    }
    aSpec = coeff (0.04, fr);
    aMod = coeff (0.25, fr);
    aStab = coeff (0.05, fr);
    aSmooth = coeff (0.02, fr);
    aVoiceUp = coeff (0.01, fr);
    aVoiceDown = coeff (0.04, fr);
    aNear = coeff (0.08, fr);
    aAct = coeff (0.15, fr);
    percRel = (float)std::exp (-1.0 / (0.04 * fr));
    aLevel = coeff (0.15, fr);
    {
        // band-pass 2 .. 8 Hz (centre 4 Hz, Q 0.7) at the frame rate, 0 dB at the centre
        const double w = 2.0 * M_PI * 4.0 / fr, c = std::cos (w), al = std::sin (w) / (2.0 * 0.7);
        bpB0 = al / (1.0 + al);
        bpB2 = -bpB0;
        bpA1 = -2.0 * c / (1.0 + al);
        bpA2 = (1.0 - al) / (1.0 + al);
    }
    reset ();
}

void LaneSplitter::reset ()
{
    for (auto* v : {&inL, &inR})
        std::fill (v->begin (), v->end (), 0.0f);
    for (auto& a : acc)
        std::fill (a.begin (), a.end (), 0.0f);
    for (auto* v : {&pLL, &pRR, &pMM, &pLRre, &pLRim, &bp1, &bp2, &modPow, &gVoice, &voiceNear, &percHold, &stab, &lmPrev, &aSm, &floorNow})
        std::fill (v->begin (), v->end (), 0.0f);
    std::fill (subMin.begin (), subMin.end (), 0.0f);
    std::fill (curMin.begin (), curMin.end (), 1e30f);
    std::fill (hist.begin (), hist.end (), 0.0f);
    for (auto& m : masks)
        std::fill (m.begin (), m.end (), 0.0f);
    level.fill (0.0f);
    voiceAct = 0.0f;
    histPos = subPos = subCount = count = 0;
    t = 0;
}

void LaneSplitter::tick (float l, float r, LaneSample& out)
{
    const size_t w = (size_t)(t & (uint64_t)mask_);
    inL[w] = l;
    inR[w] = r;
    if (++count == hop)
    {
        count = 0;
        frame ();
    }
    // the sample n - 1 ago: complete in the accumulators (the last frame covering it ended now or before)
    const size_t d = (size_t)((t + 1) & (uint64_t)mask_);
    out.inL = inL[d];
    out.inR = inR[d];
    out.voice = acc[kVoice][d];
    out.sideCut = acc[kSideCut][d];
    out.bassMid = acc[kBassMid][d];
    out.feedWide = acc[kFeedWide][d];
    out.feedBeyond = acc[kFeedBeyond][d];
    out.beyondSide = acc[kBeyondSide][d];
    const int used = full ? kNumOut : kNumMixes;
    if (full)
    {
        const float v = out.voice;
        out.l[kLaneVoice] = out.r[kLaneVoice] = v;
        float sl = v, sr2 = v;
        for (int j = 0; j < 3; ++j)
        {
            out.l[(size_t)(kLaneBass + j)] = acc[(size_t)(kFullBassL + 2 * j)][d];
            out.r[(size_t)(kLaneBass + j)] = acc[(size_t)(kFullBassL + 2 * j + 1)][d];
            sl += out.l[(size_t)(kLaneBass + j)];
            sr2 += out.r[(size_t)(kLaneBass + j)];
        }
        // the rest: what makes the five sum to the input
        out.l[kLaneAmbience] = out.inL - sl;
        out.r[kLaneAmbience] = out.inR - sr2;
    }
    for (int o = 0; o < used; ++o)
        acc[(size_t)o][d] = 0.0f;
    ++t;
}

void LaneSplitter::frame ()
{
    ++frameCount;
    // the last n samples, oldest first (the newest is at t)
    const uint64_t start = t + 1; // (t - n + 1, modulo n)
    for (int i = 0; i < n; ++i)
        buf[(size_t)i] = inL[(size_t)((start + (uint64_t)i) & (uint64_t)mask_)] * win[(size_t)i];
    fft.forward (buf.data (), xl.data ());
    for (int i = 0; i < n; ++i)
        buf[(size_t)i] = inR[(size_t)((start + (uint64_t)i) & (uint64_t)mask_)] * win[(size_t)i];
    fft.forward (buf.data (), xr.data ());
    const float eps = 1e-12f;

    // 1. per bin: the magnitude into the time history, the smoothed spectra, the steady floor, the
    //    level in dB (over a floor 30 dB under the frame's mean), how much it moves from frame to frame
    //    and at a syllable rate, and how far it rises over its own recent past (an onset)
    double meanPow = 0.0;
    for (int k = 0; k < nb; ++k)
        meanPow += (double)std::norm (0.5f * (xl[(size_t)k] + xr[(size_t)k]));
    const float floorPow = (float)(1e-3 * meanPow / nb) + 1e-6f;
    float* h = hist.data () + (size_t)histPos * (size_t)nb;
    for (int k = 0; k < nb; ++k)
    {
        const size_t i = (size_t)k;
        const cf L = xl[i], R = xr[i], M = 0.5f * (L + R);
        const float a = 0.5f * (magOf (L) + magOf (R));
        h[k] = a;
        const float nl = std::norm (L), nr = std::norm (R), nm = std::norm (M);
        pLL[i] += (nl - pLL[i]) * aSpec;
        pRR[i] += (nr - pRR[i]) * aSpec;
        pMM[i] += (nm - pMM[i]) * aSpec;
        pLRre[i] += (L.real () * R.real () + L.imag () * R.imag () - pLRre[i]) * aSpec;
        pLRim[i] += (L.imag () * R.real () - L.real () * R.imag () - pLRim[i]) * aSpec;
        // onsets: a rise well over the median of the bin's last frames (what is steady in time)
        const size_t s = (size_t)nb;
        const float H = median5 (hist[i], hist[s + i], hist[2 * s + i], hist[3 * s + i], hist[4 * s + i]);
        const float q = 1.5f * H / (a + eps);
        onset[i] = clamp01 (1.0f - q * q);
        // the steady floor: the minimum of the (lightly smoothed) magnitude over the last ~0.5 s
        // (minimum statistics, Martin 2001): what a held tone keeps under a voice or a hit
        aSm[i] += (a - aSm[i]) * aSmooth;
        curMin[i] = std::min (curMin[i], aSm[i]);
        float fl = curMin[i];
        for (int j = 0; j < kSub; ++j)
            fl = std::min (fl, subMin[(size_t)j * s + i]);
        floorNow[i] = fl;
        // the level, its frame-to-frame movement (noise moves, a held tone does not) and its
        // syllable-rate modulation
        const float lm = 10.0f * std::log10 (nm + floorPow);
        stab[i] += (std::fabs (lm - lmPrev[i]) - stab[i]) * aStab;
        lmPrev[i] = lm;
        const double y = bpB0 * lm + bp1[i];
        bp1[i] = (float)(-bpA1 * y + bp2[i]);
        bp2[i] = (float)(bpB2 * lm - bpA2 * y);
        modPow[i] += ((float)(y * y) - modPow[i]) * aMod;
    }
    histPos = (histPos + 1) % kHist;
    if (++subCount >= subLen)
    {
        subCount = 0;
        std::copy (curMin.begin (), curMin.end (), subMin.begin () + (long)((size_t)subPos * (size_t)nb));
        std::fill (curMin.begin (), curMin.end (), 1e30f);
        subPos = (subPos + 1) % kSub;
    }
    // 2. percussive: the onsets spread over the neighbouring bins (+-kVert: a hit is broadband), held
    //    while it rings down
    {
        double run = 0.0;
        int lo = 0, hi = -1;
        for (int k = 0; k < nb; ++k)
        {
            while (hi < std::min (nb - 1, k + kVert))
                run += onset[(size_t)++hi];
            while (lo < k - kVert)
                run -= onset[(size_t)lo++];
            const float now = clamp01 ((float)(run / (hi - lo + 1)) * 1.6f);
            percHold[(size_t)k] = std::max (now, percHold[(size_t)k] * percRel);
            perc[(size_t)k] = percHold[(size_t)k];
        }
    }
    // 3. the Voice gain per bin: the centre estimate (least squares: the part of the mid common to L
    //    and R), where the level moves at a syllable rate, over the steady floor, not a hit, in the voice
    //    band; sharpened. Held where it is (the voice guard), with how much of the band it holds.
    double ev = 0.0, et = 0.0;
    for (int k = 0; k < nb; ++k)
    {
        const size_t i = (size_t)k;
        const float centre = clamp01 ((clamp01 (pLRre[i] / (pMM[i] + eps)) - 0.3f) / 0.55f);
        const float syllable = clamp01 ((std::sqrt (modPow[i]) - 2.0f) / 3.0f);
        const float steady = std::min (1.0f, floorNow[i] / (hist[(size_t)((histPos + kHist - 1) % kHist) * (size_t)nb + i] + eps));
        const float raw = centre * syllable * (1.0f - perc[i]) * voiceW[i] * clamp01 ((0.75f - steady * steady) / 0.75f);
        const float gT = clamp01 ((raw - 0.05f) / 0.4f);
        gVoice[i] += (gT - gVoice[i]) * (gT > gVoice[i] ? aVoiceUp : aVoiceDown);
        const double pm = (double)std::norm (0.5f * (xl[i] + xr[i])) * voiceW[i];
        ev += pm * gVoice[i] * gVoice[i];
        et += pm;
    }
    for (int k = 0; k < nb; ++k)
    {
        const size_t i = (size_t)k;
        const float near = std::max ({gVoice[i], k > 0 ? gVoice[i - 1] : 0.0f, k + 1 < nb ? gVoice[i + 1] : 0.0f});
        voiceNear[i] += (near - voiceNear[i]) * (near > voiceNear[i] ? aVoiceUp : aNear);
    }
    {
        const float act = et > 1e-9 ? clamp01 ((float)(2.0 * ev / et)) : 0.0f;
        voiceAct += (act - voiceAct) * (act > voiceAct ? aVoiceUp : aAct);
    }
    // 4. the masks, and the mixes by the lanes' Positions and Widths
    std::array<double, kNumLanes> e {};
    for (int k = 0; k < nb; ++k)
    {
        const size_t i = (size_t)k;
        const cf L = xl[i], R = xr[i], M = 0.5f * (L + R);
        float gv = gVoice[i], near = voiceNear[i];
        cf V = gv * M;
        // what Voice leaves: Bass by frequency; of the rest, Ambience where diffuse (low coherence) or
        // noise-like (the level moves, no steady floor under it), Hits where percussive, Tones where steady
        const float pp = perc[i], hp = 1.0f - pp;
        const float coh = (pLRre[i] * pLRre[i] + pLRim[i] * pLRim[i]) / (pLL[i] * pRR[i] + eps);
        const float diffuse = clamp01 ((0.8f - coh) / 0.5f);
        const float aRest = 0.5f * (magOf (L - V) + magOf (R - V));
        const float held = clamp01 ((floorNow[i] / (aRest + eps) - 0.35f) / 0.45f);
        const float noisy = clamp01 ((stab[i] - 1.0f) / 2.0f) * (1.0f - held);
        const float b = bassW[i], rest = 1.0f - b;
        std::array<float, kNumLanes> m;
        m[kLaneBass] = b;
        m[kLaneHits] = rest * (1.0f - diffuse) * pp;
        m[kLaneTones] = rest * (1.0f - diffuse) * hp * (1.0f - noisy);
        m[kLaneAmbience] = rest * (diffuse + (1.0f - diffuse) * hp * noisy);
        if (replay)
        {
            const float* rr = replay->data () + replayPos;
            replayPos = std::min (replay->size () - kRec, replayPos + kRec);
            gv = rr[0];
            V = gv * M;
            near = rr[1];
            m[kLaneHits] = rr[2];
            m[kLaneTones] = rr[3];
            m[kLaneAmbience] = rr[4];
        }
        else if (record)
            record->insert (record->end (), {gv, near, m[kLaneHits], m[kLaneTones], m[kLaneAmbience]});
        masks[kLaneVoice][i] = gv;
        for (int j = 1; j < kNumLanes; ++j)
            masks[(size_t)j][i] = m[(size_t)j];
        const cf RL = L - V, RR = R - V, rm = 0.5f * (RL + RR), rs = 0.5f * (RL - RR);
        // the guards: the feeds give way where the voice is (and across its band while it speaks), and
        // where a hit is (but the Hits lane's own)
        const float duckVoice = (1.0f - clamp01 (2.0f * near)) * (1.0f - 0.7f * voiceAct * voiceW[i]);
        float cut = 0.0f, wide = 0.0f, beyond = 0.0f, beyondS = 0.0f;
        for (int j = 1; j < kNumLanes; ++j)
        {
            const size_t jj = (size_t)j;
            if (pos[jj] == kPosCentre)
                cut += (1.0f - wid[jj]) * m[jj];
            else
            {
                const float f = wid[jj] * m[jj] * duckVoice * (j == kLaneHits ? 1.0f : hp);
                (pos[jj] == kPosBeyond ? beyond : wide) += f;
                if (pos[jj] == kPosBeyond)
                    beyondS += wid[jj] * m[jj];
            }
        }
        const float vWide = pos[kLaneVoice] == kPosWide ? wid[kLaneVoice] : 0.0f;
        const float vBeyond = pos[kLaneVoice] == kPosBeyond ? wid[kLaneVoice] : 0.0f;
        outSpec[kVoice][i] = V;
        outSpec[kSideCut][i] = cut * rs;
        outSpec[kBassMid][i] = b * rm;
        outSpec[kFeedWide][i] = wide * rm + vWide * V;
        outSpec[kFeedBeyond][i] = beyond * rm + vBeyond * V;
        outSpec[kBeyondSide][i] = beyondS * rs;
        if (full)
            for (int j = 0; j < 3; ++j)
            {
                const float mj = m[(size_t)(kLaneBass + j)];
                outSpec[(size_t)(kFullBassL + 2 * j)][i] = mj * RL;
                outSpec[(size_t)(kFullBassL + 2 * j + 1)][i] = mj * RR;
            }
        const double wgt = (k == 0 || k == nb - 1) ? 1.0 : 2.0;
        const double rest2 = (double)std::norm (RL) + std::norm (RR);
        e[kLaneVoice] += wgt * 2.0 * std::norm (V);
        for (int j = 1; j < kNumLanes; ++j)
            e[(size_t)j] += wgt * m[(size_t)j] * m[(size_t)j] * rest2;
    }
    // back to time: synthesis window, overlap-add
    const int used = full ? kNumOut : kNumMixes;
    for (int o = 0; o < used; ++o)
    {
        auto& a = acc[(size_t)o];
        bool silent = true;
        for (int k = 0; k < nb && silent; ++k)
            silent = outSpec[(size_t)o][(size_t)k] == cf (0.0f, 0.0f);
        if (silent)
            continue; // (a mix with nothing in it adds exact zeros)
        fft.inverse (outSpec[(size_t)o].data (), buf.data ());
        for (int i = 0; i < n; ++i)
            a[(size_t)((start + (uint64_t)i) & (uint64_t)mask_)] += olaScale * win[(size_t)i] * buf[(size_t)i];
    }
    // levels (the window's power gain and the FFT's scale out: relative levels are what the display shows)
    const double scale = 1.0 / ((double)n * (double)n * 0.375);
    for (int j = 0; j < kNumLanes; ++j)
        level[(size_t)j] += ((float)(e[(size_t)j] * scale) - level[(size_t)j]) * aLevel;
}

} // namespace widr
