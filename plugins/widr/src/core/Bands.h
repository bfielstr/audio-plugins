// The 24 third-octave bands Widr measures, shapes and negotiates in: fixed frequencies (100 Hz to
// 20 kHz) so every instance talks about the same bands whatever its Mono Below.
#pragma once

#include <cmath>

namespace widr {

constexpr int kBands = 24;

inline double bandHz (int k) { return 100.0 * std::pow (2.0, k / 3.0); }
inline double bandLowHz (int k) { return bandHz (k) * std::pow (2.0, -1.0 / 6.0); }
inline double bandHighHz (int k) { return bandHz (k) * std::pow (2.0, 1.0 / 6.0); }

} // namespace widr
