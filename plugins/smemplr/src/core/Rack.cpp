#include "Rack.h"

#include "smacheratr/src/core/TailExt.h"

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

static_assert (kSlotBlock >= para::kNumParams && kSlotBlock >= widr::kNumParams && kSlotBlock >= smacheratr::kNumParams &&
                   kSlotBlock >= mseq::kNumParams,
               "a slot's block must hold every effect's parameters");
// Wubr in a slot: its own IDs without its end saturator (5 .. 10, 85 .. 101 and its last block,
// Gently's Advanced mode), in order, then the ones after the saturator's block (Link Rates)
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
static_assert (multidyn::kNumParams == kSlotBlock + 2 + pk::kTailExtFields + pk::kTailExt2Fields && multidyn::kRmsWindow == kSlotBlock &&
                   multidyn::kSoften == kSlotBlock + 1 && multidyn::kSatPreLimitThreshold == kSlotBlock - 1 &&
                   multidyn::kSatExtBase == kSlotBlock + 2,
               "Multidyn grew: give its new parameters places in the block");

int64_t fxIdAt (int type, uint32_t j)
{
    if (type == kFxWubr)
        return wubrIdAt (j);
    if (type == kFxMultidyn)
    {
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
        {para::kLiquid, para::kLiquid, "unused: Vocal movement is what Liquid was"},
        {para::kNotch, para::kNotch, "unused: Liquid's notch is gone"},
    };
    static const std::vector<RackHidden> multidynHidden {
        {multidyn::kScOn, multidyn::kScListen, "the side-chain: Smemplr has no side-chain input"},
        {multidyn::kMode, multidyn::kMode, "unused: Multidyn always works in its character mode"},
        {multidyn::kSatOn, multidyn::kSatPreLimitThreshold, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {multidyn::kSatExtBase, multidyn::kSatExtBase + pk::kTailExtFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {multidyn::kSatExt2Base, multidyn::kSatExt2Base + pk::kTailExt2Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
    };
    static const std::vector<RackHidden> widrHidden {
        {widr::kRole, widr::kGroup, "Mix Aware: between Widr plug-ins on different tracks, not inside Smemplr"},
        {widr::kTailBase, widr::kTailBase + pk::kTailFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {widr::kTailExtBase, widr::kTailExtBase + pk::kTailExtFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {widr::kTailExt2Base, widr::kTailExt2Base + pk::kTailExt2Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
    };
    static const std::vector<RackHidden> levlrHidden {
        {levlr::kTailBase, levlr::kTailBase + pk::kTailFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {levlr::kTailExtBase, levlr::kTailExtBase + pk::kTailExtFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {levlr::kTailExt2Base, levlr::kTailExt2Base + pk::kTailExt2Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
    };
    static_assert (levlr::kNumParams <= kSlotBlock, "Levlr's parameters must fit a slot's block");
    static const std::vector<RackHidden> wubrHidden {
        {wubr::kTailBase, wubr::kTailBase + pk::kTailFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {wubr::kTailExtBase, wubr::kTailExtBase + pk::kTailExtFields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        {wubr::kTailExt2Base, wubr::kTailExt2Base + pk::kTailExt2Fields - 1, "its own end-of-chain saturator: in Smemplr a Smacheratr slot does that"},
        // each band's points and their count: drawn in the shape display (a ShapeView, not a control per value)
        {wubr::bandParam (0, wubr::kPointCount), wubr::bandParam (0, wubr::kPointCount), "the shape display adds and removes points"},
        {wubr::pointParam (0, 0, wubr::kPtX), wubr::pointParam (0, wubr::kMaxPoints - 1, wubr::kPtCurve), "drawn in the shape display"},
        {wubr::bandParam (1, wubr::kPointCount), wubr::bandParam (1, wubr::kPointCount), "the shape display adds and removes points"},
        {wubr::pointParam (1, 0, wubr::kPtX), wubr::pointParam (1, wubr::kMaxPoints - 1, wubr::kPtCurve), "drawn in the shape display"},
    };
    static const std::vector<RackHidden> smacheratrHidden {
        {smacheratr::kClarity2, smacheratr::kClarity2, "unused: one Gently button (a band works while its Range is above 0)"},
    };
    switch (type)
    {
        case kFxSmacheratr: return smacheratrHidden;
        case kFxPara: return paraHidden;
        case kFxMultidyn: return multidynHidden;
        case kFxWidr: return widrHidden;
        case kFxWubr: return wubrHidden;
        case kFxLevlr: return levlrHidden;
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
        default: return "";
    }
}

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
        default: return empty;
    }
}

void endSaturatorToSlot (int slot, const std::function<double (uint32_t)>& norm, const std::function<void (uint32_t, double)>& set)
{
    // Smacheratr's block positions are its own IDs; each one is a field of the old saturator (its On is
    // the slot's). The values go through their plain values, so the two tables need not agree on ranges.
    // The old saturator never had Gently's Advanced mode (the tail's third block): those get defaults.
    const auto& st = smacheratr::paramTable ();
    static_assert (smacheratr::kNumParams <= kSlotBlock, "Smacheratr's parameters sit in a slot's block");
    set (slotParam (slot, kSlotType), toNormalized (slotParam (slot, kSlotType), (double)kFxSmacheratr));
    set (slotParam (slot, kSlotOn), 1.0);
    for (uint32_t j = 0; j < kSlotBlockAll; ++j)
    {
        double v = 0.0;
        if (j < st.size ())
        {
            int f = smacheratr::tailFieldOf (j);
            if (f >= (int)(pk::kTailFields + pk::kTailExtFields))
                f = -1;
            const uint32_t id = f < 0 ? 0 : (f < (int)pk::kTailFields ? kTailBase + (uint32_t)f : kTailExtBase + (uint32_t)(f - pk::kTailFields));
            v = f < 0 ? st.defaultNormalized (j) : st.toNormalized (j, toPlain (id, norm (id)));
        }
        set (slotBlockParam (slot, j), v);
    }
}

int slotAfterChain (const std::function<int (int)>& typeOf)
{
    int last = -1;
    for (int s = 0; s < kRackSlots; ++s)
        if (typeOf (s) != kFxEmpty)
            last = s;
    return last + 1 < kRackSlots ? last + 1 : -1;
}

void moveEndSaturatorIntoRack (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has)
{
    for (uint32_t id = 0; id < kNumParams; ++id)
        if (isRackParam (id) && !has[id])
        {
            norm[id] = rackField (id).field == kSlotOn ? 1.0 : 0.0; // an empty slot
            has[id] = true;
        }
    const uint32_t onId = kTailBase + pk::kTailOn;
    const bool on = has[onId] ? norm[onId] >= 0.5 : true;
    has[onId] = true;
    norm[onId] = on ? 1.0 : 0.0;
    if (!on)
        return;
    const int slot = slotAfterChain ([&] (int s) {
        const uint32_t typeId = slotParam (s, kSlotType);
        return (int)std::lround (toPlain (typeId, norm[typeId]));
    });
    if (slot < 0)
        return;
    endSaturatorToSlot (
        slot, [&] (uint32_t id) { return has[id] ? norm[id] : defaultNormalized (id); },
        [&] (uint32_t id, double v) {
            norm[id] = v;
            has[id] = true;
        });
    norm[onId] = 0.0;
}

Rack::Rack ()
{
    for (auto& s : slots)
        s = std::make_unique<Slot> ();
}

void Rack::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    for (auto& s : slots)
    {
        s->para.prepare (sr, maxBlock);
        s->multidyn.prepare (sr, maxBlock);
        s->ms.prepare (sr);
        s->sat.prepare (sr, maxBlock);
        s->widr.prepare (sr, maxBlock);
        s->wubr.prepare (sr, maxBlock);
        s->levlr.prepare (sr, maxBlock);
        applyAll (*s);
    }
}

void Rack::reset ()
{
    for (auto& s : slots)
    {
        s->para.reset ();
        s->multidyn.reset ();
        s->ms.reset ();
        s->sat.reset ();
        s->widr.reset ();
        s->wubr.reset ();
        s->levlr.reset ();
    }
}

void Rack::setMeters (RackMeters* m)
{
    meters = m;
    for (int i = 0; i < kRackSlots; ++i)
    {
        Slot& s = *slots[(size_t)i];
        s.para.setMeters (m ? &m->para[(size_t)i] : nullptr);
        s.sat.setMeters (m ? &m->sat[(size_t)i] : nullptr);
        s.widr.setMeters (m ? &m->widr[(size_t)i] : nullptr);
        s.wubr.setMeters (m ? &m->wubr[(size_t)i] : nullptr);
        s.levlr.setMeters (m ? &m->levlr[(size_t)i] : nullptr);
    }
}

void Rack::apply (Slot& s, uint32_t block)
{
    const auto& t = fxBlockTable (s.type);
    const int64_t id = fxIdAt (s.type, block);
    if (block >= t.size () || id < 0)
        return;
    const double v = t.toPlain (block, std::clamp (s.norm[block], 0.0, 1.0));
    const auto j = (uint32_t)id;
    switch (s.type)
    {
        case kFxPara: s.para.setParam (j, v); break;
        case kFxMultidyn: s.multidyn.setParam (j, v); break;
        case kFxSmacheratr:
            // off: fully dry (it keeps its latency)
            s.sat.setParam (j, j == smacheratr::kDryWet && !s.on ? 0.0 : v);
            break;
        case kFxWidr: s.widr.setParam (j, v); break;
        case kFxWubr: s.wubr.setParam (j, v); break;
        case kFxLevlr: s.levlr.setParam (j, v); break;
        default: break; // the M/S EQ reads its values when it runs
    }
}

void Rack::applyAll (Slot& s)
{
    for (uint32_t j = 0; j < kSlotBlockAll; ++j)
        apply (s, j);
    s.multidyn.setBypass (!s.on);
    switch (s.type)
    {
        case kFxPara: s.para.reset (); break;
        case kFxMultidyn: s.multidyn.reset (); break;
        case kFxMsEq: s.ms.reset (); break;
        case kFxSmacheratr: s.sat.reset (); break;
        case kFxWidr: s.widr.reset (); break;
        case kFxWubr: s.wubr.reset (); break;
        case kFxLevlr: s.levlr.reset (); break;
        default: break;
    }
}

void Rack::setParam (uint32_t id, double plain)
{
    if (!isRackParam (id))
        return;
    const RackField rf = rackField (id);
    Slot& s = *slots[(size_t)rf.slot];
    const uint32_t field = rf.field;
    if (field == kSlotType)
    {
        const int t = std::clamp ((int)std::lround (plain), 0, kNumFxTypes - 1);
        if (t != s.type)
        {
            s.type = t;
            applyAll (s);
        }
    }
    else if (field == kSlotOn)
    {
        s.on = plain >= 0.5;
        s.multidyn.setBypass (!s.on);
        if (s.type == kFxSmacheratr)
            apply (s, smacheratr::kDryWet);
    }
    else
    {
        const uint32_t j = field - kSlotParams;
        s.norm[j] = plain;
        apply (s, j);
    }
}

int Rack::latency () const
{
    int l = 0;
    for (const auto& s : slots)
        if (s->type == kFxMultidyn)
            l += s->multidyn.latency ();
        else if (s->type == kFxSmacheratr)
            l += s->sat.latency ();
        else if (s->type == kFxPara)
            l += s->para.latency (); // its drive's oversampling, on or off
    return l;
}

void Rack::noteOn (int note)
{
    for (auto& s : slots)
        if (s->type == kFxPara)
            s->para.noteOn (note);
        else if (s->type == kFxWubr)
            s->wubr.noteOn (note);
}

void Rack::noteOff (int note)
{
    for (auto& s : slots)
        s->wubr.noteOff (note); // every slot, so a Wubr loaded while a note is down does not hang on to it
}

void Rack::allNotesOff ()
{
    for (auto& s : slots)
        s->wubr.allNotesOff ();
}

void Rack::setTransport (double bpm, double ppq, bool playing)
{
    for (auto& s : slots)
        if (s->type == kFxWubr)
            s->wubr.setTransport (bpm, ppq, playing);
}

void Rack::setPitchBend (float bipolar)
{
    for (auto& s : slots)
        s->para.setPitchBend (bipolar);
}

void Rack::process (float* L, float* R, int n)
{
    for (int i = 0; i < kRackSlots; ++i)
    {
        Slot& s = *slots[(size_t)i];
        switch (s.type)
        {
            case kFxPara:
                if (s.on)
                    s.para.process (L, R, L, R, n);
                else
                    s.para.processBypassed (L, R, n); // off: its latency's delay only
                break;
            case kFxMultidyn: s.multidyn.process (L, R, nullptr, nullptr, L, R, n); break; // bypassed: delay only
            case kFxMsEq:
            {
                const auto& t = mseq::paramTable ();
                auto plain = [&] (uint32_t j) { return t.toPlain (j, std::clamp (s.norm[j], 0.0, 1.0)); };
                if (s.on)
                    s.ms.process (L, R, n, plain (mseq::kSideHp), (int)std::lround (plain (mseq::kSlope)),
                                  plain (mseq::kSideGain), plain (mseq::kMidGain));
                else
                    s.ms.measure (L, R, n);
                break;
            }
            case kFxSmacheratr: s.sat.process (L, R, L, R, n); break; // off: fully dry, same latency
            case kFxWidr:
                if (s.on)
                    s.widr.process (L, R, L, R, n);
                break;
            case kFxWubr:
                if (s.on)
                    s.wubr.process (L, R, L, R, n);
                break;
            case kFxLevlr: // (no latency without its own saturator: off simply passes)
                if (s.on)
                    s.levlr.process (L, R, L, R, n);
                break;
            default: break;
        }
        publish (i);
    }
}

void Rack::publish (int i)
{
    if (!meters)
        return;
    Slot& s = *slots[(size_t)i];
    if (s.type == kFxMultidyn)
        for (int b = 0; b < multidyn::kNumBands; ++b)
        {
            const auto& m = s.multidyn.meter (b);
            meters->multidyn[(size_t)i].inputDb[(size_t)b].store (m.inputDb, std::memory_order_relaxed);
            meters->multidyn[(size_t)i].outputDb[(size_t)b].store (m.outputDb, std::memory_order_relaxed);
            meters->multidyn[(size_t)i].gainDb[(size_t)b].store (m.gainDb, std::memory_order_relaxed);
        }
    else if (s.type == kFxMsEq)
    {
        meters->msMid[(size_t)i].store (s.ms.midPeak, std::memory_order_relaxed);
        meters->msSide[(size_t)i].store (s.ms.sidePeak, std::memory_order_relaxed);
    }
}

} // namespace smemplr
