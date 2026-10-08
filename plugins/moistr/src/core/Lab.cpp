#include "Lab.h"

#include <algorithm>
#include <cmath>

namespace moistr {

namespace {
// the slots that run now: the chains on the bands and POST (chain 4's are kept for later, not made)
constexpr bool runs (int s) { return s < chainSlot (kNumBandChains, 0) || s >= postSlot (0); }
} // namespace

void Lab::Delay::reset ()
{
    for (auto& b : buf)
        b.assign ((size_t)kMaxLatency, 0.0);
    w = 0;
}

double Lab::Delay::tick (int ch, double x, int delay)
{
    auto& b = buf[ch];
    b[(size_t)w] = x;
    return b[(size_t)((w - delay) & (kMaxLatency - 1))];
}

void Lab::Below::reset ()
{
    for (auto& ch : hp)
        for (auto& f : ch)
            f.reset ();
}

double Lab::Below::tick (int ch, double dry, double out, const dsp::SvfCoefs& c)
{
    const double d = out - dry;
    return dry + hp[ch][1].tick (hp[ch][0].tick (d, c).hp, c).hp;
}

Lab::Lab ()
{
    for (int s = 0; s < kNumLabSlots; ++s)
        if (runs (s))
            slots[(size_t)s] = std::make_unique<smemplr::FxSlot> ();
    for (auto& d : chainDelay)
        d.reset ();
    for (auto& d : chainDry)
        d.reset ();
    postDry.reset ();
    lowDelay.reset ();
    gain.fill (1.0);
}

void Lab::prepare (double sampleRate)
{
    sr = sampleRate;
    fade = 1.0 - std::exp (-1.0 / (0.02 * sr));
    for (auto& s : slots)
        if (s)
            s->prepare (sr, kChunk);
    reset ();
}

void Lab::reset ()
{
    for (auto& s : slots)
        if (s)
            s->reset ();
    for (auto& d : chainDelay)
        d.reset ();
    for (auto& d : chainDry)
        d.reset ();
    postDry.reset ();
    for (auto& b : chainBelow)
        b.reset ();
    postBelow.reset ();
    lowDelay.reset ();
    fill = 0;
    for (int ch = 0; ch < 2; ++ch)
        std::fill (held[ch], held[ch] + kChunk, 0.0);
    // every gain at its setting
    const bool anySolo = solo[0] || solo[1] || solo[2];
    for (int c = 0; c < kNumChains; ++c)
        gain[(size_t)c] = mute[(size_t)c] || (anySolo && !solo[(size_t)c]) || levelDb[(size_t)c] <= kLevelOffDb ? 0.0
                                                                                                                : std::pow (10.0, levelDb[(size_t)c] / 20.0);
    lowGain = anySolo ? 0.0 : 1.0;
    wasRunning.fill (false);
}

void Lab::setParam (uint32_t id, double plain)
{
    if (isChainParam (id))
    {
        const auto c = (size_t)((id - kChainBase) / kChainFields);
        switch ((id - kChainBase) % kChainFields)
        {
            case kChainLevel: levelDb[c] = std::fabs (plain) < 1e-6 ? 0.0 : plain; break; // (0 dB exactly from its normalized value)
            case kChainMute: mute[c] = plain >= 0.5; break;
            case kChainSolo: solo[c] = plain >= 0.5; break;
            case kChainMono: mono[c] = plain >= 0.5; break;
            default: break; // (kept for later)
        }
        return;
    }
    if (!isLabSlotParam (id))
        return;
    auto& s = slots[(size_t)labSlotOf (id)];
    if (!s)
        return;
    const uint32_t field = labFieldOf (id);
    if (field == kLabType)
    {
        // a kind this build does not run is Empty
        const int t = (int)std::lround (plain);
        s->setType (t > 0 && t < smemplr::kNumFxTypes ? t : smemplr::kFxEmpty);
    }
    else if (field == kLabOn)
        s->setOn (plain >= 0.5);
    else
        s->setValue (field - kLabBlock, plain);
}

void Lab::setTransport (double bpm, double ppq, bool playing)
{
    for (auto& s : slots)
        if (s)
            s->setTransport (bpm, ppq, playing);
}

bool Lab::active () const
{
    for (int s = 0; s < kNumLabSlots; ++s)
        if (slots[(size_t)s] && slots[(size_t)s]->type != smemplr::kFxEmpty)
            return true;
    if (lowGain != 1.0)
        return true;
    for (int c = 0; c < kNumBandChains; ++c)
        if (levelDb[(size_t)c] != 0.0 || mute[(size_t)c] || solo[(size_t)c] || mono[(size_t)c] || gain[(size_t)c] != 1.0)
            return true;
    return false;
}

int Lab::chainLatency (int chain) const
{
    int l = 0;
    for (int k = 0; k < kChainSlots; ++k)
        if (const auto& s = slots[(size_t)chainSlot (chain, k)])
            l += s->latency ();
    return l;
}

int Lab::postLatency () const
{
    int l = 0;
    for (int k = 0; k < kPostSlots; ++k)
        l += slots[(size_t)postSlot (k)]->latency ();
    return l;
}

int Lab::latency () const
{
    if (!holdsEffects ())
        return 0;
    int l = 0;
    for (int c = 0; c < kNumBandChains; ++c)
        l = std::max (l, chainLatency (c));
    return std::min (kChunk + l + postLatency (), kMaxLatency - 1);
}

int Lab::firstOf (int chain, int fxType) const
{
    const int first = chain >= kNumBandChains ? postSlot (0) : chainSlot (chain, 0);
    const int count = chain >= kNumBandChains ? kPostSlots : kChainSlots;
    for (int k = 0; k < count; ++k)
        if (slots[(size_t)(first + k)] && slots[(size_t)(first + k)]->type == fxType)
            return first + k;
    return -1;
}

void Lab::runSlots (int first, int count, float* l, float* r, int m)
{
    for (int k = 0; k < count; ++k)
    {
        auto& s = *slots[(size_t)(first + k)];
        if (s.type != smemplr::kFxEmpty)
            s.process (l, r, m);
    }
}

bool Lab::holdsEffects () const
{
    for (int s = 0; s < kNumLabSlots; ++s)
        if (runs (s) && slots[(size_t)s]->type != smemplr::kFxEmpty)
            return true;
    return false;
}

void Lab::run (float (*in)[2][kTick], double (*low)[kTick], double (*up)[kTick], int m, double lowX)
{
    below.set (std::tan (dsp::kPi * std::min (lowX, 0.45 * sr) / sr), dsp::kSqrt2);
    const bool chunked = holdsEffects ();
    const int lowLat = latency ();
    if (!chunked)
    {
        // (no effects: the gains, Mono and the sum at once)
        for (int c = 0; c < kNumBandChains; ++c)
            for (int ch = 0; ch < 2; ++ch)
                std::copy (in[c][ch], in[c][ch] + m, chunkIn[c][ch]);
        block (m);
        for (int ch = 0; ch < 2; ++ch)
            std::copy (chunkOut[ch], chunkOut[ch] + m, up[ch]);
        fill = 0;
    }
    else
        for (int i = 0; i < m; ++i)
        {
            // the effects run kChunk samples at a time: a sample in comes out kChunk later (part of the latency)
            for (int c = 0; c < kNumBandChains; ++c)
            {
                chunkIn[c][0][fill] = in[c][0][i];
                chunkIn[c][1][fill] = in[c][1][i];
            }
            up[0][i] = held[0][fill];
            up[1][i] = held[1][fill];
            if (++fill == kChunk)
            {
                block (kChunk);
                for (int ch = 0; ch < 2; ++ch)
                    std::copy (chunkOut[ch], chunkOut[ch] + kChunk, held[ch]);
                fill = 0;
            }
        }
    // the Low band, lined up (silent while a chain is soloed)
    const bool anySolo = solo[0] || solo[1] || solo[2];
    const double lowTarget = anySolo ? 0.0 : 1.0;
    for (int i = 0; i < m; ++i)
    {
        lowGain += (lowTarget - lowGain) * fade;
        if (std::fabs (lowTarget - lowGain) < 1e-6)
            lowGain = lowTarget;
        low[0][i] = lowDelay.tick (0, low[0][i] * lowGain, lowLat);
        low[1][i] = lowDelay.tick (1, low[1][i] * lowGain, lowLat);
        lowDelay.advance ();
    }
}

void Lab::block (int m)
{
    double dry[2][kChunk];
    int lat[kNumBandChains], slowest = 0;
    for (int c = 0; c < kNumBandChains; ++c)
    {
        lat[c] = chainLatency (c);
        slowest = std::max (slowest, lat[c]);
    }
    const int postLat = postLatency ();
    const bool anySolo = solo[0] || solo[1] || solo[2];
    double (*up)[kChunk] = chunkOut;
    for (int ch = 0; ch < 2; ++ch)
        std::fill (up[ch], up[ch] + m, 0.0);
    for (int c = 0; c < kNumBandChains; ++c)
    {
        const auto cc = (size_t)c;
        float (*in)[kChunk] = chunkIn[c];
        double target = levelDb[cc] <= kLevelOffDb ? 0.0 : std::pow (10.0, levelDb[cc] / 20.0);
        if (mute[cc] || (anySolo && !solo[cc]))
            target = 0.0;
        // a chain faded out is not run (it starts clean when it is heard again)
        const bool silent = gain[cc] == 0.0 && target == 0.0;
        // (the input, lined up with the effects' output: always written, so it is ready when the chain is heard again)
        for (int i = 0; i < m; ++i)
        {
            dry[0][i] = chainDry[c].tick (0, in[0][i], lat[c]);
            dry[1][i] = chainDry[c].tick (1, in[1][i], lat[c]);
            chainDry[c].advance ();
        }
        if (!silent)
        {
            if (!wasRunning[cc])
            {
                for (int k = 0; k < kChainSlots; ++k)
                    slots[(size_t)chainSlot (c, k)]->reset ();
                chainBelow[c].reset ();
            }
            runSlots (chainSlot (c, 0), kChainSlots, in[0], in[1], m);
        }
        wasRunning[cc] = !silent;
        const int delay = slowest - lat[c];
        double& g = gain[cc];
        for (int i = 0; i < m; ++i)
        {
            double l = silent ? 0.0 : chainBelow[c].tick (0, dry[0][i], in[0][i], below);
            double r = silent ? 0.0 : chainBelow[c].tick (1, dry[1][i], in[1][i], below);
            if (mono[cc])
                l = r = 0.5 * (l + r);
            g += (target - g) * fade;
            if (std::fabs (target - g) < 1e-6)
                g = target;
            up[0][i] += chainDelay[c].tick (0, l * g, delay);
            up[1][i] += chainDelay[c].tick (1, r * g, delay);
            chainDelay[c].advance ();
        }
    }
    // POST on the chains' sum
    bool posting = false;
    for (int k = 0; k < kPostSlots; ++k)
        posting = posting || slots[(size_t)postSlot (k)]->type != smemplr::kFxEmpty;
    if (posting)
    {
        for (int i = 0; i < m; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                post[ch][i] = (float)up[ch][i];
                dry[ch][i] = postDry.tick (ch, post[ch][i], postLat);
            }
            postDry.advance ();
        }
        runSlots (postSlot (0), kPostSlots, post[0], post[1], m);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < m; ++i)
                up[ch][i] = postBelow.tick (ch, dry[ch][i], post[ch][i], below);
    }
}

} // namespace moistr
