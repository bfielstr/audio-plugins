#include "Limiter.h"

#include <cmath>

namespace detonatr {

namespace {
constexpr float kTpMargin = 0.97724f; // 0.2 dB: what the interpolator can miss between its points (0.1 dB let 0.07 dB through on some material)

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
} // namespace

// --- the fast part ---

void Limiter::FastGain::Box::prepare (int len)
{
    buf.assign ((size_t)std::max (1, len), 1.0);
    sum = (double)buf.size ();
    pos = 0;
}

double Limiter::FastGain::Box::push (double x)
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

void Limiter::FastGain::prepare (int w)
{
    window = std::max (1, w);
    wVal.assign ((size_t)window + 1, 0.0f);
    wIdx.assign ((size_t)window + 1, 0);
    wHead = wCount = 0;
    count = 0;
    env = 0.0f;
    const int a = (window + 1) / 2, b = window + 1 - a; // a + b - 1 = window
    b1.prepare (a);
    b2.prepare (b);
}

float Limiter::FastGain::push (float gr, float rel)
{
    gr = std::max (0.0f, gr);
    // the largest reduction in the window (a monotonic wedge)
    const int cap = (int)wVal.size ();
    while (wCount > 0)
    {
        const int back = (wHead + wCount - 1) % cap;
        if (wVal[(size_t)back] > gr)
            break;
        --wCount;
    }
    const int at = (wHead + wCount) % cap;
    wVal[(size_t)at] = gr;
    wIdx[(size_t)at] = count;
    ++wCount;
    while (wIdx[(size_t)wHead] <= count - window)
    {
        wHead = (wHead + 1) % cap;
        --wCount;
    }
    ++count;
    const float top = wVal[(size_t)wHead];
    // the release only ever lowers the gain below what the window needs
    env = top >= env ? top : env + (top - env) * rel;
    const double g = env > 1e-6f ? (double)dsp::dbToAmp (-env) : 1.0;
    return (float)b2.push (b1.push (g));
}

// --- the limiter ---

void Limiter::prepare (double sampleRate, int)
{
    sr = sampleRate;
    maxWindow = std::max (2, (int)std::lround (kMaxLookMs * 0.001 * sr));
    // the true-peak interpolator: a Kaiser-windowed sinc per phase
    for (int f = 1; f < kOver; ++f)
    {
        const double frac = (double)f / kOver;
        double sum = 0.0;
        for (int j = 0; j < 2 * kHalfTaps; ++j)
        {
            const double t = (double)(j - kHalfTaps + 1) - frac; // tap j holds x[m - kHalfTaps + 1 + j]
            const double sinc = std::fabs (t) < 1e-12 ? 1.0 : std::sin (dsp::kPi * t) / (dsp::kPi * t);
            const double r = t / (kHalfTaps + 0.5);
            const double w = besselI0 (6.0 * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (6.0);
            taps[f - 1][j] = (float)(sinc * w);
            sum += sinc * w;
        }
        for (int j = 0; j < 2 * kHalfTaps; ++j)
            taps[f - 1][j] = (float)(taps[f - 1][j] / sum);
    }
    for (auto& h : hist)
        h.assign (4 * kHalfTaps, 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        audio[c].prepare (latency ());
        overDelay[c].prepare (maxWindow);
        slowDelay[c].prepare (maxWindow);
    }
    setAttackMs (attackMs);
    setReleaseMs (releaseMs);
    dirty = true;
    reset ();
}

void Limiter::configure ()
{
    window = std::clamp ((int)std::lround (lookMs * 0.001 * sr), 1, maxWindow);
    for (int c = 0; c < 2; ++c)
    {
        fast[c].prepare (window);
        overDelay[c].reset ();
        slowDelay[c].reset ();
    }
    dirty = false;
}

void Limiter::reset ()
{
    configure ();
    for (auto& h : hist)
        std::fill (h.begin (), h.end (), 0.0f);
    histPos = 0;
    for (int c = 0; c < 2; ++c)
    {
        audio[c].reset ();
        prevSeg[c] = 0.0f;
        slowEnv[c] = 0.0f;
    }
    maxReduction = 0.0f;
}

void Limiter::setCeilingDb (double db) { ceil = (float)std::pow (10.0, std::min (0.0, db) / 20.0); }

void Limiter::setLookaheadMs (double ms)
{
    ms = std::clamp (ms, 0.02, kMaxLookMs);
    if (ms != lookMs)
    {
        lookMs = ms;
        dirty = true;
    }
}

void Limiter::setAttackMs (double ms)
{
    attackMs = ms;
    attC = dsp::coef (ms, sr);
}

void Limiter::setReleaseMs (double ms)
{
    releaseMs = ms;
    relC = dsp::coef (ms, sr);
    fastRel = relC;
}

void Limiter::setTruePeak (bool on) { truePeak = on; }

float Limiter::takeReductionDb ()
{
    const float r = maxReduction;
    maxReduction = 0.0f;
    return r;
}

void Limiter::process (float* l, float* r, int n)
{
    if (dirty)
        configure ();
    float* io[2] = {l, r};
    const int H = kHalfTaps, W = window, lat = latency ();
    const int overLen = maxWindow - W, slowLen = W - 1;
    // (a hair under the ceiling even without True Peak: the fast dB conversions are good to 1e-4)
    const float detCeil = ceil * (truePeak ? kTpMargin : 0.9995f);
    const float lk = link, at = attC, re = relC, fr = fastRel, dr = drive, cl = ceil;
    for (int i = 0; i < n; ++i)
    {
        float over[2];
        float x[2] = {io[0][i] * dr, io[1][i] * dr};
        // the history (written twice, so hist[c][histPos .. histPos + 2H - 1] is the last 2H, oldest first)
        for (int c = 0; c < 2; ++c)
        {
            hist[c][(size_t)histPos] = x[c];
            hist[c][(size_t)(histPos + 2 * H)] = x[c];
        }
        histPos = histPos + 1 >= 2 * H ? 0 : histPos + 1;
        for (int c = 0; c < 2; ++c)
        {
            const float* h = hist[c].data () + histPos; // h[j] = x[m - H + 1 + j] with m = n - H
            float pk = std::fabs (h[H - 1]);
            if (truePeak)
            {
                float seg = 0.0f;
                for (int f = 0; f < kOver - 1; ++f)
                {
                    float acc = 0.0f;
                    for (int j = 0; j < 2 * H; ++j)
                        acc += taps[f][j] * h[j];
                    seg = std::max (seg, std::fabs (acc));
                }
                pk = std::max ({pk, seg, prevSeg[c]});
                prevSeg[c] = seg;
            }
            over[c] = dsp::ampToDb ((pk + 1e-20f) / detCeil);
        }
        const float top = std::max (over[0], over[1]);
        float gain[2];
        for (int c = 0; c < 2; ++c)
        {
            float o = over[c] + (top - over[c]) * lk;
            o = overDelay[c].push (o, overLen);
            // the slow part: the body, followed with Attack and Release
            float& e = slowEnv[c];
            e += (o - e) * (o > e ? at : re);
            const float slow = std::max (0.0f, e);
            // the fast part: what is left, looked ahead
            const float g = fast[c].push (o - slow, fr);
            const float slowNow = slowDelay[c].push (slow, slowLen);
            gain[c] = g * dsp::dbToAmp (-slowNow);
        }
        for (int c = 0; c < 2; ++c)
        {
            float y = audio[c].push (x[c], lat) * gain[c];
            if (std::fabs (y) > cl)
            {
                y = y > 0.0f ? cl : -cl;
                ++clampCount;
            }
            io[c][i] = y;
            maxReduction = std::max (maxReduction, -dsp::ampToDb (gain[c] + 1e-12f));
        }
    }
    for (auto& e : slowEnv)
        if (e < -200.0f)
            e = -200.0f;
}

} // namespace detonatr
