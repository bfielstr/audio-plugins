#pragma once

#include "Params.h"

#include "pluginkit/EndClip.h"

namespace sunder::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kInput:
            return "The level into the compressor, -48 to +48 dB (finer around 0 dB; double-click for 0 dB). There is no "
                   "makeup gain after it: more Input compresses harder and is louder until the compression holds it.";
        case kRatio:
            return "How hard the compressor works, in steps 1 to 10: higher lowers the threshold and steepens the knee, as "
                   "the hardware's Ratio does (a position, not a fixed ratio).";
        case kCurve:
            return "The knee's diodes: R Soft (a resistor, the gentlest and cleanest), Germ Soft (one germanium "
                   "diode), Germ Med (five), Silicon Med (one silicon diode), Silicon Hard (two) and Zener Hard (the "
                   "sharpest and earliest).";
        case kAttack:
            return "How fast the compressor reacts, 0 (Fast, about 10 ms) to 10 (Slow, about 100 ms). Slower settles "
                   "with a little less gain reduction.";
        case kRelease:
            return "How fast the compressor lets go, 0 (Fast, about 30 ms) to 10 (Slow, about 300 ms from deep gain "
                   "reduction). Slower also holds a little more gain reduction.";
        case kScFilter:
            return "Takes the lows out of what the compressor listens to, so bass pumps it less: Off, then 30 to 300 "
                   "Hz (broad shelves; 300 Hz leaves it hearing mostly the mids and highs). The detector always "
                   "ignores the deepest lows.";
        case kMix:
            return "Blend of the compressed and the uncompressed signal (parallel compression). Both go through the "
                   "output stage, so Drive and Feedback colour either.";
        case kDrive:
            return "The germanium output stage's gain switch, in steps 1 to 10 (about 4.5 dB a step). Higher drive also "
                   "raises the low resonance and the top end's lift; the top steps push the stage further than the "
                   "measured hardware went.";
        case kFeedback:
            return "The output stage's feedback, in steps 1 to 10: up adds gain and a resonant bass bump (about 20 to 90 "
                   "Hz, higher with more Drive) with a steep cut below it, and makes Saturation warmer from lower levels. "
                   "The top steps go past the measured hardware's range.";
        case kSaturation:
            return "The germanium transistor in the output stage: an asymmetric, mostly second-harmonic saturation. "
                   "Higher sets it in at lower levels (100 %: from about -12 dBFS out of the stage). Feedback shapes "
                   "it: low Feedback stays clean, then breaks up more firmly; high Feedback is warm from lower levels. "
                   "0 %: off, the measured clean stage.";
        case kOutput: return "Overall output level.";
        case kLink: return "One detector for both channels (the louder sets the gain), so the image stays put. Off: each channel on its own.";
        case kMode: return "Stereo (left and right) or Mid/Side (the compressor and the output stage work on the mid and the side).";
        case kExtSc:
            return "The compressor listens to the sidechain input instead of the sound it compresses (route one to "
                   "sunder's sidechain in the host; without one, it listens to its input as usual).";
        case kScGain: return "The sidechain input's level into the compressor, -24 to +24 dB (0: as the input would be heard).";
        case kSoftClip: return pk::kSoftClipHelp;
        case kHardClip: return pk::kHardClipHelp;
        case kBass:
            return "The bass enhancer (ahead of the compressor, or after it with Bass Position at Post): a band of the "
                   "low end driven into a clipper and blended in (the BASS section shapes it). 0: off, nothing of it "
                   "runs. Up to 100 %: the effect comes in under the dry sound, both at full level at 100 % (on the "
                   "way it first thins the band, then builds it up).";
        case kBassBoost:
            return "How much of the bass effect is added, 0 to 110 %: 0.3 dB a percent, part into its clipper and part "
                   "after it. The part into the clipper is taken back as the band gets loud, so it lifts the quieter "
                   "passages most.";
        case kBassFreq:
            return "Where the bass effect sits, 20 to 125 Hz: the centre of the band it is made from. Tune it to the "
                   "sub or low end of the source.";
        case kBassIntensity:
            return "How hard the band drives the bass effect's clipper, from subtle (0 %: hardly clipping) to heavily "
                   "processed (100 %: some 36 dB more). The effect's ceiling stays where it is.";
        case kBassQ:
            return "The bass band's focus: low values spread the effect wide around Bass Freq, high values narrow it "
                   "onto Bass Freq (and drive the clipper harder there).";
        case kBassDrive: return "The gain after the bass effect's clipper, 0 to 100 % (about 5.7 dB): a louder effect at the same clipping.";
        case kBassDriveX2: return "Doubles Bass Drive's stage: about 9.6 dB at 100 % instead of 5.7, a little more distortion near the knee.";
        case kBassGate:
            return "The bass effect's gate, -60 to 0 dB, on the input's peak (both channels together): below it the "
                   "effect is turned down, so it only comes in on the louder parts.";
        case kBassRatio:
            return "How strongly the gate turns the bass effect down below Bass Gate: Ratio - 1 dB a dB (4: 3 dB a dB). "
                   "The middle of the knob is 10; from about 50 the gate shuts.";
        case kBassAttack: return "How fast the bass effect's gate opens once the input passes Bass Gate, 0 (at once) to 300 ms.";
        case kBassRelease: return "How fast the bass effect's gate closes when the input falls below Bass Gate, 20 to 500 ms.";
        case kBassLowCut:
            return "A fourth-order high-pass on the bass effect, 20 to 125 Hz (20 Hz: the lowest it goes). As Bass "
                   "comes in it filters the dry signal too (at 100 % fully).";
        case kBassOversample:
            return "Runs the bass effect's clipper at 2x or 4x the rate (less aliasing when it is driven hard), at the "
                   "cost of some latency and CPU; the latency stays while it is set, Bass at 0 included.";
        case kBassPosition:
            return "Where the bass enhancer sits. Pre: ahead of Input and the compressor, which then holds the added "
                   "bass too. Post: after the compressor and the germanium stage, ahead of Output, Soft Clip and Hard "
                   "Clip, so the compressor does not hear it and the bass comes out as set. A change crossfades.";
        default: return nullptr;
    }
}

constexpr const char* kSunderView =
    "At the left, the output stage's response (Drive and Feedback) from 10 Hz to 20 kHz, relative to its gain at "
    "1 kHz (shown in the corner). At the right, the compression curve for the Curve and Ratio: the level out "
    "against the level in, with the level reaching the detector now and the gain reduction while audio plays.";

} // namespace sunder::help
