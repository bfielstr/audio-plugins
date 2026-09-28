#include "Params.h"

#include <vector>

namespace stretchr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        v.push_back (choice (kAlgorithm, "Algorithm", "Algo",
                             {"Simple Windowed", "Balanced", "Polyphonic", "Soloist", "Beats", "Extreme", "Tape"},
                             kPolyphonic));
        v.push_back (integer (kPitch, "Pitch", "Pitch", -24.0, 24.0, 0.0, Disp::Semis));
        v.push_back (integer (kFine, "Fine", "Fine", -100.0, 100.0, 0.0, Disp::Cents));
        v.push_back (real (kFormant, "Formant", "Formant", -12.0, 12.0, 0.0, Curve::Linear, Disp::Semis));
        v.push_back (toggle (kPreserveFormants, "Preserve Formants", "Formants", false));
        v.push_back (real (kSpeed, "Speed", "Speed", 0.05, 4.0, 1.0, Curve::Log, Disp::Percent));
        v.push_back (toggle (kFollowTempo, "Follow Tempo", "Follow", false));
        v.push_back (real (kSourceBpm, "Source Tempo", "BPM", 40.0, 240.0, 120.0, Curve::Linear, Disp::Bpm));
        v.push_back (real (kWindow, "Window", "Window", 10.0, 200.0, 60.0, Curve::Log, Disp::Ms));
        v.push_back (choice (kTransients, "Transients", "Trans", {"Crisp", "Mixed", "Smooth"}, kMixed));
        v.push_back (real (kSmear, "Smear", "Smear", 100.0, 4000.0, 500.0, Curve::Log, Disp::Ms));
        v.push_back (real (kGain, "Gain", "Gain", -24.0, 12.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (choice (kOutside, "Outside Clip", "Outside", {"Thru", "Mute"}, kThru));
        pk::addTailParams (v, kTailBase);
        return v;
    }());
    return t;
}

} // namespace stretchr
