#include "Rack.h"

#include "smacheratr/src/core/TailExt.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace smemplr {

void endSaturatorToSlot (int slot, const std::function<double (uint32_t)>& norm, const std::function<void (uint32_t, double)>& set)
{
    // Smacheratr's block positions are its own IDs; each one is a field of the old saturator (its On is
    // the slot's). The values go through their plain values, so the two tables need not agree on ranges.
    // The old saturator never had Gentlr's Advanced mode (the tail's third block): those get defaults,
    // but the band Slope gets Classic, the shape the old saturator's bands had (and still have).
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
            if (j == smacheratr::kClaritySlope)
                v = smacheratr::classicSlopeNorm ();
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

void migrateGentlrInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version)
{
    if (version >= 17)
        return;
    // Smacheratr's Gentlr parameters added since the state was saved: before 11 from Advanced on, before
    // 12 from the Sub band on, before 17 the High band and No Overlap
    const uint32_t first = version < 11 ? smacheratr::kClarityAdvanced : version < 12 ? smacheratr::kClaritySub : smacheratr::kClarityHigh;
    for (int slot = 0; slot < kRackSlots; ++slot)
    {
        const uint32_t typeId = slotParam (slot, kSlotType);
        if (!has[typeId])
            continue;
        const int type = (int)std::lround (toPlain (typeId, norm[typeId]));
        auto setDefaults = [&] (uint32_t from, uint32_t to, const pk::ParamTable& t) {
            for (uint32_t id = from; id < to; ++id)
            {
                norm[slotBlockParam (slot, id)] = t.defaultNormalized (id);
                has[slotBlockParam (slot, id)] = true;
            }
        };
        if (type == kFxSmacheratr)
            setDefaults (first, smacheratr::kNumParams, smacheratr::paramTable ());
        else if (type == kFxGentlr && version >= 13) // (Gentlr came to the rack in 13)
            setDefaults (gentlr::kHighOn, gentlr::kTailExt3Base, gentlr::paramTable ());
    }
}

void migrateSlopeInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version)
{
    if (version >= 20)
        return;
    for (int slot = 0; slot < kRackSlots; ++slot)
    {
        const uint32_t typeId = slotParam (slot, kSlotType);
        if (!has[typeId])
            continue;
        const int type = (int)std::lround (toPlain (typeId, norm[typeId]));
        int64_t j = -1;
        if (type == kFxSmacheratr)
            j = fxBlockOf (type, smacheratr::kClaritySlope);
        else if (type == kFxGentlr)
            j = fxBlockOf (type, gentlr::kSlope);
        if (j < 0)
            continue; // (the other effects' own saturators are not used in the rack)
        const uint32_t id = slotBlockParam (slot, (uint32_t)j);
        norm[id] = smacheratr::classicSlopeNorm (); // (the same normalized value in Gentlr's table: Smacheratr's choice)
        has[id] = true;
    }
}

void migrateSlopeChoicesInSlots (std::array<double, kNumParams>& norm, const std::array<bool, kNumParams>& has, int version)
{
    if (version >= 24)
        return;
    for (int slot = 0; slot < kRackSlots; ++slot)
    {
        const uint32_t typeId = slotParam (slot, kSlotType);
        if (!has[typeId])
            continue;
        const int type = (int)std::lround (toPlain (typeId, norm[typeId]));
        const int64_t j = type == kFxSmacheratr ? fxBlockOf (type, smacheratr::kClaritySlope)
                          : type == kFxGentlr   ? fxBlockOf (type, gentlr::kSlope)
                                                : -1;
        if (j < 0)
            continue; // (the other effects' own saturators are not used in the rack)
        // (Gentlr's Slope is Smacheratr's choice: the same normalized values)
        smacheratr::slopeFromThreeChoices (norm, has, slotBlockParam (slot, (uint32_t)j));
    }
}

void keepOldFirstSlotDefaults (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has)
{
    // (only while the first slot holds a Smacheratr: its block positions are Smacheratr's IDs; another
    // effect there keeps its own values)
    const uint32_t typeId = slotParam (0, kSlotType);
    if ((int)std::lround (toPlain (typeId, has[typeId] ? norm[typeId] : defaultNormalized (typeId))) != kFxSmacheratr)
        return;
    for (uint32_t j : {(uint32_t)smacheratr::kClarity, (uint32_t)smacheratr::kClaritySlope})
    {
        const uint32_t id = slotBlockParam (0, j);
        if (has[id])
            continue;
        norm[id] = j == smacheratr::kClaritySlope ? smacheratr::oldDefaultSlopeNorm () : 0.0;
        has[id] = true;
    }
}

void migrateGlueInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version)
{
    if (version >= 21)
        return;
    for (int slot = 0; slot < kRackSlots; ++slot)
    {
        const uint32_t typeId = slotParam (slot, kSlotType);
        if (!has[typeId])
            continue;
        const int type = (int)std::lround (toPlain (typeId, norm[typeId]));
        const uint32_t* ids = type == kFxSmacheratr ? smacheratr::kClarityGlueIds : type == kFxGentlr ? gentlr::kGlueIds : nullptr;
        if (!ids)
            continue; // (the other effects' own saturators are not used in the rack)
        for (int g = 0; g < smacheratr::kGluePairs; ++g)
        {
            const uint32_t id = slotBlockParam (slot, (uint32_t)fxBlockOf (type, ids[g]));
            norm[id] = 0.0; // off (the same normalized value in both tables)
            has[id] = true;
        }
    }
}

void migrateOversamplingInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version)
{
    if (version >= 22)
        return;
    for (int slot = 0; slot < kRackSlots; ++slot)
    {
        const uint32_t typeId = slotParam (slot, kSlotType);
        if (!has[typeId])
            continue;
        const int type = (int)std::lround (toPlain (typeId, norm[typeId]));
        if (type == kFxSmacheratr)
        {
            const uint32_t id = slotBlockParam (slot, (uint32_t)fxBlockOf (type, smacheratr::kOversampling));
            if (has[id])
                norm[id] = smacheratr::oversamplingFromHiQuality (norm[id]);
        }
        else if (type == kFxLevlr)
        {
            const uint32_t id = slotBlockParam (slot, (uint32_t)fxBlockOf (type, levlr::kDriveOversampling));
            norm[id] = levlr::defaultNormalized (levlr::kDriveOversampling);
            has[id] = true;
        }
    }
    smacheratr::tailOversamplingFromHiQuality (norm, has, kTailExtBase); // (the old saturator after the rack)
}

void migrateSubHighInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version)
{
    if (version >= 19)
        return;
    for (int slot = 0; slot < kRackSlots; ++slot)
    {
        const uint32_t typeId = slotParam (slot, kSlotType);
        if (!has[typeId])
            continue;
        const int type = (int)std::lround (toPlain (typeId, norm[typeId]));
        uint32_t subOn, subRange, highOn, highRange;
        if (type == kFxSmacheratr)
        {
            subOn = slotBlockParam (slot, smacheratr::kClaritySub), subRange = slotBlockParam (slot, smacheratr::kClaritySubRange);
            highOn = slotBlockParam (slot, smacheratr::kClarityHigh), highRange = slotBlockParam (slot, smacheratr::kClarityHighRange);
        }
        else if (type == kFxGentlr)
        {
            subOn = slotBlockParam (slot, gentlr::kSubOn), subRange = slotBlockParam (slot, gentlr::kSubRange);
            highOn = slotBlockParam (slot, gentlr::kHighOn), highRange = slotBlockParam (slot, gentlr::kHighRange);
        }
        else
            continue; // (the other effects' own saturators are not used in the rack)
        smacheratr::subHighStateToRange (norm, has, subOn, subRange, highOn, highRange);
        has[subRange] = has[highRange] = true;
    }
}

void migrateMultidynInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version)
{
    if (version >= 16)
        return;
    const auto& md = multidyn::paramTable ();
    for (int slot = 0; slot < kRackSlots; ++slot)
    {
        const uint32_t typeId = slotParam (slot, kSlotType);
        if (!has[typeId] || std::lround (toPlain (typeId, norm[typeId])) != kFxMultidyn)
            continue;
        // before Sub Input (16): the position read 0 (-24 dB), the Sub band was at 0 dB
        const uint32_t subInJ = (uint32_t)fxBlockOf (kFxMultidyn, multidyn::kSubInput);
        norm[slotBlockParam (slot, subInJ)] = md.defaultNormalized (multidyn::kSubInput);
        has[slotBlockParam (slot, subInJ)] = true;
        if (version >= 14)
            continue;
        // before Style (14): Multidyn's own sound, Character (new slots get OTT)
        const uint32_t styleJ = (uint32_t)fxBlockOf (kFxMultidyn, multidyn::kStyle);
        norm[slotBlockParam (slot, styleJ)] = md.toNormalized (multidyn::kStyle, multidyn::kStyleCharacter);
        has[slotBlockParam (slot, styleJ)] = true;
        if (version >= 13)
            continue;
        // by Multidyn's IDs: what the slot has (its later parameters were not there: defaults)
        std::array<double, multidyn::kNumParams> v;
        for (uint32_t id = 0; id < multidyn::kNumParams; ++id)
        {
            const int64_t j = fxBlockOf (kFxMultidyn, id);
            const bool stored = j >= 0 && id < multidyn::kXoverSlope && has[slotBlockParam (slot, (uint32_t)j)];
            v[id] = stored ? norm[slotBlockParam (slot, (uint32_t)j)] : md.defaultNormalized (id);
        }
        v[multidyn::kStyle] = md.toNormalized (multidyn::kStyle, multidyn::kStyleCharacter);
        multidyn::migrateOldBaked (v.data ());
        for (uint32_t id = 0; id < multidyn::kNumParams; ++id)
            if (const int64_t j = fxBlockOf (kFxMultidyn, id); j >= 0)
            {
                norm[slotBlockParam (slot, (uint32_t)j)] = v[id];
                has[slotBlockParam (slot, (uint32_t)j)] = true;
            }
    }
}

void migrateParaInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version)
{
    if (version >= 15)
        return;
    for (int slot = 0; slot < kRackSlots; ++slot)
    {
        const uint32_t typeId = slotParam (slot, kSlotType);
        if (!has[typeId] || std::lround (toPlain (typeId, norm[typeId])) != kFxPara)
            continue;
        auto get = [&] (uint32_t id, double& v) {
            const uint32_t pid = slotBlockParam (slot, (uint32_t)fxBlockOf (kFxPara, id));
            if (!has[pid])
                return false;
            v = norm[pid];
            return true;
        };
        auto set = [&] (uint32_t id, double v) {
            const uint32_t pid = slotBlockParam (slot, (uint32_t)fxBlockOf (kFxPara, id));
            norm[pid] = v;
            has[pid] = true;
        };
        // before 13: three slopes, one drive; before 15: one slope, no gain locks, Fade up to 36 semitones
        if (version < 13)
            para::upgradeToPerBandDrive (get, set);
        para::upgradeToSeparateSlopes (get, set);
    }
}

void migrateLevlrInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version)
{
    if (version >= 13)
        return;
    for (int slot = 0; slot < kRackSlots; ++slot)
    {
        const uint32_t typeId = slotParam (slot, kSlotType);
        if (!has[typeId] || std::lround (toPlain (typeId, norm[typeId])) != kFxLevlr)
            continue;
        for (uint32_t id = levlr::kFirstAddedAfter060; id < levlr::kEndAddedAfter060; ++id)
        {
            norm[slotBlockParam (slot, id)] = levlr::defaultNormalized (id);
            has[slotBlockParam (slot, id)] = true;
        }
    }
}

Rack::Rack ()
{
    for (auto& s : slots)
        s = std::make_unique<FxSlot> ();
}

void Rack::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    for (auto& s : slots)
        s->prepare (sr, maxBlock);
}

void Rack::reset ()
{
    for (auto& s : slots)
        s->reset ();
}

void Rack::setMeters (RackMeters* m)
{
    meters = m;
    for (int i = 0; i < kRackSlots; ++i)
    {
        FxSlot& s = *slots[(size_t)i];
        s.para.setMeters (m ? &m->para[(size_t)i] : nullptr);
        s.sat.setMeters (m ? &m->sat[(size_t)i] : nullptr);
        s.widr.setMeters (m ? &m->widr[(size_t)i] : nullptr);
        s.wubr.setMeters (m ? &m->wubr[(size_t)i] : nullptr);
        s.levlr.setMeters (m ? &m->levlr[(size_t)i] : nullptr);
        s.smoothr.setMeters (m ? &m->smoothr[(size_t)i] : nullptr);
        s.gentlr.setMeters (m ? &m->gentlr[(size_t)i] : nullptr);
    }
}

void Rack::setParam (uint32_t id, double plain)
{
    if (!isRackParam (id))
        return;
    const RackField rf = rackField (id);
    FxSlot& s = *slots[(size_t)rf.slot];
    const uint32_t field = rf.field;
    if (field == kSlotType)
        s.setType ((int)std::lround (plain));
    else if (field == kSlotOn)
        s.setOn (plain >= 0.5);
    else
        s.setValue (field - kSlotParams, plain);
}

int Rack::latency () const
{
    int l = 0;
    for (const auto& s : slots)
        l += s->latency ();
    return l;
}

void Rack::noteOn (int note)
{
    for (auto& s : slots)
        s->noteOn (note);
}

void Rack::noteOff (int note)
{
    for (auto& s : slots)
        s->noteOff (note);
}

void Rack::allNotesOff ()
{
    for (auto& s : slots)
        s->allNotesOff ();
}

void Rack::setTransport (double bpm, double ppq, bool playing)
{
    for (auto& s : slots)
        s->setTransport (bpm, ppq, playing);
}

void Rack::setPitchBend (float bipolar)
{
    for (auto& s : slots)
        s->setPitchBend (bipolar);
}

void Rack::process (float* L, float* R, int n)
{
    for (int i = 0; i < kRackSlots; ++i)
    {
        slots[(size_t)i]->process (L, R, n);
        publish (i);
    }
}

void Rack::publish (int i)
{
    if (!meters)
        return;
    FxSlot& s = *slots[(size_t)i];
    if (s.type == kFxMultidyn)
        for (int b = 0; b <= multidyn::kSubBand; ++b) // the bands and the Sub band
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
