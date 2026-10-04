#include "Motion.h"

#include <cmath>

namespace orbitr {

namespace {
constexpr double kTwoPi = 2.0 * dsp::kPi;
constexpr double kAxis[3] = {1.0, 1.0, 0.4}; // the swarm is flatter in height
const double kAxisNorm = 1.0 / std::sqrt (1.0 + 1.0 + 0.16);
constexpr double kAxisRate[3][2] = {{1.0, 2.3}, {1.13, 2.71}, {0.87, 1.9}};

// a small deterministic generator: -1 .. 1
struct Lcg
{
    uint32_t s;
    double next ()
    {
        s = s * 1664525u + 1013904223u;
        return (double)(s >> 8) / (double)(1u << 23) - 1.0;
    }
};

// the grains' Hann window, kWindow + 1 points (the last for the interpolation's neighbour)
constexpr int kWindow = 1024;
struct HannTable
{
    float w[kWindow + 2];
    HannTable ()
    {
        for (int i = 0; i <= kWindow + 1; ++i)
            w[i] = (float)(0.5 - 0.5 * std::cos (kTwoPi * std::min (i, kWindow) / kWindow));
    }
};
const HannTable kHann;
inline float hann (double phase) // phase 0 .. 1
{
    const double x = phase * kWindow;
    const int i = std::clamp ((int)x, 0, kWindow);
    const float f = (float)(x - i);
    return kHann.w[i] + (kHann.w[i + 1] - kHann.w[i]) * f;
}
} // namespace

void Motion::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    baseDelay = (int)std::lround (kBaseMs * 0.001 * sr);
    // the longest delay: the base, plus a source Radius beyond the centre and its reflection's detour
    const double maxSec = kBaseMs * 0.001 + (3.0 * kMaxRadius + 2.0 * kEarHeight + 1.0) / kSoundSpeed;
    const int need = (int)(maxSec * sr) + std::max (1, maxBlock) + 8;
    int size = 1;
    while (size < need)
        size <<= 1;
    buf.assign ((size_t)size, 0.0f);
    mask = size - 1;
    orbBuf.assign ((size_t)size * kMaxOrbs, 0.0f);
    // the grains read the input from a longer line: a slice and Scatter (1 s), and a grain at half
    // speed falling behind by half its length (0.25 s)
    histMax = (kSliceSpan + kScatterSpan + 0.5 * kMaxGrainMs * 0.001) * sr + 8.0;
    int hsize = 1;
    while (hsize < (int)histMax + 16)
        hsize <<= 1;
    hist.assign ((size_t)hsize, 0.0f);
    histMask = hsize - 1;
    gMixStep = (float)(1.0 / (0.03 * sr));
    dry.prepare (baseDelay);
    smooth = 1.0 - std::exp (-(double)kStep / (0.05 * sr));
    Lcg rng {0x2545F491u};
    for (auto& o : orbList)
    {
        o.tiltU = rng.next ();
        o.sizeU = std::fabs (rng.next ());
        o.speedU = rng.next ();
        o.placeU = rng.next ();
        for (auto& a : o.rateU)
            for (auto& v : a)
                v = rng.next ();
        o.weight[0] = 2.0 / 3.0;
        o.weight[1] = 1.0 / 3.0;
    }
    reset ();
}

void Motion::reset ()
{
    std::fill (buf.begin (), buf.end (), 0.0f);
    std::fill (hist.begin (), hist.end (), 0.0f);
    writePos = 0;
    histPos = 0;
    stepLeft = 0;
    dry.reset ();
    dSm = distance;
    rSm = radius;
    Lcg rng {0x9E3779B9u};
    for (int k = 0; k < kMaxOrbs; ++k)
    {
        auto& o = orbList[(size_t)k];
        o.angle = kTwoPi * k / std::max (1, numOrbs) + randomness * 0.6 * o.placeU;
        for (auto& a : o.phase)
            for (auto& v : a)
                v = kTwoPi * 0.5 * (rng.next () + 1.0);
    }
    for (auto& t : taps)
        t.fill ({});
    started = false;
    updateRates ();
    std::fill (orbBuf.begin (), orbBuf.end (), 0.0f);
    grainPath = grains;
    gMix = grains ? 1.0f : 0.0f;
    quiet = 0;
    for (int k = 0; k < kMaxOrbs; ++k)
        grainOrbs[(size_t)k].rng = 0x51ED270Bu + 0x9E3779B9u * (uint32_t)k;
    startGrains ();
}

void Motion::setOrbs (int n) { numOrbs = std::clamp (n, 1, kMaxOrbs); }
void Motion::setPattern (int p) { pattern = p == kOrbit ? kOrbit : kSwarm; }
void Motion::setSpeed (double v) { speed = std::clamp (v, 0.0, 100.0); }
void Motion::setRandomness (double r) { randomness = std::clamp (r, 0.0, 1.0); }

void Motion::updateRates ()
{
    for (auto& o : orbList)
    {
        const double rk = rSm * (1.0 - 0.3 * randomness * o.sizeU);
        o.angleRate = speed * (1.0 + 0.35 * randomness * o.speedU) / std::max (0.01, rk);
        // the swarm: the rates of the six sines, scaled so the RMS speed is Speed
        double ms = 0.0, w[3][2];
        for (int a = 0; a < 3; ++a)
            for (int j = 0; j < 2; ++j)
            {
                w[a][j] = kAxisRate[a][j] * (1.0 + 0.5 * randomness * o.rateU[a][j]);
                const double amp = rSm * kAxisNorm * kAxis[a] * o.weight[j] * w[a][j];
                ms += 0.5 * amp * amp;
            }
        const double omega = ms > 0.0 ? speed / std::sqrt (ms) : 0.0;
        for (int a = 0; a < 3; ++a)
            for (int j = 0; j < 2; ++j)
                o.phaseRate[a][j] = w[a][j] * omega;
    }
}

Motion::Vec Motion::position (int k, double ago) const
{
    const auto& o = orbList[(size_t)k];
    Vec p;
    if (pattern == kOrbit)
    {
        const double rk = rSm * (1.0 - 0.3 * randomness * o.sizeU);
        const double th = o.angle - o.angleRate * ago, tilt = 1.2 * randomness * o.tiltU;
        const double cx = rk * std::cos (th), cy = rk * std::sin (th);
        p.x = cx;
        p.y = cy * std::cos (tilt);
        p.z = cy * std::sin (tilt);
    }
    else
    {
        double v[3];
        for (int a = 0; a < 3; ++a)
        {
            double s = 0.0;
            for (int j = 0; j < 2; ++j)
                s += o.weight[j] * std::sin (o.phase[a][j] - o.phaseRate[a][j] * ago);
            v[a] = rSm * kAxisNorm * kAxis[a] * s;
        }
        p.x = v[0];
        p.y = v[1];
        p.z = v[2];
    }
    p.y += dSm;
    return p;
}

void Motion::retarget ()
{
    dSm += (distance - dSm) * smooth;
    rSm += (radius - rSm) * smooth;
    // a tap whose gain ramped to nothing stops
    for (auto& t : taps)
        for (auto& tap : t)
            if (std::fabs (tap.gain) < 1e-7f)
                tap.gain = 0.0f;
    updateRates ();
    const double c = kSoundSpeed, ref = dSm / c, base = kBaseMs * 0.001;
    const double maxDelay = (double)(mask + 1) - kStep - 8.0;
    const double norm = 1.0 / std::sqrt ((double)numOrbs);
    const double ear = kEarHalf * spread;
    for (int k = 0; k < kMaxOrbs; ++k)
    {
        auto& t = taps[(size_t)k];
        const bool active = k < numOrbs;
        // the pan, from where the orb is now
        const Vec now = position (k, ref);
        const double h = std::sqrt (now.x * now.x + now.y * now.y);
        const double p = (h > 1e-6 ? now.x / h : 0.0) * spread, th = (p + 1.0) * 0.25 * dsp::kPi;
        const double pan[2] = {std::sqrt (2.0) * std::cos (th), std::sqrt (2.0) * std::sin (th)};
        for (int img = 0; img < 2; ++img)
            for (int e = 0; e < 2; ++e)
            {
                Tap& tap = t[(size_t)(img * 2 + e)];
                double target = (double)baseDelay, gain = 0.0;
                if (active && (img == 0 || floor))
                {
                    const double ex = e == 0 ? -ear : ear;
                    double d = ref, dist = dSm;
                    for (int it = 0; it < 3; ++it)
                    {
                        // (the first step starts from ref for every tap: where the orb is now)
                        const Vec q = it == 0 ? now : position (k, d);
                        const double dz = img == 0 ? q.z : -2.0 * kEarHeight - q.z;
                        dist = std::sqrt ((q.x - ex) * (q.x - ex) + q.y * q.y + dz * dz);
                        d = dist / c;
                    }
                    target = std::clamp ((base + d - ref) * sr, 2.0, maxDelay);
                    gain = std::min (4.0, dSm / std::max (dist, 0.3)) * pan[e] * norm * (img == 0 ? 1.0 : kFloorGain);
                }
                if (!started)
                {
                    tap.delay = (float)target;
                    tap.gain = (float)gain;
                }
                tap.delayStep = (float)((target - tap.delay) / kStep);
                tap.gainStep = (float)((gain - tap.gain) / kStep);
            }
    }
    started = true;
    // move on by one step
    const double dt = (double)kStep / sr;
    for (auto& o : orbList)
    {
        o.angle = std::fmod (o.angle + o.angleRate * dt, kTwoPi);
        for (int a = 0; a < 3; ++a)
            for (int j = 0; j < 2; ++j)
                o.phase[a][j] = std::fmod (o.phase[a][j] + o.phaseRate[a][j] * dt, kTwoPi);
    }
}

void Motion::process (float* l, float* r, int n)
{
    if (grains && !grainPath)
    {
        // Grains on: each orb's line starts as a copy of the input's (what its taps read now), then the
        // grains fade in
        const int size = mask + 1;
        for (int k = 0; k < kMaxOrbs; ++k)
        {
            float* o = orbBuf.data () + (size_t)k * (size_t)size;
            for (int i = 0; i < size; ++i)
                o[(writePos - i) & mask] = buf[(size_t)((writePos - i) & mask)];
        }
        gMix = 0.0f;
        quiet = 0;
        startGrains ();
        grainPath = true;
    }
    else if (!grains && grainPath && quiet > mask)
        grainPath = false; // faded out, and the orbs' lines hold nothing but the input any more: back to the input's
    if (grainPath)
        processGrains (l, r, n);
    else
        processPlain (l, r, n);
}

float Motion::grainLevel (int k) const
{
    if (!grainPath || k < 0 || k >= kMaxOrbs)
        return 0.0f;
    const auto& go = grainOrbs[(size_t)k];
    return go.newest >= 0 && go.g[(size_t)go.newest].on ? hann (go.g[(size_t)go.newest].phase) * gMix : 0.0f;
}

void Motion::startGrains ()
{
    // each orb's first grain a share of the gap between grains after the one before
    const double gap = grainMs * 0.001 * sr / density;
    for (int k = 0; k < kMaxOrbs; ++k)
    {
        auto& go = grainOrbs[(size_t)k];
        for (auto& g : go.g)
            g.on = false;
        go.newest = -1;
        go.wait = 1.0 + gap * k / std::max (1, numOrbs);
    }
}

void Motion::startGrain (int k)
{
    auto& go = grainOrbs[(size_t)k];
    Lcg rng {go.rng};
    const double u = 0.5 * (rng.next () + 1.0), v = rng.next ();
    go.rng = rng.s;
    const double len = std::max (16.0, grainMs * 0.001 * sr);
    const double speed = std::pow (2.0, grainPitch / 12.0);
    go.wait += len / density * (1.0 + 0.5 * scatter * v);
    if (go.wait < 1.0)
        go.wait = 1.0;
    int slot = -1;
    for (int i = 0; i < kMaxGrains; ++i)
        if (!go.g[(size_t)i].on)
        {
            slot = i;
            break;
        }
    if (slot < 0)
        return; // (all busy: this one is dropped)
    Grain& g = go.g[(size_t)slot];
    g.drift = 1.0 - speed;
    // where it starts reading: the orb's slice, scattered further back; a grain faster than the input
    // starts far enough back not to catch up with it (Hermite reads two samples ahead)
    double back = (kSliceSpan * k / std::max (1, numOrbs) + kScatterSpan * scatter * u) * sr;
    back = std::max (back, 3.0 - std::min (0.0, g.drift) * len);
    back = std::min (back, histMax - std::max (0.0, g.drift) * len);
    g.back = back;
    g.phase = 0.0;
    g.phaseStep = 1.0 / len;
    g.amp = density <= 2.0 ? 1.0f : (float)std::sqrt (2.0 / density);
    g.on = true;
    go.newest = slot;
}

inline float Motion::cloud (int k)
{
    auto& go = grainOrbs[(size_t)k];
    go.wait -= 1.0;
    if (go.wait <= 0.0)
        startGrain (k);
    const float* h = hist.data ();
    const int hm = histMask;
    float y = 0.0f;
    for (auto& g : go.g)
    {
        if (!g.on)
            continue;
        const double pos = (double)histPos - g.back;
        const double fl = std::floor (pos);
        const int i0 = (int)fl;
        const float fr = (float)(pos - fl);
        y += dsp::hermite (h[(i0 - 1) & hm], h[i0 & hm], h[(i0 + 1) & hm], h[(i0 + 2) & hm], fr) * hann (g.phase) * g.amp;
        g.phase += g.phaseStep;
        g.back += g.drift;
        if (g.phase >= 1.0)
            g.on = false;
    }
    return y;
}

void Motion::processGrains (float* l, float* r, int n)
{
    const float m = mix, target = grains ? 1.0f : 0.0f;
    float* b = buf.data ();
    const int size = mask + 1;
    for (int i = 0; i < n; ++i)
    {
        if (stepLeft == 0)
        {
            retarget ();
            stepLeft = kStep;
        }
        --stepLeft;
        const float x = 0.5f * (l[i] + r[i]);
        b[writePos] = x;
        hist[(size_t)histPos] = x;
        // the crossfade between the input and the grains in the orbs' lines
        if (gMix != target)
            gMix = target > gMix ? std::min (target, gMix + gMixStep) : std::max (target, gMix - gMixStep);
        quiet = gMix == 0.0f ? std::min (quiet + 1, 1 << 30) : 0;
        const int w = writePos;
        const float dryPart = (1.0f - gMix) * x;
        for (int k = 0; k < kMaxOrbs; ++k)
        {
            float s = dryPart;
            if (k < numOrbs && gMix > 0.0f)
                s += gMix * cloud (k);
            orbBuf[(size_t)k * (size_t)size + (size_t)w] = s;
        }
        float wl = 0.0f, wr = 0.0f;
        for (int k = 0; k < kMaxOrbs; ++k)
        {
            auto& t = taps[(size_t)k];
            const float* o = orbBuf.data () + (size_t)k * (size_t)size;
            for (int j = 0; j < 4; ++j)
            {
                Tap& tap = t[(size_t)j];
                if (tap.gain == 0.0f && tap.gainStep == 0.0f)
                    continue;
                const float pos = (float)w - tap.delay;
                int i0 = (int)pos;
                if ((float)i0 > pos)
                    --i0;
                const float fr = pos - (float)i0;
                const float y = dsp::hermite (o[(i0 - 1) & mask], o[i0 & mask], o[(i0 + 1) & mask], o[(i0 + 2) & mask], fr) * tap.gain;
                if (j & 1)
                    wr += y;
                else
                    wl += y;
                tap.delay += tap.delayStep;
                tap.gain += tap.gainStep;
            }
        }
        writePos = (writePos + 1) & mask;
        histPos = (histPos + 1) & histMask;
        float dl = l[i], dr = r[i];
        dry.tick (dl, dr);
        l[i] = dl + (wl - dl) * m;
        r[i] = dr + (wr - dr) * m;
    }
}

void Motion::processPlain (float* l, float* r, int n)
{
    const float m = mix;
    float* b = buf.data ();
    for (int i = 0; i < n; ++i)
    {
        if (stepLeft == 0)
        {
            retarget ();
            stepLeft = kStep;
        }
        --stepLeft;
        b[writePos] = 0.5f * (l[i] + r[i]);
        hist[(size_t)histPos] = b[writePos]; // (for the grains, should they start)
        float wl = 0.0f, wr = 0.0f;
        for (int k = 0; k < kMaxOrbs; ++k)
        {
            auto& t = taps[(size_t)k];
            for (int j = 0; j < 4; ++j)
            {
                Tap& tap = t[(size_t)j];
                if (tap.gain == 0.0f && tap.gainStep == 0.0f)
                    continue;
                const float pos = (float)writePos - tap.delay;
                int i0 = (int)pos; // floor (without the library call)
                if ((float)i0 > pos)
                    --i0;
                const float fr = pos - (float)i0;
                const float y = dsp::hermite (b[(i0 - 1) & mask], b[i0 & mask], b[(i0 + 1) & mask], b[(i0 + 2) & mask], fr) * tap.gain;
                if (j & 1)
                    wr += y;
                else
                    wl += y;
                tap.delay += tap.delayStep;
                tap.gain += tap.gainStep;
            }
        }
        writePos = (writePos + 1) & mask;
        histPos = (histPos + 1) & histMask;
        float dl = l[i], dr = r[i];
        dry.tick (dl, dr);
        l[i] = dl + (wl - dl) * m;
        r[i] = dr + (wr - dr) * m;
    }
}

} // namespace orbitr
