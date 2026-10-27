#include "Engine.h"

#include "CompShape.h"
#include "DetectorShape.h"
#include "StageShape.h"

#include "pluginkit/EndClip.h"
#include "pluginkit/NoDenormals.h"

#include <algorithm>
#include <cmath>

namespace sunder {

namespace dsp = sweepr::dsp;

namespace {
// the germanium transistor's open-loop curve: asymmetric (the positive side saturates first), slope 1 at rest
constexpr double kGermBias = 0.35;
const double kGermT = std::tanh (kGermBias), kGermNorm = 1.0 / (1.0 - kGermT * kGermT);
inline double germ (double x) { return (std::tanh (x + kGermBias) - kGermT) * kGermNorm; }
inline double germSlope (double x)
{
    const double t = std::tanh (x + kGermBias);
    return (1.0 - t * t) * kGermNorm;
}
// the loop's solution: germ (x) for x + loop germ (x) = (1 + loop) v, v the linear stage's output over the headroom
inline double germLoop (double v, double loop, double& x)
{
    const double r = (1.0 + loop) * v;
    for (int k = 0; k < 8; ++k)
        x -= (x + loop * germ (x) - r) / (1.0 + loop * germSlope (x));
    return germ (x);
}
// the level u (dB) a follower level w stands for: kCalW read backwards (linear between its points)
inline double levelOf (double w)
{
    using namespace shape;
    if (w <= kCalW[0])
        return kCalMinDb - 30.0 * (1.0 - std::max (0.0, w) / kCalW[0]); // (below the table: well under any knee)
    if (w >= kCalW[kCalPoints - 1])
        return kCalMinDb + (kCalPoints - 1) + 20.0 * std::log10 (w / kCalW[kCalPoints - 1]);
    int lo = 0, hi = kCalPoints - 1;
    while (hi - lo > 1)
    {
        const int mid = (lo + hi) / 2;
        (kCalW[mid] <= w ? lo : hi) = mid;
    }
    return kCalMinDb + lo + (w - kCalW[lo]) / (kCalW[hi] - kCalW[lo]);
}
// the output stage's positions (pinchr's 0 .. 10 measured, extrapolated to 15): above Drive 10 the top step's
// response, louder by kExtraDriveDb a position; above Feedback 10 the frequencies and Qs held to the table's span
constexpr double kMaxPosition = 15.0;
constexpr double kExtraDriveDb = 2.0;
constexpr double kExtraMinHz = 3.0, kExtraMaxHz = 40000.0, kExtraMinQ = 0.1, kExtraMaxQ = 2.5;
inline double coefFor (double sec, double sr) { return 1.0 - std::exp (-1.0 / (std::max (1e-6, sec) * sr)); }
// glides x towards t by c, landing on it once close (so a still setting is exactly its value)
inline void glide (double& x, double t, double c, double eps = 1e-9)
{
    x += (t - x) * c;
    if (std::fabs (t - x) < eps)
        x = t;
}
inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
// a time from an 11-point table (positions 0 .. 10), log-interpolated
inline double tableTime (const double* t, double pos)
{
    pos = std::clamp (pos, 0.0, 10.0);
    const int i = std::min (9, (int)pos);
    const double f = pos - i;
    return std::exp (std::log (t[i]) * (1.0 - f) + std::log (t[i + 1]) * f);
}
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

double staticGrDb (int curve, double ratio, double u)
{
    using namespace shape;
    curve = std::clamp (curve, 0, kCurves - 1);
    const double pos = std::clamp (ratio - 1.0, 0.0, (double)(kRatios - 1));
    const int r0 = std::min (kRatios - 2, (int)pos);
    const double rf = pos - r0;
    const double x = std::clamp ((u - kLevelMin) / kLevelStep, 0.0, (double)(kLevels - 1));
    const int l0 = std::min (kLevels - 2, (int)x);
    const double lf = x - l0;
    const float* a = kGr[curve][r0];
    const float* b = kGr[curve][r0 + 1];
    const double ga = a[l0] + (a[l0 + 1] - a[l0]) * lf, gb = b[l0] + (b[l0 + 1] - b[l0]) * lf;
    return ga + (gb - ga) * rf;
}

StageParams stageAt (double drive, double feedback)
{
    using namespace shape;
    const double d = std::clamp (drive, 0.0, kMaxPosition), f = std::clamp (feedback, 0.0, kMaxPosition);
    // gains in dB, frequencies and Qs in logs, bilinearly between the four neighbours (the measured 0 .. 10)
    auto pick = [] (const Stage& s, int k) -> double {
        const float v[11] = {s.gainDb, s.fa, s.qa, s.fb, s.qb, s.fc, s.qc, s.fz, s.qz, s.fh, s.qh};
        return k == 0 ? (double)v[0] : std::log ((double)v[k]);
    };
    // one Drive row at Feedback f: above 10, on along the row's slope from 7 to 10 (the frequencies and Qs held
    // to the range the table spans, so no section runs away)
    auto row = [&] (int di, int k) -> double {
        if (f <= 10.0)
        {
            const int f0 = std::min (9, (int)f);
            const double fw = f - f0;
            return pick (kStage[di][f0], k) * (1.0 - fw) + pick (kStage[di][f0 + 1], k) * fw;
        }
        const double top = pick (kStage[di][10], k), slope = (top - pick (kStage[di][7], k)) / 3.0;
        const double v = top + slope * (f - 10.0);
        return k == 0 ? v : std::clamp (v, std::log (k % 2 == 1 ? kExtraMinHz : kExtraMinQ), std::log (k % 2 == 1 ? kExtraMaxHz : kExtraMaxQ));
    };
    double out[11];
    const double dIn = std::min (d, 10.0);
    const int d0 = std::min (9, (int)dIn);
    const double dw = dIn - d0;
    for (int k = 0; k < 11; ++k)
    {
        const double v = row (d0, k) * (1.0 - dw) + row (d0 + 1, k) * dw;
        out[k] = k == 0 ? v : std::exp (v);
    }
    // above Drive 10: the top step's response, louder by kExtraDriveDb a position (the measured steps' gain)
    if (d > 10.0)
        out[0] += kExtraDriveDb * (d - 10.0);
    return {out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7], out[8], out[9], out[10]};
}

std::complex<double> stageResponse (const StageParams& s, double hz)
{
    constexpr double kTwoPi = 2.0 * dsp::kPi;
    const std::complex<double> j (0.0, kTwoPi * hz);
    auto quad = [&] (double f, double q) { const double w = kTwoPi * f; return j * j + j * (w / q) + w * w; };
    const double wh = kTwoPi * s.fh;
    std::complex<double> h = dbToGain (s.gainDb) * j * j * j * j * quad (s.fz, s.qz) / (quad (s.fa, s.qa) * quad (s.fb, s.qb) * quad (s.fc, s.qc));
    return h * (wh * wh) / quad (s.fh, s.qh);
}

Engine::Engine () {}

void Engine::prepare (double sampleRate, int /*maxBlock*/) // (it works in chunks of kChunk)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    srOs = sr * kOversample;
    for (auto& o : os)
        o.prepare (sr, kChunk);
    for (auto& o : scOs)
        o.prepare (sr, kChunk);
    osLatency = os[0].latency ();
    for (int c = 0; c < 2; ++c)
    {
        up[c].assign ((size_t)(kChunk * kOversample), 0.0f);
        scUp[c].assign ((size_t)(kChunk * kOversample), 0.0f);
        wet[c].assign ((size_t)kChunk, 0.0f);
    }
    cSmooth = coefFor (0.02, sr);
    hp1G = dsp::onePoleG (shape::kHp1Hz, srOs);
    hp2G = dsp::onePoleG (shape::kHp2Hz, srOs);
    satDcG = dsp::onePoleG (5.0, srOs);
    kSm = coefFor (shape::kSmoothSec, srOs);
    bass.prepare (sr, kChunk);

    for (uint32_t id = 0; id < kNumParams; ++id)
        setParam (id, p[id]);
    reset ();
}

void Engine::reset ()
{
    for (auto& o : os)
        o.reset ();
    for (auto& o : scOs)
        o.reset ();
    for (int c = 0; c < 2; ++c)
    {
        hp1[c].reset ();
        hp2[c].reset ();
        scLp[c].reset ();
        peak[c] = smooth[c] = 0.0;
        grNow[c] = 0.0;
        for (auto& s : svf)
            s[c].reset ();
        satDc[c].reset ();
    }
    levelNow = -120.0;
    inGain = dbToGain (p[kInput]);
    scGain = dbToGain (p[kScGain]);
    ratio = ratioT;
    attack = p[kAttack];
    release = p[kRelease];
    mix = std::clamp (p[kMix], 0.0, 1.0);
    drive = driveT;
    feedback = feedbackT;
    bass.reset ();
    bassActive = false;
    satAmt = std::clamp (p[kSaturation], 0.0, 1.0);
    out = (float)dbToGain (p[kOutput]);
    shownDrive = shownFeedback = -1.0;
    controlChunk ();
}

void Engine::setParam (uint32_t id, double plain)
{
    if (id >= kNumParams)
        return;
    p[id] = plain;
    // (read where they are used; the old end saturator's and Dirty are retired)
    if (id == kRatio)
        ratioT = std::clamp (plain, 1.0, (double)kSteps);
    else if (id == kDrive)
        driveT = stagePosition (std::clamp (plain, 1.0, (double)kSteps));
    else if (id == kFeedback)
        feedbackT = stagePosition (std::clamp (plain, 1.0, (double)kSteps));
    else if (id >= kBass)
        bassParam (id);
}

// A bass enhancer parameter to its engine: Bass is its Dry/Wet from -100 % (0) to 0 % (100 %), the others are
// bubr's own.
void Engine::bassParam (uint32_t id)
{
    if (id == kBass)
        bass.setParam (bass::kDryWet, std::clamp (p[kBass], 0.0, 1.0) - 1.0);
    else if (id == kBassOversample)
        bass.setParam (bass::kOversample, p[id]);
    else if (id < kBassOversample)
        bass.setParam (bass::kBoost + (id - kBassBoost), p[id]);
}

void Engine::setModelPositions (double ratioPos, double drivePos, double feedbackPos)
{
    ratioT = ratioPos;
    driveT = drivePos;
    feedbackT = feedbackPos;
}

// The output stage's sections for the drive and feedback now (only when they moved).
void Engine::stageCoefs ()
{
    if (std::fabs (drive - shownDrive) < 1e-6 && std::fabs (feedback - shownFeedback) < 1e-6)
        return;
    shownDrive = drive;
    shownFeedback = feedback;
    const StageParams s = stageAt (drive, feedback);
    auto set = [this] (Section& x, double f, double q) {
        x.c.k = 1.0 / std::max (0.05, q);
        x.c.set (std::tan (dsp::kPi * std::min (f, 0.45 * srOs) / srOs));
    };
    // two resonant high-passes, a section with the free zeros, the high peak (a low-pass)
    set (sec[0], s.fa, s.qa);
    sec[0].mHp = 1.0, sec[0].mBp = 0.0, sec[0].mLp = 0.0;
    set (sec[1], s.fb, s.qb);
    sec[1].mHp = 1.0, sec[1].mBp = 0.0, sec[1].mLp = 0.0;
    set (sec[2], s.fc, s.qc);
    const double rz = s.fz / s.fc;
    sec[2].mHp = 1.0, sec[2].mBp = rz / s.qz, sec[2].mLp = rz * rz;
    set (sec[3], s.fh, s.qh);
    sec[3].mHp = 0.0, sec[3].mBp = 0.0, sec[3].mLp = 1.0;
    stageGain = dbToGain (s.gainDb);
}

// Once a chunk: the timing, the sidechain filter and the output stage.
void Engine::controlChunk ()
{
    const double c = std::min (1.0, cSmooth * kChunk);
    glide (ratio, ratioT, c, 1e-6);
    glide (attack, p[kAttack], c, 1e-6);
    glide (release, p[kRelease], c, 1e-6);
    glide (drive, driveT, c, 1e-6);
    glide (feedback, feedbackT, c, 1e-6);
    glide (satAmt, std::clamp (p[kSaturation], 0.0, 1.0), c, 1e-6);
    // the loop's gain: 30 at Feedback 0 down to about 1 at 10 (0.17 at 15: little feedback, warm early); the
    // headroom: +6 dBFS at 0 % .. -12 dBFS at 100 %
    satLoop = 30.0 * std::pow (10.0, -0.15 * feedback);
    satHead = dbToGain (6.0 - 18.0 * satAmt);
    // the loop has no memory: its curve, tabulated when its gain moves, is read per sample
    if (std::fabs (satLoop - satTabLoop) > 1e-9)
    {
        satTabLoop = satLoop;
        double x = 0.0;
        for (int i = kSatHalf; i <= 2 * kSatHalf; ++i)
            satTab[i] = germLoop ((i - kSatHalf) * kSatStep, satLoop, x);
        x = 0.0;
        for (int i = kSatHalf; i >= 0; --i)
            satTab[i] = germLoop ((i - kSatHalf) * kSatStep, satLoop, x);
    }
    kAtk = coefFor (tableTime (shape::kAttackSec, attack), srOs);
    kRel = coefFor (tableTime (shape::kReleaseSec, release), srOs);
    const int sf = std::clamp ((int)std::lround (p[kScFilter]), 0, kNumScFilters - 1);
    if (sf > 0)
    {
        scPoleW = shape::kScPoleHz[sf - 1];
        scZeroW = shape::kScZeroHz[sf - 1];
        scPoleG = dsp::onePoleG (scPoleW, srOs);
    }
    else
        scPoleG = 0.0;
    stageCoefs ();
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n, const float* sl, const float* sr2)
{
    const pk::NoDenormals guard;
    const double inT = dbToGain (p[kInput]);
    const double scT = dbToGain (p[kScGain]);
    const bool ext = p[kExtSc] >= 0.5 && sl != nullptr;
    const float outT = (float)dbToGain (p[kOutput]);
    const float smoothOut = (float)cSmooth;
    const bool midSide = p[kMode] >= 0.5;
    const bool link = p[kLink] >= 0.5;
    const int curve = std::clamp ((int)std::lround (p[kCurve]), 0, kNumCurves - 1);
    const double mixT = std::clamp (p[kMix], 0.0, 1.0);
    const double restDb = shape::kRestDb;
    const double ln10over20 = dsp::kLn10 / 20.0;

    // the bass enhancer: while Bass is up, or gliding down to 0; at 0 only its dry path's delay (Bass
    // Oversample), or nothing at all
    const bool bassOn = p[kBass] > 0.0 || (bassActive && !bass.dryOnly ());
    bassActive = bassOn;
    const bool bassDelay = !bassOn && bass.latency () > 0;

    for (int a = 0; a < n; a += kChunk)
    {
        const int m = std::min (kChunk, n - a);
        controlChunk ();
        float bl[kChunk], br[kChunk];
        const float* inL = xl + a;
        const float* inR = xr + a;
        if (bassOn || bassDelay)
        {
            if (bassOn)
                bass.process (inL, inR, bl, br, m);
            else
                bass.processDry (inL, inR, bl, br, m);
            inL = bl;
            inR = br;
        }
        const bool scOn = scPoleG > 0.0;
        const double scShelf = scOn ? scZeroW / scPoleW : 1.0;

        // the base rate: Mid/Side and Input (and the sidechain input's, for Ext SC)
        float in[2][kChunk], sc[2][kChunk];
        double mixAt[kChunk];
        for (int i = 0; i < m; ++i)
        {
            glide (inGain, inT, cSmooth);
            glide (scGain, scT, cSmooth);
            glide (mix, mixT, cSmooth);
            mixAt[i] = mix;
            double l = inL[i], r = inR[i];
            double s0 = ext ? sl[a + i] : 0.0, s1 = ext ? (sr2 ? sr2[a + i] : sl[a + i]) : 0.0;
            if (midSide)
            {
                const double mid = 0.5 * (l + r), side = 0.5 * (l - r);
                l = mid;
                r = side;
                const double smid = 0.5 * (s0 + s1), sside = 0.5 * (s0 - s1);
                s0 = smid;
                s1 = sside;
            }
            in[0][i] = (float)(l * inGain);
            in[1][i] = (float)(r * inGain);
            sc[0][i] = (float)(s0 * scGain);
            sc[1][i] = (float)(s1 * scGain);
        }

        // 4x: the compressor, Mix and the output stage
        os[0].up (in[0], up[0].data (), m);
        os[1].up (in[1], up[1].data (), m);
        if (ext)
        {
            scOs[0].up (sc[0], scUp[0].data (), m);
            scOs[1].up (sc[1], scUp[1].data (), m);
        }
        for (int j = 0; j < m * kOversample; ++j)
        {
            const double mx = mixAt[j / kOversample];
            double x[2] = {up[0][(size_t)j], up[1][(size_t)j]};
            // the detector: the high-passes, SC Filter's shelf, the rectifier
            double rect[2];
            for (int c = 0; c < 2; ++c)
            {
                const double d = ext ? (double)scUp[c][(size_t)j] : x[c];
                double h = d - hp1[c].lp (d, hp1G);
                h -= hp2[c].lp (h, hp2G);
                if (scOn)
                {
                    const double lp = scLp[c].lp (h, scPoleG);
                    h = (h - lp) + scShelf * lp;
                }
                const double r = h > 0.0 ? h : -shape::kNegative * h;
                // the knee's diodes ahead of the capacitor
                rect[c] = (std::pow (1.0 + r * (1.0 / shape::kR0), shape::kGamma) - 1.0) * (1.0 / shape::kGamma);
            }
            if (link)
                rect[0] = rect[1] = std::max (rect[0], rect[1]);
            const int nDet = link ? 1 : 2;
            double gr[2];
            for (int c = 0; c < nDet; ++c)
            {
                double& pk = peak[c];
                if (rect[c] > pk)
                    pk += (rect[c] - pk) * kAtk;
                pk -= pk * kRel;
                smooth[c] += (pk - smooth[c]) * kSm;
                const double u = levelOf (smooth[c]);
                gr[c] = staticGrDb (curve, ratio, u);
                if (c == 0)
                    levelNow = u;
            }
            if (link)
            {
                gr[1] = gr[0];
                peak[1] = peak[0];
                smooth[1] = smooth[0];
            }
            for (int c = 0; c < 2; ++c)
            {
                grNow[c] = gr[c];
                const double g = std::exp ((gr[c] + restDb) * ln10over20);
                double w = x[c];
                {
                    // the FET's second harmonic (held back for very hot signals)
                    const double k = shape::kDirtyK * std::min (1.0, std::exp ((gr[c] - shape::kDirtyKneeDb) * ln10over20));
                    const double x2 = x[c] * x[c];
                    w += k * x2 / (1.0 + x2 * x2 * (1.0 / 256.0));
                }
                double y = mx * g * w + (1.0 - mx) * x[c];
                // the output stage
                for (int s = 0; s < 4; ++s)
                {
                    double lp;
                    const double bp = svf[s][c].tick (y, sec[s].c, lp);
                    const double hp = y - sec[s].c.k * bp - lp;
                    y = sec[s].mHp * hp + sec[s].mBp * bp + sec[s].mLp * lp;
                }
                y *= stageGain;
                // the germanium transistor (its distortion's DC blocked; at 0 % the stage stays linear)
                if (satAmt > 0.0)
                {
                    // (held to the curve's table first, 16 times the headroom, where the curve is flat: what is
                    // over it would only leak past the distortion's DC blocker; Drive and Feedback near 10 reach it)
                    const double lim = kSatHalf * kSatStep * satHead;
                    if (std::isfinite (y))
                        y = std::clamp (y, -lim, lim);
                    double u = y / satHead;
                    u = std::isfinite (u) ? std::clamp (u, -kSatHalf * kSatStep, kSatHalf * kSatStep) : 0.0; // (a NaN reads the middle)
                    const double v = u / kSatStep + kSatHalf;
                    const int i0 = std::min (2 * kSatHalf - 1, (int)v);
                    const double z = satHead * (satTab[i0] + (satTab[i0 + 1] - satTab[i0]) * (v - i0));
                    const double d = (z - y) * std::min (1.0, satAmt * 20.0); // (fades in over the first 5 %)
                    y += d - satDc[c].lp (d, satDcG);
                }
                up[c][(size_t)j] = (float)y;
            }
        }
        os[0].down (up[0].data (), wet[0].data (), m);
        os[1].down (up[1].data (), wet[1].data (), m);

        // Mid/Side back, Output
        for (int i = 0; i < m; ++i)
        {
            out += (outT - out) * smoothOut;
            if (std::fabs (outT - out) < 1e-6f)
                out = outT;
            double l = wet[0][(size_t)i], r = wet[1][(size_t)i];
            if (midSide)
            {
                const double mid = l, side = r;
                l = mid + side;
                r = mid - side;
            }
            yl[a + i] = (float)std::clamp (l * out, -64.0, 64.0);
            yr[a + i] = (float)std::clamp (r * out, -64.0, 64.0);
        }
        // (never: a NaN or an overflow in the filters starts them again)
        bool ok = std::isfinite (yl[a + m - 1]) && std::isfinite (yr[a + m - 1]) && std::isfinite (smooth[0]) && std::isfinite (smooth[1]);
        for (int s = 0; ok && s < 4; ++s)
            ok = svf[s][0].finite () && svf[s][1].finite ();
        if (!ok)
        {
            reset ();
            for (int i = 0; i < m; ++i)
                yl[a + i] = yr[a + i] = 0.0f;
        }
    }

    if (meters)
    {
        constexpr auto rx = std::memory_order_relaxed;
        meters->sampleRate.store ((float)sr, rx);
        meters->grDb[0].store ((float)grNow[0], rx);
        meters->grDb[1].store ((float)grNow[1], rx);
        meters->levelDb.store ((float)levelNow, rx);
        meters->blocks.fetch_add (1, std::memory_order_release);
    }
    pk::endClip (yl, yr, n, p[kSoftClip] >= 0.5, p[kHardClip] >= 0.5);
}

} // namespace sunder
