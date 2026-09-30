#include "Limiter.h"

#include <cmath>

namespace smoothr {

namespace {
constexpr double kDbToLn = 0.11512925464970229; // ln (10) / 20
constexpr float kSnapDb = 1e-5f;                // a reduction this small is none (so an idle limiter is exact)
constexpr double kPastZeroDb = 1.0;             // where the releases aim (see LookaheadGain::push)
// the detection works this far under the ceiling, so rounding never takes a sample over it
constexpr double kMargin = 0.99;     // 0.09 dB: checked at eight points a sample, a peak between two of them can be that much higher (at 18 kHz, 48 kHz)
constexpr double kLowMargin = kMargin * 0.998; // the lows a little further under (0.02 dB more), so the highs always have room

double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    const double hx = 0.5 * x;
    for (int k = 1; k < 64; ++k)
    {
        term *= (hx / k) * (hx / k);
        sum += term;
        if (term < 1e-14 * sum)
            break;
    }
    return sum;
}

// The most g (0..1) can be with |a + g b| <= c, for |a| <= c: the highs' room next to the lows.
inline float roomFor (float a, float b, float c)
{
    const float mb = std::fabs (b);
    if (mb < 1e-12f)
        return 1.0f;
    const float g = (c - (b > 0.0f ? a : -a)) / mb;
    return std::clamp (g, 0.0f, 1.0f);
}

inline float overDbOf (float gain) { return gain >= 1.0f ? 0.0f : (float)(-std::log (std::max (gain, 1e-6f)) / kDbToLn); } // at most 120 dB
} // namespace

// --- LookaheadGain ---

void LookaheadGain::Box::prepare (int n, double v)
{
    buf.assign ((size_t)std::max (1, n), v);
    sum = v * (double)buf.size ();
    pos = 0;
}

void LookaheadGain::Box::fill (double v)
{
    std::fill (buf.begin (), buf.end (), v);
    sum = v * (double)buf.size ();
    pos = 0;
}

double LookaheadGain::Box::push (double x)
{
    sum += x - buf[(size_t)pos];
    buf[(size_t)pos] = x;
    if (++pos >= (int)buf.size ())
    {
        // add it up afresh once a round, so rounding never builds up (and a run of 1s gives exactly 1)
        pos = 0;
        sum = 0.0;
        for (double v : buf)
            sum += v;
    }
    return sum / (double)buf.size ();
}

void LookaheadGain::prepare (int w)
{
    window = std::max (1, w);
    wVal.assign ((size_t)window + 1, 0.0f);
    wIdx.assign ((size_t)window + 1, 0);
    const int w1 = (window + 1) / 2, w2 = window + 1 - w1; // w1 + w2 - 1 = window
    box1.prepare (w1, 1.0);
    box2.prepare (w2, 1.0);
    reset ();
}

void LookaheadGain::reset ()
{
    wHead = wCount = 0;
    count = 0;
    env = sus = 0.0;
    box1.fill (1.0);
    box2.fill (1.0);
}

void LookaheadGain::setRelease (double r, bool a, double up, double down)
{
    rel = r;
    autoRel = a;
    susUp = up;
    susDown = down;
}

float LookaheadGain::push (float overDb, float atLeastDb)
{
    // the soft knee: the reduction starts knee/2 under the ceiling and reaches the hard line knee/2 over
    // it, never under it (so the gain is never above what the sample needs)
    float gr;
    if (knee > 0.0)
    {
        const float h = (float)(0.5 * knee);
        if (overDb <= -h)
            gr = 0.0f;
        else if (overDb < h)
            gr = (overDb + h) * (overDb + h) / (float)(2.0 * knee);
        else
            gr = overDb;
    }
    else
        gr = std::max (0.0f, overDb);
    gr = std::max (gr, atLeastDb);

    // the largest reduction in the window
    const int cap = (int)wVal.size ();
    while (wCount > 0)
    {
        const int back = (wHead + wCount - 1) % cap;
        if (wVal[(size_t)back] > gr)
            break;
        --wCount;
    }
    {
        const int at = (wHead + wCount) % cap;
        wVal[(size_t)at] = gr;
        wIdx[(size_t)at] = count;
        ++wCount;
    }
    while (wIdx[(size_t)wHead] <= count - window)
    {
        wHead = (wHead + 1) % cap;
        --wCount;
    }
    ++count;
    const double held = wVal[(size_t)wHead];

    // the release: only ever more reduction than the window's. It heads for kPastZeroDb under 0 dB
    // rather than 0, so it arrives in a finite time (an exponential in dB never quite gets back)
    env = std::max (held, std::max (0.0, (env + kPastZeroDb) * rel - kPastZeroDb));
    if (env < kSnapDb)
        env = held;
    double r = env;
    if (autoRel)
    {
        // what has been sustained: rises slowly towards the reduction, falls slower; the gain does not
        // come back past it, so dense limiting lets go gently and a lone peak quickly
        if (env > sus)
            sus += (env - sus) * susUp;
        else
            sus = std::max (0.0, (sus + kPastZeroDb) * susDown - kPastZeroDb);
        if (sus < kSnapDb)
            sus = 0.0;
        r = std::max (r, sus);
    }
    const double g = r > 0.0 ? std::exp (-r * kDbToLn) : 1.0;
    return (float)box2.push (box1.push (g));
}

// --- the split ---

void Limiter::Split::prepare (int len)
{
    for (auto& b : buf)
        b.assign ((size_t)std::max (1, len), 0.0);
    mid.assign ((size_t)(2 * (std::max (1, len) - 1)), 0.0);
    reset ();
}

void Limiter::Split::reset ()
{
    for (int k = 0; k < 8; ++k)
    {
        std::fill (buf[k].begin (), buf[k].end (), 0.0);
        sum[k] = 0.0;
    }
    std::fill (mid.begin (), mid.end (), 0.0);
    pos = midPos = 0;
}

double Limiter::Split::push (double x, int len)
{
    double v = x, once = 0.0;
    for (int k = 0; k < 8; ++k)
    {
        sum[k] += v - buf[k][(size_t)pos];
        buf[k][(size_t)pos] = v;
        v = sum[k] / (double)len;
        if (k == 3)
            once = v;
    }
    if (++pos >= len)
    {
        // add them up afresh once a round, so rounding never builds up
        pos = 0;
        for (int k = 0; k < 8; ++k)
        {
            double s = 0.0;
            for (double b : buf[k])
                s += b;
            sum[k] = s;
        }
    }
    // B, delayed to line up with B twice
    double onceLate = once;
    if (!mid.empty ())
    {
        onceLate = mid[(size_t)midPos];
        mid[(size_t)midPos] = once;
        if (++midPos >= (int)mid.size ())
            midPos = 0;
    }
    return 2.0 * onceLate - v;
}

double Limiter::lowResponse (double hz) const
{
    const double w = M_PI * hz / sr;
    const double s = std::sin (w);
    const double d = std::fabs (s) < 1e-12 ? 1.0 : std::sin (w * boxLen) / (boxLen * s);
    const double b = d * d * d * d;
    return 2.0 * b - b * b;
}

// --- Limiter ---

void Limiter::prepare (double sampleRate, int mb)
{
    sr = sampleRate;
    maxBlock = std::max (1, mb);
    // four boxes of boxLen samples are 6 dB down at about 0.32 sr / boxLen; each box delays by
    // (boxLen - 1) / 2, and the split runs eight
    boxLen = std::max (2, (int)std::lround (0.32 * sr / kBoxHz));
    splitDelay = 4 * (boxLen - 1);
    for (auto& s : split)
        s.prepare (boxLen);
    dryDelay.prepare (splitDelay);
    slow.prepare ((int)std::lround (kSlowMs * 0.001 * sr));
    slow.setKnee (kLowKneeDb);
    fastF.prepare ((int)std::lround (kFastMs * 0.001 * sr));
    fastH.prepare ((int)std::lround (kFastMs * 0.001 * sr));
    slowDelay.prepare (slow.delay ());
    fastDelay.prepare (kTaps + fastH.delay ());
    for (int c = 0; c < 2; ++c)
    {
        histA[c].assign (4 * kTaps, 0.0f);
        histB[c].assign (4 * kTaps, 0.0f);
    }
    histC.assign (4 * kTaps, 1.0f);
    // the interpolation of the highs: windowed sinc (Kaiser), each phase scaled to add up to 1
    const double beta = 7.0;
    for (int q = 1; q < kOver; ++q)
    {
        const double d = (double)q / kOver;
        double sum = 0.0;
        for (int k = 0; k < 2 * kTaps; ++k)
        {
            const double t = (k - kTaps + 1) - d;
            const double sinc = std::sin (M_PI * t) / (M_PI * t);
            const double u = t / (kTaps + 0.5);
            const double win = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - u * u))) / besselI0 (beta);
            phase[q - 1][k] = (float)(sinc * win);
            sum += sinc * win;
        }
        for (int k = 0; k < 2 * kTaps; ++k)
            phase[q - 1][k] = (float)(phase[q - 1][k] / sum);
    }
    ceilStep = 1.0 - std::exp (-1.0 / (0.02 * sr));
    gLowOut.assign ((size_t)maxBlock, 1.0f);
    gHighOut.assign ((size_t)maxBlock, 1.0f);
    inOut.assign ((size_t)maxBlock, 0.0f);
    dirty = true;
    reset ();
}

void Limiter::reset ()
{
    for (auto& s : split)
        s.reset ();
    slow.reset ();
    fastF.reset ();
    fastH.reset ();
    ceil = std::pow (10.0, std::clamp (ceilingDb, -60.0, 0.0) / 20.0);
    // silence in the pipeline, at the ceiling there is now
    const float now = (float)ceil;
    dryDelay.reset ({0.0f, 0.0f, now});
    slowDelay.reset ({0.0f, 0.0f, 0.0f, 0.0f, now});
    fastDelay.reset ({0.0f, 0.0f, 0.0f, 0.0f, 1.0f, now});
    for (int c = 0; c < 2; ++c)
    {
        std::fill (histA[c].begin (), histA[c].end (), 0.0f);
        std::fill (histB[c].begin (), histB[c].end (), 0.0f);
    }
    std::fill (histC.begin (), histC.end (), now);
    histPos = 0;
    prevSegF = prevSegH = 1.0f;
    highsLately = 0.0;
    clampCount = fullCount = 0;
    dirty = true;
}

void Limiter::setRelease (double ms, bool autoMode)
{
    if (ms != releaseMs || autoMode != autoRelease)
    {
        releaseMs = ms;
        autoRelease = autoMode;
        dirty = true;
    }
}

void Limiter::setSmooth (double s)
{
    if (s != smooth)
    {
        smooth = s;
        dirty = true;
    }
}

void Limiter::updateSettings ()
{
    dirty = false;
    const double sm = std::clamp (smooth, 0.0, 1.0);
    rho = std::pow (10.0, -24.0 * sm / 20.0);
    share = 1.0 - sm * sm; // all of it at 0 %, three quarters at 50 %, none at 100 %
    auto coef = [this] (double ms) { return std::exp (-1.0 / (std::max (0.1, ms) * 0.001 * sr)); };
    auto rise = [this] (double ms) { return 1.0 - std::exp (-1.0 / (std::max (0.1, ms) * 0.001 * sr)); };
    const double fast = std::clamp (releaseMs, 1.0, 5000.0);
    fastH.setRelease (coef (fast), autoRelease, rise (4.0 * fast), coef (15.0 * fast));
    fastF.setRelease (coef (fast), autoRelease, rise (4.0 * fast), coef (15.0 * fast));
    // the lows: never faster than 60 ms (three periods of 50 Hz), slower with Smooth, and at least
    // twice the highs' release
    const double low = std::max (2.0 * fast, 60.0 + 190.0 * sm);
    slow.setRelease (coef (low), autoRelease, rise (4.0 * low), coef (10.0 * low));
    // what the highs have been turned down lately: rises over a quarter of a second, falls like the
    // lows' sustained part
    susRise = rise (250.0);
    susFall = coef (10.0 * low);
}

void Limiter::process (float* l, float* r, int n)
{
    if (dirty)
        updateSettings ();
    const double ceilT = std::pow (10.0, std::clamp (ceilingDb, -60.0, 0.0) / 20.0);
    const float rh = (float)rho;
    const int span = 2 * kTaps;
    for (int i = 0; i < n; ++i)
    {
        float x0 = l[i], x1 = r[i];
        if (!std::isfinite (x0))
            x0 = 0.0f;
        if (!std::isfinite (x1))
            x1 = 0.0f;
        ceil += (ceilT - ceil) * ceilStep;

        // the split: the lows (linear phase) and the input lined up with them
        const float lo0 = (float)split[0].push (x0, boxLen), lo1 = (float)split[1].push (x1, boxLen);
        const auto dry = dryDelay.push ({x0, x1, (float)ceil});
        const float cA = dry[2];

        // the lows' gain: turned down for their own level, for the whole with the highs at rho, and by
        // their share of what the highs have been turned down lately (so when the highs are held down
        // for a while, the lows come down with them, slowly, and the balance between them holds)
        const float h0 = dry[0] - lo0, h1 = dry[1] - lo1;
        const float level = std::max ({std::fabs (lo0 + rh * h0), std::fabs (lo1 + rh * h1), std::fabs (lo0), std::fabs (lo1)});
        const float cLow = (float)(cA * kLowMargin);
        const float kneeFloor = cLow * 0.8912509f; // half the knee (1 dB) under: no reduction
        const float over = level > kneeFloor ? (float)(20.0 * std::log10 (level / cLow)) : -200.0f;
        const float gS = slow.push (over, (float)(share * highsLately));
        const auto sd = slowDelay.push ({lo0, lo1, dry[0], dry[1], cA});

        // the highs' stage: the lows after their gain (a) and the highs (b), at the samples and 7 points
        // between each two (8x): the highs interpolated properly, the lows (nothing much over 300 Hz)
        // along a straight line
        {
            const float v[5] = {gS * sd[0], gS * sd[1], sd[2] - sd[0], sd[3] - sd[1], sd[4]};
            std::vector<float>* dst[5] = {&histA[0], &histA[1], &histB[0], &histB[1], &histC};
            for (int k = 0; k < 5; ++k)
                (*dst[k])[(size_t)histPos] = (*dst[k])[(size_t)(histPos + span)] = v[k];
            if (++histPos >= span)
                histPos = 0;
        }
        // the segment from sample j (kTaps back) to the next
        const float* wc = histC.data () + histPos;
        const float cSeg = (float)(std::min (wc[kTaps - 1], wc[kTaps]) * kMargin);
        float segF = 1.0f, segH = 1.0f;
        float pa[kOver][2], pb[kOver][2];
        for (int c = 0; c < 2; ++c)
        {
            const float* wa = histA[c].data () + histPos;
            const float* wb = histB[c].data () + histPos;
            const float a0 = wa[kTaps - 1], a1 = wa[kTaps];
            pa[0][c] = a0;
            pb[0][c] = wb[kTaps - 1];
            for (int q = 1; q < kOver; ++q)
            {
                const float* h = phase[q - 1];
                float sb = 0.0f;
                for (int t = 0; t < span; ++t)
                    sb += h[t] * wb[t];
                pa[q][c] = a0 + (a1 - a0) * ((float)q / kOver);
                pb[q][c] = sb;
            }
            for (int q = 0; q < kOver; ++q)
            {
                const float m = std::fabs (pa[q][c]);
                if (m > cSeg)
                    segF = std::min (segF, cSeg / m);
            }
        }
        const float room = cSeg / segF;
        for (int q = 0; q < kOver; ++q)
            for (int c = 0; c < 2; ++c)
                segH = std::min (segH, roomFor (pa[q][c], pb[q][c], room));
        const float needF = std::min (segF, prevSegF), needH = std::min (segH, prevSegH);
        prevSegF = segF;
        prevSegH = segH;
        const float gF = fastF.push (overDbOf (needF));
        const float gH = fastH.push (overDbOf (needH));
        const auto fd = fastDelay.push ({sd[0], sd[1], sd[2], sd[3], gS, sd[4]});

        // out: gS on the lows, gH on the highs, written so that equal gains give the input exactly
        const float g0 = fd[4];
        const float y0 = gF * (gH * fd[2] + (g0 - gH) * fd[0]);
        const float y1 = gF * (gH * fd[3] + (g0 - gH) * fd[1]);
        const float top = fd[5]; // this sample's ceiling
        float o0 = y0, o1 = y1;
        if (std::fabs (o0) > top || std::fabs (o1) > top)
        {
            ++clampCount;
            o0 = std::clamp (o0, -top, top);
            o1 = std::clamp (o1, -top, top);
        }
        if (gF < 1.0f)
            ++fullCount;
        {
            // how far the highs have been turned down lately: rises over a quarter second, falls like
            // the lows' sustained part
            const double down = overDbOf (gH);
            if (down > highsLately)
                highsLately += (down - highsLately) * susRise;
            else
                highsLately = std::max (0.0, (highsLately + kPastZeroDb) * susFall - kPastZeroDb);
        }
        l[i] = o0;
        r[i] = o1;
        gLowOut[(size_t)i] = gF * g0;
        gHighOut[(size_t)i] = gF * gH;
        inOut[(size_t)i] = std::max (std::fabs (fd[2]), std::fabs (fd[3]));
    }
}

} // namespace smoothr
