// The colour filters: a low shelf (Amt Lo) and a peak (Amt Hi at Freq, Width = 1 / Q) applied
// before the shaper and undone after it, so the amounts change how much of each range is
// saturated without changing the balance of the output.
#pragma once

#include "Biquad.h"

#include <algorithm>

namespace smacheratr {

constexpr double kColorLowHz = 100.0;
constexpr double kColorMaxDb = 24.0; // the filter gain at an amount of +-100 %

inline double colorDb (double amount) { return kColorMaxDb * std::clamp (amount, -1.0, 1.0); }

inline double colorQ (double width) { return 1.0 / std::clamp (width, 0.05, 10.0); }

inline double colorPeakHz (double freq, double rate) { return std::min (freq, rate * 0.45); }

// Response of the pre-shaper stage in dB for amounts lo / hi (the post stage is its mirror image).
inline double colorResponseDb (double hz, double sr, double lo, double hi, double freq, double width)
{
    return magnitudeDb (lowShelf (sr, kColorLowHz, colorDb (lo)), hz, sr) +
           magnitudeDb (peak (sr, colorPeakHz (freq, sr), colorDb (hi), colorQ (width)), hz, sr);
}

} // namespace smacheratr
