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

// ---- the one gesture (0.30): a JSON file with a lane per target, all on one timeline
//
//   {"name": "...", "length_beats": 8,
//    "lanes": [{"target": "High Level", "points": [[beat, value], ...], "min": -24, "max": 0,
//               "source": "where it came from (optional)"}, ...]}
//
// target: a target's name (kTargetNames: Mid Level, High Level, Air Level, Wobble Rate, Wobble Amount, Close,
// Liquid Pos, Dirt, Bells, Mid X, High X, Seed Blend, Shift, Mid Grit, High Grit, Air Grit, Mid OTT, High OTT, Air OTT,
// Post OTT; "Off": a lane kept in the file that does nothing).
// Values 0 .. 1 (clamped), straight lines between points, two points at one beat a jump. min and max (optional)
// are what 0 and 1 mean in the target's units (Gesture.h: targetNorm; Level in dB from the band's Level, Close in
// Hz, Wobble Rate in cycles per beat ...); without them a lane covers the target's whole range. min above max turns
// the lane round. Without length_beats the length is the last point's beat. At most kMaxSceneLanes lanes with a
// target, kMaxGesturePoints points each.
struct SceneLaneData
{
    std::string target; // a target's name (as kTargetNames)
    std::string source; // (kept for the user: where the lane came from)
    bool hasMin = false, hasMax = false;
    double min = 0.0, max = 1.0;
    std::vector<std::pair<double, double>> points; // (beat, value 0 .. 1)
};
struct SceneData
{
    std::string name;
    double length = 1.0;
    std::vector<SceneLaneData> lanes;
    bool empty () const { return lanes.empty (); }
};
// false with a reason when the text is not a gesture of lanes (a 0.27 single curve without a "target" is not)
bool parseSceneJson (const std::string& text, const std::string& fallbackName, SceneData& out, std::string& error);
// the scene for the engine (the lanes with a target; false when it cannot be one)
bool toScene (const SceneData& d, Scene& s);
std::string sceneJson (const SceneData& d);

} // namespace moistr
