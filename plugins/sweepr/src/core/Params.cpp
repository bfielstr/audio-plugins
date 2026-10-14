#include "Params.h"

#include "BellShape.h"

#include "smacheratr/src/core/TailExt.h"

#include <string>
#include <vector>

namespace sweepr {

using namespace pk;
using namespace pk::make;

double divisionBeats (int d)
{
    // 1/32 is an eighth of a beat; a triplet two thirds of its note, a dotted one and a half
    static const double beats[kNumDivisions] = {0.125,       0.25 * 2.0 / 3.0, 0.25, 0.375, 0.5 * 2.0 / 3.0, 0.5, 0.75, 2.0 / 3.0,
                                                1.0,         1.5,              4.0 / 3.0, 2.0, 3.0, 4.0, 8.0, 16.0};
    return beats[d < 0 ? 0 : (d >= kNumDivisions ? kNumDivisions - 1 : d)];
}

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        // a new instance: the settings the plug-in was made to replace (a low boost at 110 Hz and a cut
        // at 320 Hz, both swept an octave up and down every beat), into tape
        // (the bells' ranges, tapers and shape: BellShape.h)
        const double freq[kBands] = {110.0, 320.0}, q[kBands] = {1.0, 1.6}, gain[kBands] = {13.0, -15.0};
        for (int b = 0; b < kBands; ++b)
        {
            const std::string n = b == 0 ? "A " : "B ";
            auto id = [b] (uint32_t f) { return bandParam (b, f); };
            v.push_back (real (id (kFreq), keep (n + "Freq"), "Freq", bell::kFreqMin, bell::kFreqMax, freq[b], Curve::Log, Disp::Hz));
            v.push_back (real (id (kQ), keep (n + "Q"), "Q", bell::kQMin, bell::kQMax, q[b], Curve::Log, Disp::Number));
            v.push_back (real (id (kGain), keep (n + "Gain"), "Gain", bell::kGainMin, bell::kGainMax, gain[b], Curve::Linear, Disp::Db));
            v.push_back (toggle (id (kLfoSync), keep (n + "LFO Sync"), "Sync", true));
            v.push_back (choice (id (kLfoDiv), keep (n + "LFO Division"), "Division",
                                 {"1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/2D",
                                  "1 Bar", "2 Bars", "4 Bars"},
                                 kDiv4));
            v.push_back (real (id (kLfoRate), keep (n + "LFO Rate"), "Rate", 0.01, 40.0, 1.0, Curve::Log, Disp::Hz));
            v.push_back (choice (id (kLfoShape), keep (n + "LFO Shape"), "Shape",
                                 {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random", "Smooth Random"}, kSine));
            v.push_back (real (id (kLfoDepth), keep (n + "LFO Depth"), "Depth", -4.0, 4.0, 1.0, Curve::Linear, Disp::Octaves));
            v.push_back (real (id (kLfoPhase), keep (n + "LFO Phase"), "Phase", 0.0, 360.0, 0.0, Curve::Linear, Disp::Degrees));
        }
        v.push_back (toggle (kLink, "LFO Link", "Link", false));
        v.push_back (toggle (kTapeOn, "Tape", "Tape", true));
        v.push_back (percent (kDrive, "Drive", "Drive", 0.75));
        v.push_back (percent (kSpeed, "Speed", "Speed", 0.8));
        v.push_back (percent (kCompression, "Compression", "Compression", 0.35));
        v.push_back (percent (kMix, "Mix", "Mix", 1.0));
        v.push_back (real (kOutput, "Output", "Output", -24.0, 12.0, -4.4, Curve::Linear, Disp::Db));
        // the end saturator: off in a new instance (the tape stage is the saturator here)
        pk::addTailParams (v, kTailBase, false);
        smacheratr::addTailExtParams (v, kTailExtBase);
        smacheratr::addTailExt2Params (v, kTailExt2Base);
        smacheratr::addTailExt3Params (v, kTailExt3Base);
        smacheratr::addTailExt4Params (v, kTailExt4Base);
        return v;
    }());
    return t;
}

} // namespace sweepr
