// A user gesture: read from a JSON file (off the audio thread; this allocates), kept in the plug-in's state.
//
// Two formats are read:
//   moistr's own      {"name": "...", "length_beats": 8, "points": [[beat, value], ...]}   values 0 .. 1
//   the extractor's   {"name": "...", "time_unit": "beats", "points": [[beat, value], ...], "min": a, "max": b}
//                     (scripts/als_extract.py --out: a lane's raw values; normalised here by min and max)
// Beats are from the gesture's start; a file whose first point is later (a lane in song beats) is moved to
// start at its first point. Without length_beats the length is the span of the points. Points are sorted by
// beat (keeping the order of points at the same beat: a jump), and values outside 0 .. 1 are clamped.
#pragma once

#include "Gesture.h"

#include <string>
#include <utility>
#include <vector>

namespace moistr {

struct GestureData
{
    std::string name;
    double length = 1.0;
    std::vector<std::pair<double, double>> points; // (beat, value 0 .. 1)
    bool empty () const { return points.empty (); }
};

// false with a reason when the text is not a gesture
bool parseGestureJson (const std::string& text, const std::string& fallbackName, GestureData& out, std::string& error);
// The curve for the engine (false when it cannot be one: no points, more than kMaxGesturePoints)
bool toGesture (const GestureData& d, Gesture& g);
// moistr's own format
std::string gestureJson (const GestureData& d);

} // namespace moistr
