#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <vector>

namespace para {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        v.push_back (real (kHpFreq, "High-Pass Frequency", "HP Freq", 20.0, 20000.0, 300.0, Curve::Log, Disp::Hz));
        v.push_back (percent (kHpRes, "High-Pass Resonance", "HP Res", 0.0));
        v.push_back (real (kLpFreq, "Low-Pass Frequency", "LP Freq", 20.0, 20000.0, 100.0, Curve::Log, Disp::Hz));
        v.push_back (percent (kLpRes, "Low-Pass Resonance", "LP Res", 0.0));
        // (12 / 18 / 24 dB in states from before: slopeFromThreeChoices)
        v.push_back (choice (kSlope, "Slope", "Slope",
                             {"6 dB", "12 dB", "18 dB", "24 dB", "36 dB", "48 dB", "60 dB", "72 dB", "84 dB", "96 dB", "Brickwall"}, kSlope24));
        v.push_back (real (kSplit, "Split", "Split", -48.0, 48.0, 0.0, Curve::Linear, Disp::Semis));
        v.push_back (real (kEnvAmount, "Envelope Amount", "Env Amt", -48.0, 48.0, 0.0, Curve::Linear, Disp::Semis));
        v.push_back (real (kEnvAttack, "Envelope Attack", "Attack", 0.1, 2000.0, 5.0, Curve::Log, Disp::Ms));
        v.push_back (real (kEnvDecay, "Envelope Decay", "Decay", 1.0, 5000.0, 300.0, Curve::Log, Disp::Ms));
        // no note tracking since 0.6: these four are kept (unused) so old projects load
        v.push_back (percent (kKey, "Key Tracking (unused)", "Key", 1.0));
        v.push_back (integer (kTranspose, "Transpose (unused)", "Transpose", -48.0, 48.0, 0.0, Disp::Semis));
        v.push_back (integer (kPbRange, "Pitch Bend Range (unused)", "Bend", 0.0, 24.0, 2.0, Disp::Semis));
        v.push_back (integer (kRoot, "Root Note (unused)", "Root", 0.0, 127.0, kDefaultRoot, Disp::Note));
        v.push_back (percent (kDryWet, "Dry/Wet", "Dry/Wet", 1.0));
        v.push_back (real (kOutput, "Output", "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kHpGain, "High-Pass Gain", "HP Gain", kGainMinDb, 12.0, 0.0, Curve::Linear, Disp::DbGain));
        v.push_back (real (kLpGain, "Low-Pass Gain", "LP Gain", kGainMinDb, 12.0, 0.0, Curve::Linear, Disp::DbGain));
        v.push_back (toggle (kResLink, "Link Resonance", "Link Res", false));
        v.push_back (choice (kMovement, "Movement", "Movement", {"Free", "Vocal"}, kFree));
        pk::addTailParams (v, kTailBase);
        v.push_back (toggle (kDragGain, "Drag Gain", "Drag Gain", false));
        v.push_back (toggle (kLiquid, "Liquid (unused)", "Liquid", false));
        v.push_back (real (kFade, "Vocal Fade", "Fade", 1.0, 36.0, 12.0, Curve::Log, Disp::Semis));
        v.push_back (toggle (kNotch, "Notch (unused)", "Notch", false));
        v.push_back (real (kDipStart, "Vocal Dip Start", "Dip", 20.0, 1000.0, 80.0, Curve::Log, Disp::Hz));
        v.push_back (real (kLpFloor, "Low-Pass Floor", "Floor", 20.0, 500.0, 40.0, Curve::Log, Disp::Hz));
        smacheratr::addTailExtParams (v, kTailExtBase);
        // off, 0 dB and Pre are all normalized 0: where the values were never stored (Smemplr's rack
        // slots from before) they read as the defaults
        v.push_back (toggle (kHpDriveOn, "High-Pass Drive On", "HP Drive On", false));
        v.push_back (real (kHpDrive, "High-Pass Drive", "HP Drive", 0.0, 36.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (choice (kDrivePos, "Drive Position", "Drive Pos", {"Pre", "Post"}, kDrivePre));
        smacheratr::addTailExt2Params (v, kTailExt2Base);
        // off and 0 dB are normalized 0 too (a rack slot's places that never held them: see upgradeToPerBandDrive)
        v.push_back (toggle (kLpDriveOn, "Low-Pass Drive On", "LP Drive On", false));
        v.push_back (real (kLpDrive, "Low-Pass Drive", "LP Drive", 0.0, 36.0, 0.0, Curve::Linear, Disp::Db));
        return v;
    }());
    return t;
}

double slopeFromThreeChoices (double oldNorm)
{
    // 12, 18, 24 dB: the second, third and fourth slopes now
    const double index = std::round (std::fmin (std::fmax (oldNorm, 0.0), 1.0) * 2.0) + (double)kSlope12;
    return toNormalized (kSlope, index);
}

void upgradeToPerBandDrive (const std::function<bool (uint32_t, double&)>& get, const std::function<void (uint32_t, double)>& set)
{
    double v = 0.0;
    if (get (kSlope, v))
        set (kSlope, slopeFromThreeChoices (v));
    // the one drive was on the input of both filters (Pre) or on their sum (Post): now each filter has
    // one, both as the old one was (never saved: off, 0 dB)
    double on = 0.0, amount = 0.0;
    if (!get (kHpDriveOn, on))
        on = 0.0;
    if (!get (kHpDrive, amount))
        amount = 0.0;
    set (kLpDriveOn, on);
    set (kLpDrive, amount);
}

} // namespace para
