#include "Tape.h"

#include <cmath>

namespace detonatr {

namespace {
// the moving average of L samples: its gain at hz
double boxGain (int L, double hz, double sr)
{
    const double w = dsp::kPi * hz / sr;
    const double s = std::sin (w);
    return std::fabs (s) < 1e-12 ? 1.0 : std::fabs (std::sin (w * L) / (L * s));
}
} // namespace

int Tape::lengthFor (double hz, double sr)
{
    // the lows (2 B - B B, B four boxes) are 6 dB down where B = 1 - 1/sqrt 2
    const double want = std::pow (1.0 - std::sqrt (0.5), 0.25);
    int best = 2;
    double bestErr = 1e9;
    for (int L = 2; L < 100000; ++L)
    {
        if (L * hz > 0.9 * sr)
            break;
        const double err = std::fabs (boxGain (L, hz, sr) - want);
        if (err < bestErr)
        {
            bestErr = err;
            best = L;
        }
    }
    return best;
}

double Tape::lowResponse (double hz) const
{
    const double b = std::pow (boxGain (len, hz, sr), 4.0);
    return 2.0 * b - b * b;
}

void Tape::prepare (double sampleRate, int block)
{
    sr = sampleRate;
    maxBlock = std::max (1, block);
    maxLen = lengthFor (kMinSplit, sr);
    for (int b = 0; b < kBands; ++b)
        for (int c = 0; c < 2; ++c)
        {
            over[b][c].prepare (sr, maxBlock);
            bandBuf[b][c].assign ((size_t)maxBlock, 0.0f);
        }
    up.assign ((size_t)maxBlock * 4, 0.0f);
    driveBuf.assign ((size_t)maxBlock, 0.0f);
    envC = dsp::coef (20.0, sr);
    warmC = (float)(1.0 - std::exp (-2.0 * dsp::kPi * kWarmHz / (4.0 * sr)));
    dcR = (float)(1.0 - 2.0 * dsp::kPi * 5.0 / (4.0 * sr)); // at the 4x rate
    const double tb = std::tanh (kBias);
    biasOut = (float)tb;
    biasSlope = (float)(1.0 / (1.0 - tb * tb));
    for (int b = 0; b < kBands; ++b)
        updateComp (b);
    dirty = true;
    reset ();
}

void Tape::configure ()
{
    len = std::clamp (lengthFor (split, sr), 2, maxLen);
    for (int c = 0; c < 2; ++c)
    {
        for (auto& b : boxes[c])
            b.prepare (len);
        pad[c].prepare (4 * (maxLen - len));
        mid[c].prepare (2 * (len - 1));
        highDelay[c].prepare (4 * (len - 1));
    }
    dirty = false;
}

void Tape::reset ()
{
    configure ();
    for (auto& b : bands)
    {
        b.env = 0.0f;
        b.warm[0] = b.warm[1] = 0.0f;
        b.dcX[0] = b.dcX[1] = b.dcY[0] = b.dcY[1] = 0.0f;
    }
    for (auto& o : over)
        for (auto& x : o)
            x.reset ();
}

void Tape::setSplit (double hz)
{
    hz = std::clamp (hz, kMinSplit, kMaxSplit);
    if (hz != split)
    {
        split = hz;
        if (lengthFor (split, sr) != len)
            dirty = true;
    }
}

void Tape::setDriveDb (int band, double db)
{
    bands[(size_t)band].drive = (float)std::pow (10.0, std::clamp (db, 0.0, 48.0) / 20.0);
    updateComp (band);
}

float Tape::shape (const Band&, float x, float drive) const
{
    // tanh with a bias: its offset and its small-signal gain taken out
    return (std::tanh (drive * x + (float)kBias) - biasOut) * biasSlope;
}

void Tape::updateComp (int band)
{
    // what keeps a -18 dBFS sine (peak 0.177) as loud: the input's RMS over the curve's
    Band& b = bands[(size_t)band];
    const double tb = std::tanh (kBias), slope = 1.0 / (1.0 - tb * tb);
    double in = 0.0, out = 0.0, mean = 0.0;
    constexpr int N = 256;
    std::array<double, N> y {};
    for (int i = 0; i < N; ++i)
    {
        const double x = 0.177 * std::sin (2.0 * dsp::kPi * (i + 0.5) / N);
        y[(size_t)i] = (std::tanh (b.drive * x + kBias) - tb) * slope;
        mean += y[(size_t)i] / N;
        in += x * x;
    }
    for (double v : y)
        out += (v - mean) * (v - mean);
    b.comp = (float)std::sqrt (in / std::max (1e-20, out));
}

float Tape::curve (int band, float x) const
{
    const Band& b = bands[(size_t)band];
    return shape (b, x, b.drive) * b.comp;
}

void Tape::process (float* l, float* r, int n)
{
    if (dirty)
        configure ();
    float* io[2] = {l, r};
    for (int start = 0; start < n; start += maxBlock)
    {
        const int m = std::min (maxBlock, n - start);
        // the split
        for (int c = 0; c < 2; ++c)
        {
            float* lo = bandBuf[0][c].data ();
            float* hi = bandBuf[1][c].data ();
            for (int i = 0; i < m; ++i)
            {
                const float x = pad[c].push (io[c][start + i]);
                double y1 = x;
                for (int k = 0; k < 4; ++k)
                    y1 = boxes[c][k].push (y1);
                double y2 = y1;
                for (int k = 4; k < 8; ++k)
                    y2 = boxes[c][k].push (y2);
                const float lows = (float)(2.0 * mid[c].push ((float)y1) - y2);
                lo[i] = lows;
                hi[i] = highDelay[c].push (x) - lows;
            }
        }
        // each band, saturated at 4x
        for (int b = 0; b < kBands; ++b)
        {
            Band& bd = bands[(size_t)b];
            const float* bl = bandBuf[b][0].data ();
            const float* br = bandBuf[b][1].data ();
            // the drive for each sample: Drive, moved by Dynamics with the band's level
            float* drv = driveBuf.data ();
            const int mm = m;
            for (int i = 0; i < mm; ++i)
            {
                const float p = std::max (bl[i] * bl[i], br[i] * br[i]);
                bd.env += (p - bd.env) * envC;
                float g = bd.drive;
                if (bd.dyn != 0.0f)
                    g *= dsp::dbToAmp (0.5f * bd.dyn * std::clamp (dsp::powToDb (2.0f * bd.env + 1e-20f) + 18.0f, -24.0f, 24.0f));
                drv[i] = g;
            }
            for (int c = 0; c < 2; ++c)
            {
                float* x = bandBuf[b][c].data ();
                over[b][c].up (x, up.data (), mm);
                float w = bd.warm[c], hx = bd.dcX[c], hy = bd.dcY[c];
                for (int i = 0; i < 4 * mm; ++i)
                {
                    const float d = up[(size_t)i];
                    const float s = shape (bd, d, drv[i >> 2]) * bd.comp;
                    w += (s - w) * warmC;
                    // what the saturation adds, without its DC (a 5 Hz high-pass)
                    const float diff = w - d;
                    hy = diff - hx + dcR * hy;
                    hx = diff;
                    up[(size_t)i] = d + hy * bd.mix;
                }
                bd.warm[c] = w;
                bd.dcX[c] = hx;
                bd.dcY[c] = hy;
                over[b][c].down (up.data (), x, mm);
            }
        }
        // the sum, levelled
        for (int c = 0; c < 2; ++c)
        {
            const float* lo = bandBuf[0][c].data ();
            const float* hi = bandBuf[1][c].data ();
            float* o = io[c] + start;
            for (int i = 0; i < m; ++i)
                o[i] = lo[i] * bands[0].level + hi[i] * bands[1].level;
        }
    }
}

} // namespace detonatr
