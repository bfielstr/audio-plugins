#include "FxSlot.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace smemplr {

namespace mseq {
const pk::ParamTable& paramTable ()
{
    using namespace pk;
    using namespace pk::make;
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        v.push_back (real (kSideHp, "Side High-Pass", "Side HP", 20.0, 2000.0, 150.0, Curve::Log, Disp::Hz));
        // (3 choices, 6 / 12 / 24 dB, in states before version 9: StateIO.cpp converts them)
        v.push_back (choice (kSlope, "Side High-Pass Slope", "Slope",
                             {"6 dB", "12 dB", "24 dB", "36 dB", "48 dB", "60 dB", "72 dB", "84 dB", "96 dB", "Brickwall"}, MsEq::k24));
        v.push_back (real (kSideGain, "Side Gain", "Side", -24.0, 12.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kMidGain, "Mid Gain", "Mid", -24.0, 12.0, 0.0, Curve::Linear, Disp::Db));
        return v;
    }());
    return t;
}

double slopeFromThreeChoices (double oldNorm)
{
    const double index = std::round (std::clamp (oldNorm, 0.0, 1.0) * 2.0); // 6, 12, 24 dB: the first three
    return paramTable ().toNormalized (kSlope, index);
}
} // namespace mseq

static_assert (kSlotBlock >= widr::kNumParams && kSlotBlock >= smacheratr::kNumParams && kSlotBlock >= mseq::kNumParams,
               "a slot's block must hold every effect's parameters");
// Para in a slot: its own IDs as block positions, all of them; the ones after its low-pass drive (Low-Pass
// Slope, the Gain Locks) are the first positions of the slot's extension
static_assert (para::kLpDrive == kSlotBlock - 1 && para::kLpSlope == kSlotBlock && para::kHpGainLock == kSlotBlock + 1 &&
                   para::kLpGainLock == kSlotBlock + 2 && para::kNumParams <= kSlotBlockAll,
               "Para's parameters must fit a slot's block and extension (block position = Para's ID)");
// Wubr in a slot: its own IDs without its end saturator (5 .. 10, 85 .. 101 and its last block,
// Gentlr's Advanced mode), in order, then the ones after the saturator's block (Link Rates)
constexpr uint32_t kWubrBands = wubr::kTailBase + (wubr::kTailExtBase - wubr::kBandBase); // positions before Link Rates
constexpr uint32_t kWubrHosted = kWubrBands + (wubr::kTailExt2Base - wubr::kLinkRate);
static_assert (wubr::kTailBase == 5 && kWubrHosted <= kSlotBlockAll, "Wubr's parameters must fit a slot's block and extension");
static int64_t wubrIdAt (uint32_t j)
{
    if (j < wubr::kTailBase)
        return j;
    if (j < kWubrBands)
        return (int64_t)(j - wubr::kTailBase + wubr::kBandBase);
    return j < kWubrHosted ? (int64_t)(j - kWubrBands + wubr::kLinkRate) : -1;
}
static int64_t wubrBlockOf (uint32_t id)
{
    if (id < wubr::kTailBase)
        return id;
    if (id >= wubr::kBandBase && id < wubr::kTailExtBase)
        return (int64_t)(id - wubr::kBandBase + wubr::kTailBase);
    return id >= wubr::kLinkRate && id < wubr::kTailExt2Base ? (int64_t)(id - wubr::kLinkRate + kWubrBands) : -1;
}
// Multidyn's later parameters take the places of its saturator's (see fxBlockTable)
static_assert (multidyn::kXoverSlope == kSlotBlock + 2 + pk::kTailExtFields + pk::kTailExt2Fields && multidyn::kRmsWindow == kSlotBlock &&
                   multidyn::kSoften == kSlotBlock + 1 && multidyn::kSatPreLimitThreshold == kSlotBlock - 1 &&
                   multidyn::kSatExtBase == kSlotBlock + 2,
               "Multidyn grew: give its new parameters places in the block");
// ... and the ones after its saturator's blocks (Slope, Soften Color, the Sub band, Style, Sub Input) run on into the
// slot's extension, in order (its saturator's fourth block after them is not in the rack either)
constexpr uint32_t kMdAdded = multidyn::kSatExt3Base - multidyn::kXoverSlope;
static_assert (kMdAdded == 11 && kSlotBlock + kMdAdded <= kSlotBlockAll, "Multidyn's later parameters must fit a slot's extension");

int64_t fxIdAt (int type, uint32_t j)
{
    if (type == kFxWubr)
        return wubrIdAt (j);
    if (type == kFxMultidyn)
    {
        if (j >= kSlotBlock)
            return j < kSlotBlock + kMdAdded ? (int64_t)(multidyn::kXoverSlope + (j - kSlotBlock)) : -1;
        if (j == multidyn::kSatOn)
            return multidyn::kRmsWindow;
        if (j == multidyn::kSatPreLimit)
            return multidyn::kSoften;
        if (j > multidyn::kSatPreLimit && j <= multidyn::kSatPreLimitThreshold)
            return -1;
    }
    return j < fxBlockTable (type).size () ? (int64_t)j : -1;
}

int64_t fxBlockOf (int type, uint32_t id)
{
    if (type == kFxWubr)
        return wubrBlockOf (id);
    if (type == kFxMultidyn)
    {
        if (id >= multidyn::kXoverSlope)
            return id < multidyn::kSatExt3Base ? (int64_t)(kSlotBlock + (id - multidyn::kXoverSlope)) : -1;
        if (id == multidyn::kRmsWindow)
            return multidyn::kSatOn;
        if (id == multidyn::kSoften)
            return multidyn::kSatPreLimit;
        if ((id >= multidyn::kSatOn && id <= multidyn::kSatPreLimitThreshold) || id >= multidyn::kSatExtBase)
            return -1; // its own saturator is not in the rack
    }
    return id < fxBlockTable (type).size () ? (int64_t)id : -1;
}

const std::vector<RackHidden>& rackHiddenParams (int type)
{
    static const std::vector<RackHidden> none;
    static const std::vector<RackHidden> paraHidden {
        {para::kKey, para::kKey, "unused since Para stopped tracking notes"},
        {para::kTranspose, para::kTranspose, "unused since Para stopped tracking notes"},
        {para::kPbRange, para::kPbRange, "unused since Para stopped tracking notes"},
        {para::kRoot, para::kRoot, "unused since Para stopped tracking notes"},
        {para::kTailBase, para::kTailBase + pk::kTailFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {para::kTailExtBase, para::kTailExtBase + pk::kTailExtFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {para::kTailExt2Base, para::kTailExt2Base + pk::kTailExt2Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {para::kTailExt3Base, para::kTailExt3Base + pk::kTailExt3Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {para::kTailExt4Base, para::kTailExt4Base + pk::kTailExt4Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {para::kLiquid, para::kLiquid, "unused: Vocal movement is what Liquid was"},
        {para::kNotch, para::kNotch, "unused: Liquid's notch is gone"},
    };
    static const std::vector<RackHidden> multidynHidden {
        {multidyn::kScOn, multidyn::kScListen, "the side-chain: Smemplr has no side-chain input"},
        {multidyn::kMode, multidyn::kMode, "unused: Multidyn always works in its character mode"},
        {multidyn::kSatOn, multidyn::kSatPreLimitThreshold, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {multidyn::kSatExtBase, multidyn::kSatExtBase + pk::kTailExtFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {multidyn::kSatExt2Base, multidyn::kSatExt2Base + pk::kTailExt2Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {multidyn::kSatExt3Base, multidyn::kSatExt3Base + pk::kTailExt3Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {multidyn::kSatExt4Base, multidyn::kSatExt4Base + pk::kTailExt4Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
    };
    static const std::vector<RackHidden> widrHidden {
        {widr::kRole, widr::kGroup, "Mix Aware: between Widr plug-ins on different tracks, not inside Smemplr"},
        {widr::kTailBase, widr::kTailBase + pk::kTailFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {widr::kTailExtBase, widr::kTailExtBase + pk::kTailExtFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {widr::kTailExt2Base, widr::kTailExt2Base + pk::kTailExt2Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {widr::kTailExt3Base, widr::kTailExt3Base + pk::kTailExt3Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {widr::kTailExt4Base, widr::kTailExt4Base + pk::kTailExt4Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
    };
    static const std::vector<RackHidden> levlrHidden {
        {levlr::kTailBase, levlr::kTailBase + pk::kTailFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {levlr::kTailExtBase, levlr::kTailExtBase + pk::kTailExtFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {levlr::kTailExt2Base, levlr::kTailExt2Base + pk::kTailExt2Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {levlr::kTailExt3Base, levlr::kTailExt3Base + pk::kTailExt3Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {levlr::kTailExt4Base, levlr::kTailExt4Base + pk::kTailExt4Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
    };
    // (its saturator's fourth block runs on into the slot's extension: not in the rack)
    static_assert (levlr::kTailExt3Base <= kSlotBlock && levlr::kNumParams <= kSlotBlockAll, "Levlr's parameters must fit a slot's block");
    static const std::vector<RackHidden> wubrHidden {
        {wubr::kTailBase, wubr::kTailBase + pk::kTailFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {wubr::kTailExtBase, wubr::kTailExtBase + pk::kTailExtFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {wubr::kTailExt2Base, wubr::kTailExt2Base + pk::kTailExt2Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {wubr::kTailExt3Base, wubr::kTailExt3Base + pk::kTailExt3Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {wubr::kTailExt4Base, wubr::kTailExt4Base + pk::kTailExt4Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        // each band's points and their count: drawn in the shape display (a ShapeView, not a control per value)
        {wubr::bandParam (0, wubr::kPointCount), wubr::bandParam (0, wubr::kPointCount), "the shape display adds and removes points"},
        {wubr::pointParam (0, 0, wubr::kPtX), wubr::pointParam (0, wubr::kMaxPoints - 1, wubr::kPtCurve), "drawn in the shape display"},
        {wubr::bandParam (1, wubr::kPointCount), wubr::bandParam (1, wubr::kPointCount), "the shape display adds and removes points"},
        {wubr::pointParam (1, 0, wubr::kPtX), wubr::pointParam (1, wubr::kMaxPoints - 1, wubr::kPtCurve), "drawn in the shape display"},
    };
    // Smoothr's saturator before its limiter: a Smacheratr slot before it does that (in the rack it is off)
    static const std::vector<RackHidden> smoothrHidden {
        {smoothr::kTailBase, smoothr::kTailBase + pk::kTailFields - 1, "its own saturator before the limiter: in Smemplr a Smacheratr slot before it does that"},
        {smoothr::kTailExtBase, smoothr::kTailExtBase + pk::kTailExtFields - 1, "its own saturator before the limiter: in Smemplr a Smacheratr slot before it does that"},
        {smoothr::kTailExt2Base, smoothr::kTailExt2Base + pk::kTailExt2Fields - 1, "its own saturator before the limiter: in Smemplr a Smacheratr slot before it does that"},
        {smoothr::kTailExt3Base, smoothr::kTailExt3Base + pk::kTailExt3Fields - 1, "its own saturator before the limiter: in Smemplr a Smacheratr slot before it does that"},
        {smoothr::kTailExt4Base, smoothr::kTailExt4Base + pk::kTailExt4Fields - 1, "its own saturator before the limiter: in Smemplr a Smacheratr slot before it does that"},
    };
    static_assert (smoothr::kNumParams <= kSlotBlock, "Smoothr's parameters must fit a slot's block");
    static const std::vector<RackHidden> gentlrHidden {
        {gentlr::kSubOn, gentlr::kSubOn, "unused: the Sub band works while its Range is above 0"},
        {gentlr::kTailBase, gentlr::kHighOn - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {gentlr::kHighOn, gentlr::kHighOn, "unused: the High band works while its Range is above 0"},
        {gentlr::kTailExt3Base, gentlr::kTailExt3Base + pk::kTailExt3Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {gentlr::kTailExt4Base, gentlr::kTailExt4Base + pk::kTailExt4Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
    };
    // (its saturator's fourth block runs on into the slot's extension: not in the rack; its Slope and glue
    // switches after it are, in the extension too; its saturator's fifth block after those is not)
    static_assert (gentlr::kTailExt3Base <= kSlotBlock && gentlr::kNumParams <= kSlotBlockAll, "Gentlr's parameters must fit a slot's block");
    static const std::vector<RackHidden> smacheratrHidden {
        {smacheratr::kClarity2, smacheratr::kClarity2, "unused: one Gentlr button (a band works while its Range is above 0)"},
        {smacheratr::kClaritySub, smacheratr::kClaritySub, "unused: the Sub band works while its Range is above 0"},
        {smacheratr::kClarityHigh, smacheratr::kClarityHigh, "unused: the High band works while its Range is above 0"},
    };
    switch (type)
    {
        case kFxSmacheratr: return smacheratrHidden;
        case kFxPara: return paraHidden;
        case kFxMultidyn: return multidynHidden;
        case kFxWidr: return widrHidden;
        case kFxWubr: return wubrHidden;
        case kFxLevlr: return levlrHidden;
        case kFxSmoothr: return smoothrHidden;
        case kFxGentlr: return gentlrHidden;
        default: return none;
    }
}

const pk::ParamTable& fxBlockTable (int type)
{
    if (type == kFxWubr)
    {
        static const pk::ParamTable t ([] {
            std::vector<pk::ParamInfo> v;
            for (uint32_t j = 0; j < kWubrHosted; ++j)
            {
                pk::ParamInfo pi = wubr::paramTable ().info ((uint32_t)wubrIdAt (j));
                pi.id = j;
                v.push_back (pi);
            }
            return v;
        }());
        return t;
    }
    if (type != kFxMultidyn)
        return fxTable (type);
    static const pk::ParamTable t ([] {
        const auto& md = multidyn::paramTable ();
        std::vector<pk::ParamInfo> v;
        for (uint32_t j = 0; j < kSlotBlock; ++j)
        {
            // the saturator's places that nothing uses keep its entries
            uint32_t id = j;
            if (j == multidyn::kSatOn)
                id = multidyn::kRmsWindow;
            else if (j == multidyn::kSatPreLimit)
                id = multidyn::kSoften;
            pk::ParamInfo pi = md.info (id);
            pi.id = j;
            v.push_back (pi);
        }
        for (uint32_t j = kSlotBlock; j < kSlotBlock + kMdAdded; ++j)
        {
            pk::ParamInfo pi = md.info (multidyn::kXoverSlope + (j - kSlotBlock));
            pi.id = j;
            v.push_back (pi);
        }
        return v;
    }());
    return t;
}

const char* fxName (int type)
{
    switch (type)
    {
        case kFxPara: return "para";
        case kFxMultidyn: return "multidyn";
        case kFxMsEq: return "m/s eq";
        case kFxSmacheratr: return "smacheratr";
        case kFxWidr: return "widr";
        case kFxWubr: return "wubr";
        case kFxLevlr: return "levlr";
        case kFxGentlr: return "gentlr";
        case kFxSmoothr: return "smoothr";
        default: return "";
    }
}

const char* fxFormerName (int type) { return type == kFxGentlr ? "gently" : ""; }

const pk::ParamTable& fxTable (int type)
{
    static const pk::ParamTable empty (std::vector<pk::ParamInfo> {});
    switch (type)
    {
        case kFxPara: return para::paramTable ();
        case kFxMultidyn: return multidyn::paramTable ();
        case kFxMsEq: return mseq::paramTable ();
        case kFxSmacheratr: return smacheratr::paramTable ();
        case kFxWidr: return widr::paramTable ();
        case kFxWubr: return wubr::paramTable ();
        case kFxLevlr: return levlr::paramTable ();
        case kFxSmoothr: return smoothr::paramTable ();
        case kFxGentlr: return gentlr::paramTable ();
        default: return empty;
    }
}

void FxSlot::prepare (double sampleRate, int maxBlock)
{
    para.prepare (sampleRate, maxBlock);
    multidyn.prepare (sampleRate, maxBlock);
    ms.prepare (sampleRate);
    sat.prepare (sampleRate, maxBlock);
    widr.prepare (sampleRate, maxBlock);
    wubr.prepare (sampleRate, maxBlock);
    levlr.prepare (sampleRate, maxBlock);
    smoothr.prepare (sampleRate, maxBlock);
    gentlr.prepare (sampleRate, maxBlock);
    applyAll ();
}

void FxSlot::reset ()
{
    para.reset ();
    multidyn.reset ();
    ms.reset ();
    sat.reset ();
    widr.reset ();
    wubr.reset ();
    levlr.reset ();
    smoothr.reset ();
    gentlr.reset ();
}

void FxSlot::apply (uint32_t block)
{
    const auto& t = fxBlockTable (type);
    const int64_t id = fxIdAt (type, block);
    if (block >= t.size () || id < 0)
        return;
    const double v = t.toPlain (block, std::clamp (norm[block], 0.0, 1.0));
    const auto j = (uint32_t)id;
    switch (type)
    {
        case kFxPara: para.setParam (j, v); break;
        case kFxMultidyn: multidyn.setParam (j, v); break;
        case kFxSmacheratr:
            // off: fully dry (it keeps its latency)
            sat.setParam (j, j == smacheratr::kDryWet && !on ? 0.0 : v);
            break;
        case kFxWidr: widr.setParam (j, v); break;
        case kFxWubr: wubr.setParam (j, v); break;
        case kFxLevlr: levlr.setParam (j, v); break;
        case kFxSmoothr:
            // its own saturator stays off in the rack (a Smacheratr slot before it does that)
            smoothr.setParam (j, j == smoothr::kTailBase + pk::kTailOn ? 0.0 : v);
            break;
        case kFxGentlr:
            // off: fully dry (its dry path is delayed to its latency)
            gentlr.setParam (j, j == gentlr::kMix && !on ? 0.0 : v);
            break;
        default: break; // the M/S EQ reads its values when it runs
    }
}

void FxSlot::applyAll ()
{
    for (uint32_t j = 0; j < kSlotBlockAll; ++j)
        apply (j);
    multidyn.setBypass (!on);
    switch (type)
    {
        case kFxPara: para.reset (); break;
        case kFxMultidyn: multidyn.reset (); break;
        case kFxMsEq: ms.reset (); break;
        case kFxSmacheratr: sat.reset (); break;
        case kFxWidr: widr.reset (); break;
        case kFxWubr: wubr.reset (); break;
        case kFxLevlr: levlr.reset (); break;
        case kFxSmoothr: smoothr.reset (); break;
        case kFxGentlr: gentlr.reset (); break;
        default: break;
    }
}

void FxSlot::setType (int t)
{
    t = std::clamp (t, 0, kNumFxTypes - 1);
    if (t != type)
    {
        type = t;
        applyAll ();
    }
}

void FxSlot::setOn (bool isOn)
{
    on = isOn;
    multidyn.setBypass (!on);
    if (type == kFxSmacheratr)
        apply (smacheratr::kDryWet);
    else if (type == kFxGentlr)
        apply (gentlr::kMix);
}

void FxSlot::setValue (uint32_t block, double normalized)
{
    if (block >= kSlotBlockAll)
        return;
    norm[block] = normalized;
    apply (block);
}

int FxSlot::latency () const
{
    switch (type)
    {
        case kFxMultidyn: return multidyn.latency ();
        case kFxSmacheratr: return sat.latency ();
        case kFxPara: return para.latency ();       // its drive's oversampling, on or off
        case kFxLevlr: return levlr.latency ();     // its drives' oversampling, on or off
        case kFxGentlr: return gentlr.latency ();   // its region Drive's oversampler, always in the path
        case kFxSmoothr: return smoothr.latency (); // the limiter's look-ahead (and its saturator's, off), on or off
        default: return 0;
    }
}

void FxSlot::noteOn (int note)
{
    if (type == kFxPara)
        para.noteOn (note);
    else if (type == kFxWubr)
        wubr.noteOn (note);
}

// (whatever the kind, so a Wubr loaded while a note is down does not hang on to it)
void FxSlot::noteOff (int note) { wubr.noteOff (note); }

void FxSlot::allNotesOff () { wubr.allNotesOff (); }

void FxSlot::setTransport (double bpm, double ppq, bool playing)
{
    if (type == kFxWubr)
        wubr.setTransport (bpm, ppq, playing);
}

void FxSlot::setPitchBend (float bipolar) { para.setPitchBend (bipolar); }

void FxSlot::process (float* L, float* R, int n)
{
    switch (type)
    {
        case kFxPara:
            if (on)
                para.process (L, R, L, R, n);
            else
                para.processBypassed (L, R, n); // off: its latency's delay only
            break;
        case kFxMultidyn: multidyn.process (L, R, nullptr, nullptr, L, R, n); break; // bypassed: delay only
        case kFxMsEq:
        {
            const auto& t = mseq::paramTable ();
            auto plain = [&] (uint32_t j) { return t.toPlain (j, std::clamp (norm[j], 0.0, 1.0)); };
            if (on)
                ms.process (L, R, n, plain (mseq::kSideHp), (int)std::lround (plain (mseq::kSlope)), plain (mseq::kSideGain),
                            plain (mseq::kMidGain));
            else
                ms.measure (L, R, n);
            break;
        }
        case kFxSmacheratr: sat.process (L, R, L, R, n); break; // off: fully dry, same latency
        case kFxWidr:
            if (on)
                widr.process (L, R, L, R, n);
            break;
        case kFxWubr:
            if (on)
                wubr.process (L, R, L, R, n);
            break;
        case kFxLevlr:
            if (on)
                levlr.process (L, R, L, R, n);
            else
                levlr.processBypassed (L, R, n); // off: its latency's delay only
            break;
        case kFxGentlr: gentlr.process (L, R, L, R, n); break; // off: fully dry, same latency
        case kFxSmoothr:
            if (on)
                smoothr.process (L, R, L, R, n);
            else
                smoothr.processBypassed (L, R, n); // off: its latency's delay only
            break;
        default: break;
    }
}

} // namespace smemplr
