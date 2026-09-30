// Smoothr's limiter: a look-ahead brickwall that keeps the low end smooth.
//
// Why a plain limiter roughens the lows: its gain drops in a millisecond or two for every peak and
// comes back in tens of milliseconds, so within one period of a 50 Hz bass (20 ms) the gain moves;
// the bass comes out multiplied by a wobbling gain: harmonics on a held note, and sidebands around
// it wherever a kick or a snare pulls the gain down.
//
// Smoothr splits the signal in two near 200 Hz and gives each part its own gain:
//   - The split is linear phase and complementary: the lows are the input through four box
//     (moving-average) filters B, twiced (2 B - B B: flat further up, and the highs, (1 - B)^2, keep
//     under 1 % of a 55 Hz bass), the highs are the input (delayed as much) minus the lows. Both are
//     real, so with both gains equal the two add back to the input exactly: a limiter that is not
//     limiting passes the signal untouched (only delayed), and there is no phase turn at the split.
//   - The lows get a slow gain: a 10 ms look-ahead (the gain glides into a peak along an S-curve
//     half a period of a 50 Hz bass long), a 2 dB soft knee, and a release that is never faster
//     than 60 ms (three periods at 50 Hz); with Auto, sustained limiting releases slower still. Its
//     window is longer than half a bass period, so on a held bass note it sees every crest and
//     holds still instead of riding the waveform.
//   - The highs get a fast gain (1.5 ms look-ahead, the Release knob), worked out from what the lows
//     already do: the most the highs may keep with the lows at their gain. So a kick's click or a
//     snare is caught in the highs, and the bass under it hardly moves.
//   - Smooth sets how much the lows stay out of it. They are turned down for their own level, for
//     the whole signal with the highs counted at Smooth's weight (0 %: 0 dB, the lows follow the
//     whole signal, only slowly; 50 %: -12 dB, so on a peak the highs may take up to 12 dB more than
//     the lows; 100 %: -24 dB), and at least a share of what the highs have been turned down over the
//     last quarter second or so (all of it at 0 %, three quarters at 50 %, none at 100 %): a lone
//     kick click or snare crack is left to the highs, but when the highs are held down for a while
//     the lows come down with them, slowly, and the balance between them holds. Smooth also
//     lengthens the lows' release (60 ms at 0 %, 250 at 100 %). Towards 100 % the lows are left
//     alone more and more: louder and smoother still, with the highs taking all of the limiting.
//
// The ceiling: every gain comes from a look-ahead window with a minimum hold, a release that only
// ever lowers it, and averages that only mix values from inside the window, so the gain on each
// sample is never above what that sample needs. The highs' requirement is checked at the samples
// and at 7 points between each two (8x: the highs through a windowed sinc, the lows, which have
// nothing much over 300 Hz, along a straight line), 0.09 dB under the ceiling (what a peak between two
// checked points can add at 18 kHz), so peaks between samples are held too (true peak). A last clamp
// at the ceiling guarantees the sample peak; it counts how often it had to act (the tests expect
// never). The latency is constant for a sample rate.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace smoothr {

// A gain that is never above what the samples it covers need. push() takes the reduction the sample
// entering now needs (dB over the ceiling: a positive number is how far over; and optionally a
// reduction to make at least, past the knee) and returns the gain
// (linear) for the sample that entered delay() samples ago. Inside: the largest reduction in the
// window (a monotonic wedge), a soft knee, a release in dB (optionally program dependent), then two
// moving averages in a row (an S-shaped ramp) whose spans add up to the window.
class LookaheadGain
{
public:
    void prepare (int window);
    void reset ();
    // per-sample coefficients: the release (exp (-1 / samples)); Auto adds a sustained part that
    // rises towards the reduction with `susUp` (1 - exp (-1 / samples)) and falls with `susDown`
    void setRelease (double release, bool autoMode, double susUp, double susDown);
    void setKnee (double kneeDb) { knee = kneeDb; }
    int delay () const { return window - 1; }
    float push (float overDb, float atLeastDb = 0.0f);
    float reductionDb () const { return (float)env; } // the release envelope now (dB, 0 or more)

private:
    struct Box
    {
        std::vector<double> buf;
        double sum = 0.0;
        int pos = 0;
        void prepare (int n, double fill);
        void fill (double v);
        double push (double x);
    };
    int window = 1;
    // the wedge: reductions (dB) and their sample numbers, the largest at the front
    std::vector<float> wVal;
    std::vector<int64_t> wIdx;
    int wHead = 0, wCount = 0;
    int64_t count = 0;
    double knee = 0.0;
    double rel = 0.0, susUp = 0.0, susDown = 0.0;
    bool autoRel = false;
    double env = 0.0, sus = 0.0;
    Box box1, box2;
};

class Limiter
{
public:
    static constexpr double kBoxHz = 150.0; // where one set of four boxes is 6 dB down (the split, twiced, is 6 dB down near 200 Hz)
    static constexpr double kSlowMs = 10.0, kFastMs = 1.5; // the look-ahead of the lows' and the highs' gains
    static constexpr double kLowKneeDb = 2.0;
    static constexpr int kTaps = 12, kOver = 8; // the true-peak check: 8x, the highs through 2 x kTaps taps per phase

    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setCeilingDb (double db) { ceilingDb = db; }
    void setRelease (double ms, bool autoMode);
    void setSmooth (double amount); // 0..1
    int latency () const { return splitDelay + slow.delay () + kTaps + fastH.delay (); }

    // In place (n <= maxBlock). Afterwards lowGain / highGain hold each output sample's gain on the
    // lows and on the highs (linear), and inLevel the limiter's input peak (both channels) lined up
    // with the output.
    void process (float* l, float* r, int n);
    const float* lowGain () const { return gLowOut.data (); }
    const float* highGain () const { return gHighOut.data (); }
    const float* inLevel () const { return inOut.data (); }
    int64_t clamps () const { return clampCount; }        // samples the last clamp touched
    int64_t fullBandSamples () const { return fullCount; } // samples whose lows had to be pulled down fast too

    // the lows' filter: its gain at hz (the highs' is 1 minus it: both are real, the split is linear phase)
    double lowResponse (double hz) const;

private:
    template <int K>
    struct Delay
    {
        std::vector<std::array<float, K>> buf;
        int pos = 0;
        void prepare (int n)
        {
            buf.assign ((size_t)std::max (0, n), std::array<float, K> {});
            pos = 0;
        }
        void reset (const std::array<float, K>& v = {}) { std::fill (buf.begin (), buf.end (), v); }
        std::array<float, K> push (const std::array<float, K>& v)
        {
            if (buf.empty ())
                return v;
            const auto y = buf[(size_t)pos];
            buf[(size_t)pos] = v;
            if (++pos >= (int)buf.size ())
                pos = 0;
            return y;
        }
    };
    // one channel of the split: four moving averages of boxLen samples in a row (B), then four more
    // (B twice); the lows are 2 B - B B, lined up (so the highs are (1 - B) squared)
    struct Split
    {
        std::vector<double> buf[8], mid;
        double sum[8] = {};
        int pos = 0, midPos = 0;
        void prepare (int len);
        void reset ();
        double push (double x, int len);
    };
    void updateSettings ();

    double sr = 48000.0;
    int maxBlock = 512, boxLen = 102, splitDelay = 404;
    double ceilingDb = -1.0, releaseMs = 80.0, smooth = 0.5;
    bool autoRelease = true, dirty = true;
    double ceil = 0.891, ceilStep = 0.0, rho = 0.5, share = 0.5;
    // how far the highs have been turned down lately (dB, a slow average), and its rates
    double highsLately = 0.0, susRise = 0.0, susFall = 0.0;
    Split split[2];
    Delay<3> dryDelay;  // x (both channels) and the ceiling, lined up with the lows
    Delay<5> slowDelay; // lows, input (both channels) and ceiling, through the lows' look-ahead
    LookaheadGain slow, fastF, fastH;
    // the highs' stage: the last 2 kTaps samples of the lows (after their gain), the highs (per
    // channel) and the ceiling, each written twice (at i and i + 2 kTaps) so the last 2 kTaps are
    // always in a row; and the requirement of the segment before
    std::vector<float> histA[2], histB[2], histC;
    int histPos = 0;
    float prevSegF = 1.0f, prevSegH = 1.0f;
    float phase[kOver - 1][2 * kTaps] {}; // the interpolation taps, for 1/8 .. 7/8 of the way
    Delay<6> fastDelay; // lows, input, the lows' gain and the ceiling, through the interpolation and the highs' look-ahead
    std::vector<float> gLowOut, gHighOut, inOut;
    int64_t clampCount = 0, fullCount = 0;
};

} // namespace smoothr
