#include "Slices.h"

#include "Params.h"

#include <algorithm>
#include <cmath>

namespace smemplr {

int sliceMatchTolerance (const SampleData& sample) { return std::max (16, (int)(0.004 * sample.sampleRate)); }

void computeSlices (const SampleData& sample, const SliceEdits* edits, const SliceSettings& s, SliceList& out)
{
    out.count = 0;
    const int a = (int)std::floor (s.regionStart);
    const int b = (int)std::floor (s.regionEnd);
    if (b - a < 8)
        return;
    const int tol = sliceMatchTolerance (sample);

    // Scratch arrays live on the stack to stay allocation-free on the audio thread.
    int autoPos[kMaxAutoSlices];
    int autoCount = 0;
    autoPos[autoCount++] = a;

    auto addAuto = [&] (int p) {
        if (autoCount < kMaxAutoSlices && p > a + tol / 2 && p < b)
            autoPos[autoCount++] = p;
    };

    switch (s.sliceBy)
    {
        case kSliceTransient:
        {
            // Strength threshold falls as sensitivity rises; keep the strongest 63 above it.
            constexpr int kMaxCandidates = 2048;
            struct Cand
            {
                int pos;
                float strength;
            } cand[kMaxCandidates];
            int n = 0;
            float regionMax = 0.0f;
            for (const auto& o : sample.onsets)
                if (o.pos > a && o.pos < b)
                    regionMax = std::max (regionMax, o.strength);
            if (regionMax <= 0.0f)
                break;
            const double sens = std::clamp (s.sensitivity, 0.0, 1.0);
            const double thr = 0.85 * std::pow (1.0 - sens, 2.2);
            for (const auto& o : sample.onsets)
            {
                if (o.pos <= a + tol / 2 || o.pos >= b)
                    continue;
                const float rel = o.strength / regionMax;
                if (rel >= thr && (sens > 0.0 || rel >= 0.999f) && n < kMaxCandidates)
                    cand[n++] = {o.pos, rel};
            }
            const int keep = kMaxAutoSlices - 1;
            if (n > keep)
            {
                std::nth_element (cand, cand + keep, cand + n,
                                  [] (const Cand& x, const Cand& y) { return x.strength > y.strength; });
                n = keep;
            }
            for (int i = 0; i < n; ++i)
                addAuto (cand[i].pos);
            break;
        }
        case kSliceBeat:
        {
            const double beatLen = (b - a) / std::max (0.25, s.warpBeats);
            const double step = beatLen * divisionBeats (s.division);
            if (step >= 1.0)
                for (int k = 1; k < kMaxAutoSlices; ++k)
                {
                    const double p = a + k * step;
                    if (p >= b - 1)
                        break;
                    addAuto ((int)std::lround (p));
                }
            break;
        }
        case kSliceRegion:
        {
            const int n = regionsFromIndex (s.regions);
            for (int k = 1; k < n; ++k)
                addAuto ((int)std::lround (a + (double)(b - a) * k / n));
            break;
        }
        default: break; // Manual: only user slices (plus the region start)
    }

    auto near = [tol] (int x, int y) { return std::abs (x - y) <= tol; };
    const double len = std::max (1, sample.length);

    int count = 0;
    for (int i = 0; i < autoCount; ++i)
    {
        const int p = autoPos[i];
        bool suppressed = false;
        if (edits && i > 0) // the region start can't be removed
            for (double sp : edits->suppressed)
                if (near (p, (int)std::lround (sp * len)))
                {
                    suppressed = true;
                    break;
                }
        if (!suppressed && count < kMaxSlices)
        {
            out.pos[count] = p;
            out.manual[count] = false;
            ++count;
        }
    }
    if (edits)
        for (double mp : edits->manual)
        {
            const int p = (int)std::lround (mp * len);
            if (p < a || p >= b || count >= kMaxSlices)
                continue;
            bool replaced = false;
            for (int i = 0; i < count; ++i)
                if (near (out.pos[i], p))
                {
                    // a manual slice at the region start or on top of an auto slice wins
                    out.pos[i] = i == 0 && out.pos[i] == a ? a : p;
                    out.manual[i] = true;
                    replaced = true;
                    break;
                }
            if (!replaced)
            {
                out.pos[count] = p;
                out.manual[count] = true;
                ++count;
            }
        }

    // insertion sort (count <= 128)
    for (int i = 1; i < count; ++i)
    {
        const int p = out.pos[i];
        const bool m = out.manual[i];
        int j = i - 1;
        while (j >= 0 && out.pos[j] > p)
        {
            out.pos[j + 1] = out.pos[j];
            out.manual[j + 1] = out.manual[j];
            --j;
        }
        out.pos[j + 1] = p;
        out.manual[j + 1] = m;
    }
    out.count = count;
}

} // namespace smemplr
