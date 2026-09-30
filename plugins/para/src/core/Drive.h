// Para's drive: a gain into Smacheratr's Analog curve (smacheratr::analogClip), 4x oversampled with
// Smacheratr's oversampling filters (computed polyphase: Oversampler.h), left and right apart. The
// stage is always in the path: off, it only delays the signal by the oversampler's latency, so the
// latency never changes. On and off crossfade (20 ms) between that delayed signal and the curve's
// output.
#pragma once

#include "Oversampler.h"
#include "smacheratr/src/core/Shaper.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace para {

class DriveStage
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        maxBlock = std::max (1, maxBlockSize);
        for (int c = 0; c < 2; ++c)
        {
            os[c].prepare (sampleRate, maxBlock);
            dry[c].assign ((size_t)os[c].latency (), 0.0f);
        }
        pre.assign ((size_t)maxBlock, 0.0f);
        wet.assign ((size_t)maxBlock, 0.0f);
        gGain.assign ((size_t)maxBlock, 0.0f);
        gAmt.assign ((size_t)maxBlock, 0.0f);
        osBuf.assign ((size_t)maxBlock * 4, 0.0f);
        smooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sampleRate)));
        reset ();
    }
    void reset ()
    {
        for (int c = 0; c < 2; ++c)
        {
            os[c].reset ();
            std::fill (dry[c].begin (), dry[c].end (), 0.0f);
        }
        pos = 0;
        gain = gainT;
        amt = amtT;
        idle = amt <= 0.0f;
    }
    // Input samples; the same whether the drive is on or off.
    int latency () const { return os[0].latency (); }

    void set (bool on, double driveDb)
    {
        amtT = on ? 1.0f : 0.0f;
        gainT = (float)std::pow (10.0, driveDb / 20.0);
    }

    // In place, n <= the prepared block size.
    void process (float* const ch[2], int n)
    {
        const int len = (int)dry[0].size ();
        if (amtT <= 0.0f && amt < 1e-5f)
        {
            // off: only the delay (the curve restarts from silence when it is turned on again)
            amt = 0.0f;
            idle = true;
            for (int c = 0; c < 2; ++c)
            {
                int p = pos;
                for (int i = 0; i < n; ++i)
                {
                    const float d = len > 0 ? dry[c][(size_t)p] : ch[c][i];
                    if (len > 0)
                    {
                        dry[c][(size_t)p] = ch[c][i];
                        if (++p >= len)
                            p = 0;
                    }
                    ch[c][i] = d;
                }
            }
            advance (n, len);
            return;
        }
        if (idle)
        {
            idle = false;
            for (auto& o : os)
                o.reset ();
        }
        for (int i = 0; i < n; ++i)
        {
            gain += (gainT - gain) * smooth;
            amt += (amtT - amt) * smooth;
            gGain[(size_t)i] = gain;
            gAmt[(size_t)i] = amt;
        }
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < n; ++i)
                pre[(size_t)i] = ch[c][i] * gGain[(size_t)i];
            os[c].up (pre.data (), osBuf.data (), n);
            for (int k = 0; k < 4 * n; ++k)
                osBuf[(size_t)k] = (float)smacheratr::analogClip (osBuf[(size_t)k]);
            os[c].down (osBuf.data (), wet.data (), n);
            int p = pos;
            for (int i = 0; i < n; ++i)
            {
                const float d = len > 0 ? dry[c][(size_t)p] : ch[c][i];
                if (len > 0)
                {
                    dry[c][(size_t)p] = ch[c][i];
                    if (++p >= len)
                        p = 0;
                }
                ch[c][i] = d + (wet[(size_t)i] - d) * gAmt[(size_t)i];
            }
        }
        advance (n, len);
    }

private:
    void advance (int n, int len)
    {
        if (len > 0)
            pos = (pos + n) % len;
    }

    Oversampler4x os[2];
    std::vector<float> dry[2]; // the delay the dry signal takes, as long as the oversampler's
    std::vector<float> pre, wet, gGain, gAmt, osBuf;
    int maxBlock = 512, pos = 0;
    float gain = 1.0f, gainT = 1.0f, amt = 0.0f, amtT = 0.0f, smooth = 0.0f;
    bool idle = true;
};

} // namespace para
