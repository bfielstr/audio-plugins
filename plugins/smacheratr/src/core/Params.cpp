#include "Params.h"

#include <vector>

namespace smacheratr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        v.push_back (real (kDrive, "Drive", "Drive", -36.0, 36.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (toggle (kPreLimit, "Pre-Limit", "Pre-Limit", true));
        v.push_back (real (kPreLimitThreshold, "Pre-Limit Threshold", "Limit", -30.0, 0.0, -6.0, Curve::Linear, Disp::Db));
        v.push_back (choice (kPostClip, "Post Clip Mode", "Post Clip", {"No Clip", "Soft Clip", "Hard Clip"}, kPostOff));
        v.push_back (toggle (kColorOn, "Color", "Color", true));
        v.push_back (real (kColorLo, "Color Amount Low", "Amt Lo", -1.0, 1.0, 0.0, Curve::Linear, Disp::Percent));
        v.push_back (real (kColorHi, "Color Amount High", "Amt Hi", -1.0, 1.0, 0.0, Curve::Linear, Disp::Percent));
        v.push_back (real (kColorFreq, "Color Frequency", "Freq", 50.0, 18000.0, 1000.0, Curve::Log, Disp::Hz));
        v.push_back (real (kColorWidth, "Color Width", "Width", 0.1, 4.0, 1.0, Curve::Log, Disp::Number));
        v.push_back (real (kOutput, "Output", "Output", -36.0, 0.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (percent (kDryWet, "Dry/Wet", "Dry/Wet", 1.0));
        v.push_back (choice (kOversampling, "Oversampling", "Oversampling", {"Off", "2x", "4x"}, kOs4x));
        v.push_back (toggle (kDcFilter, "Pre-DC Filter", "DC Filter", false));
        v.push_back (toggle (kMidSide, "Mid/Side", "M/S", false));
        v.push_back (toggle (kClarity, "Gentlr", "Gentlr", true));
        v.push_back (real (kClarityFreq, "Gentlr Frequency", "Freq", 20.0, 20000.0, 250.0, Curve::Log, Disp::Hz));
        v.push_back (real (kClarityWidth, "Gentlr Width", "Width", kMinWidthOct, kMaxWidthOct, 2.0, Curve::Linear, Disp::Number));
        v.push_back (real (kClarityRange, "Gentlr Range", "Range", 0.0, 24.0, 8.0, Curve::Linear, Disp::Db));
        v.push_back (toggle (kClarity2, "Gentlr 2 (unused)", "Gentlr 2", false));
        v.push_back (real (kClarity2Freq, "Gentlr 2 Frequency", "Freq", 20.0, 20000.0, 3000.0, Curve::Log, Disp::Hz));
        v.push_back (real (kClarity2Width, "Gentlr 2 Width", "Width", kMinWidthOct, kMaxWidthOct, 2.0, Curve::Linear, Disp::Number));
        v.push_back (real (kClarity2Range, "Gentlr 2 Range", "Range", 0.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (toggle (kClarityAdvanced, "Gentlr Advanced", "Advanced", false));
        v.push_back (real (kClarityThreshold, "Gentlr Threshold", "Thresh", -60.0, 0.0, kClarityThresholdDb, Curve::Linear, Disp::Db));
        v.push_back (real (kClarity2Threshold, "Gentlr 2 Threshold", "Thresh", -60.0, 0.0, kClarityThresholdDb, Curve::Linear, Disp::Db));
        v.push_back (toggle (kClarityDrive, "Gentlr Drive", "Drive", false));
        v.push_back (real (kClarityDriveAmount, "Gentlr Drive Amount", "Drive", 0.0, 36.0, 12.0, Curve::Linear, Disp::Db));
        // (Sub and High: their buttons are unused, a band works while its Range is above 0 dB, 0 by default)
        v.push_back (toggle (kClaritySub, "Gentlr Sub (unused)", "Sub", false));
        v.push_back (real (kClaritySubFreq, "Gentlr Sub Frequency", "Freq", kSubMinHz, kSubMaxHz, kSubDefaultHz, Curve::Log, Disp::Hz));
        v.push_back (real (kClaritySubRange, "Gentlr Sub Range", "Range", 0.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kClaritySubThreshold, "Gentlr Sub Threshold", "Thresh", -60.0, 0.0, kClarityThresholdDb, Curve::Linear, Disp::Db));
        v.push_back (toggle (kClarityHigh, "Gentlr High (unused)", "High", false));
        v.push_back (real (kClarityHighFreq, "Gentlr High Frequency", "Freq", kHighMinHz, kHighMaxHz, kHighDefaultHz, Curve::Log, Disp::Hz));
        v.push_back (real (kClarityHighRange, "Gentlr High Range", "Range", 0.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kClarityHighThreshold, "Gentlr High Threshold", "Thresh", -60.0, 0.0, kClarityThresholdDb, Curve::Linear, Disp::Db));
        v.push_back (toggle (kClarityNoOverlap, "Gentlr No Overlap", "No Overlap", false));
        v.push_back (choice (kClaritySlope, "Gentlr Slope", "Slope", {"12 / 12", "Signature", "Classic", "Alt Signature"}, kSlopeSignature));
        // (glue: off, every pair; Glue.h)
        v.push_back (toggle (kClarityGlue12, "Gentlr Glue 1 / 2", "Glue 1/2", false));
        v.push_back (toggle (kClarityGlueSub1, "Gentlr Glue Sub / 1", "Glue Sub/1", false));
        v.push_back (toggle (kClarityGlueSub2, "Gentlr Glue Sub / 2", "Glue Sub/2", false));
        v.push_back (toggle (kClarityGlue1High, "Gentlr Glue 1 / High", "Glue 1/High", false));
        v.push_back (toggle (kClarityGlue2High, "Gentlr Glue 2 / High", "Glue 2/High", false));
        return v;
    }());
    return t;
}

} // namespace smacheratr
