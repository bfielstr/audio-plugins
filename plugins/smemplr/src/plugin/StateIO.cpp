#include "StateIO.h"

#include "Rack.h"

#include <functional>

#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace smemplr {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x534d5052; // 'SMPR'
// 2: Multidyn's RMS Window and Soften in its rack slots (they read 0 in a version 1 state)
// 3: Smacheratr's Clarity Frequency / Width and Para's Dip Start / Low-Pass Floor in the rack; Para's
//    Liquid is its Vocal movement
// 4: Smacheratr's Clarity Range in the rack
// 5: Clarity Frequency 20 Hz - 20 kHz (20 - 500 Hz before), in the rack and the end saturator
// 6: Clarity's second band in the rack
// 7: one Clarity button (a band works while its Range is above 0), in the rack and the end saturator
// 8: Wubr in the rack: the slot type's choice has one more entry
// 9: no saturator after the rack (a new Smemplr has a Smacheratr slot instead; an old project's goes
//    into the rack), and the M/S EQ's side high-pass has ten slopes (6 / 12 / 24 dB before)
// 10: Levlr in the rack: the slot type's choice has one more entry
// 11: Gently's (Clarity's) Advanced mode in the rack's Smacheratrs
// 12: Gently's Sub band in the rack's Smacheratrs
// 13: Gently and Smoothr in the rack: the slot type's choice has two more entries; Levlr's Bands and
//     band drives in its slots; Para's slopes (6 .. 96 dB, Brickwall) and a drive per filter in its slots;
//     Multidyn's OTT gain staging, crossover slope, Soften Color and Sub band in its slots
// 14: Multidyn's Style in its slots (OTT for new ones; older slots keep Character, their sound)
// 15: Para's Low-Pass Slope (its slots had one slope for both filters: the low-pass gets it) and Gain Locks
//     (the high-pass's on unless its gain is above 0 dB, the low-pass's off) in its slots, and its Fade's
//     range 1 .. 60 semitones (1 .. 36 before: a slot's Fade keeps its semitones)
// 16: Multidyn's Sub Input in its slots (0 dB: older slots read 0 there, -24 dB)
// 17: Gently's High band and No Overlap in the rack's Smacheratrs and Gentlys (off, their defaults: the
//     places held nothing that was used)
// 18: the modulation LFOs' mappings after the loop fade flag (Modulation.h: encodeModMap, behind its
//     size in bytes); an older state has none
// 19: no Sub and High buttons in the rack's Smacheratrs and Gentlys (a band works while its Range is above
//     0 dB; one that was off gets Range 0)
constexpr int32 kVersion = 19;
constexpr int32 kModsSince = 18;

bool writeDoubles (IBStreamer& s, const std::vector<double>& v)
{
    if (!s.writeInt32 ((int32)v.size ()))
        return false;
    for (double d : v)
        if (!s.writeDouble (d))
            return false;
    return true;
}

bool readDoubles (IBStreamer& s, std::vector<double>& v)
{
    int32 n = 0;
    if (!s.readInt32 (n) || n < 0 || n > 100000)
        return false;
    v.resize ((size_t)n);
    for (auto& d : v)
        if (!s.readDouble (d))
            return false;
    return true;
}
} // namespace

bool writeState (IBStream* stream, const PluginState& st)
{
    IBStreamer s (stream, kLittleEndian);
    int32 present = 0;
    for (uint32 id = 0; id < kNumParams; ++id)
        present += st.has[id] ? 1 : 0;
    bool ok = s.writeInt32 (kMagic) && s.writeInt32 (kVersion) && s.writeInt32 (present);
    for (uint32 id = 0; ok && id < kNumParams; ++id)
        if (st.has[id])
            ok = s.writeInt32u (id) && s.writeDouble (st.norm[id]);
    ok = ok && s.writeInt32 ((int32)st.samplePath.size ());
    ok = ok && (st.samplePath.empty () || s.writeRaw (st.samplePath.data (), (int32)st.samplePath.size ()) ==
                                              (int32)st.samplePath.size ());
    ok = ok && s.writeDouble (st.ops.cropStart) && s.writeDouble (st.ops.cropEnd) && s.writeBool (st.ops.reverse) &&
         s.writeBool (st.ops.normalize);
    ok = ok && writeDoubles (s, st.edits.manual) && writeDoubles (s, st.edits.suppressed);
    ok = ok && s.writeBool (st.constantPowerFade);
    const std::vector<uint8_t> mods = encodeModMap (st.mods);
    ok = ok && s.writeInt32 ((int32)mods.size ()) && s.writeRaw (mods.data (), (int32)mods.size ()) == (int32)mods.size ();
    return ok;
}

bool readState (IBStream* stream, PluginState& st)
{
    IBStreamer s (stream, kLittleEndian);
    int32 magic = 0, version = 0, count = 0;
    if (!s.readInt32 (magic) || magic != kMagic || !s.readInt32 (version) || version < 1 || !s.readInt32 (count))
        return false;
    if (count < 0 || count > 100000)
        return false;
    st.has.fill (false);
    for (int32 i = 0; i < count; ++i)
    {
        uint32 id = 0;
        double v = 0.0;
        if (!s.readInt32u (id) || !s.readDouble (v))
            return false;
        if (id < kNumParams) // unknown IDs from newer versions are skipped
        {
            st.norm[id] = std::clamp (v, 0.0, 1.0);
            st.has[id] = true;
        }
    }
    int32 len = 0;
    if (!s.readInt32 (len) || len < 0 || len > 65536)
        return false;
    st.samplePath.assign ((size_t)len, '\0');
    if (len > 0 && s.readRaw (st.samplePath.data (), len) != len)
        return false;
    if (!s.readDouble (st.ops.cropStart) || !s.readDouble (st.ops.cropEnd) || !s.readBool (st.ops.reverse) ||
        !s.readBool (st.ops.normalize))
        return false;
    if (!readDoubles (s, st.edits.manual) || !readDoubles (s, st.edits.suppressed))
        return false;
    bool cp = true;
    if (s.readBool (cp))
        st.constantPowerFade = cp;
    // the modulation's mappings (none before version 18; a state cut short there keeps the rest)
    st.mods.list.clear ();
    int32 modBytes = 0;
    if (version >= kModsSince && s.readInt32 (modBytes) && modBytes > 0 && modBytes <= (1 << 20))
    {
        std::vector<uint8_t> raw ((size_t)modBytes);
        if (s.readRaw (raw.data (), modBytes) == modBytes)
            decodeModMap (raw.data (), raw.size (), st.mods);
        else
            st.mods.list.clear ();
    }
    // 0.1.x stored the loop fade type outside the parameters.
    if (!st.has[kLoopFadePower])
    {
        st.norm[kLoopFadePower] = st.constantPowerFade ? 1.0 : 0.0;
        st.has[kLoopFadePower] = true;
    }
    // the slot type was stored over the kinds before Wubr: the same kind on the longer list
    if (version < 8)
        for (int slot = 0; slot < kRackSlots; ++slot)
        {
            const uint32_t typeId = slotParam (slot, kSlotType);
            if (st.has[typeId])
                st.norm[typeId] = toNormalized (typeId, std::round (st.norm[typeId] * (kFxTypesBeforeWubr - 1)));
        }
    // 8 and 9 stored it over the kinds before Levlr
    else if (version < 10)
        for (int slot = 0; slot < kRackSlots; ++slot)
        {
            const uint32_t typeId = slotParam (slot, kSlotType);
            if (st.has[typeId])
                st.norm[typeId] = toNormalized (typeId, std::round (st.norm[typeId] * (kFxTypesBeforeLevlr - 1)));
        }
    // 10 .. 12 over the kinds before Gently and Smoothr
    else if (version < 13)
        for (int slot = 0; slot < kRackSlots; ++slot)
        {
            const uint32_t typeId = slotParam (slot, kSlotType);
            if (st.has[typeId])
                st.norm[typeId] = toNormalized (typeId, std::round (st.norm[typeId] * (kFxTypesBeforeGently - 1)));
        }
    // the M/S EQ's slope was stored over its three choices (6, 12, 24 dB): the same slope on the longer
    // list (before the old fixed M/S EQ moves into the rack, which converts its own)
    if (version < 9)
        for (int slot = 0; slot < kRackSlots; ++slot)
        {
            const uint32_t typeId = slotParam (slot, kSlotType), slopeId = slotBlockParam (slot, mseq::kSlope);
            if (st.has[typeId] && st.has[slopeId] && std::lround (toPlain (typeId, st.norm[typeId])) == kFxMsEq)
                st.norm[slopeId] = mseq::slopeFromThreeChoices (st.norm[slopeId]);
        }
    migrateToRack (st);
    if (version < 2)
        for (int slot = 0; slot < kRackSlots; ++slot)
        {
            const uint32_t typeId = slotParam (slot, kSlotType);
            if (!st.has[typeId] || std::lround (toPlain (typeId, st.norm[typeId])) != kFxMultidyn)
                continue;
            const auto& t = fxBlockTable (kFxMultidyn);
            for (uint32_t id : {multidyn::kRmsWindow, multidyn::kSoften})
            {
                const auto j = (uint32_t)fxBlockOf (kFxMultidyn, id);
                st.norm[slotBlockParam (slot, j)] = t.defaultNormalized (j);
                st.has[slotBlockParam (slot, j)] = true;
            }
        }
    if (version < 7)
    {
        // one Clarity button: states from before, made to mean the same (a slot or block that never
        // had band 2 reads it as off: its Range becomes 0)
        auto convert = [&] (uint32_t i1, uint32_t r1i, uint32_t i2, uint32_t r2i, double r1Default) {
            double on1 = st.has[i1] ? st.norm[i1] : 0.0, on2 = st.has[i2] && version >= 6 ? st.norm[i2] : 0.0;
            double r1 = st.has[r1i] && st.norm[r1i] > 0.0 ? st.norm[r1i] : r1Default, r2 = st.norm[r2i];
            smacheratr::clarityToOneButton (on1, r1, on2, r2);
            st.norm[i1] = on1;
            st.norm[r1i] = r1;
            st.norm[r2i] = r2;
            st.has[i1] = st.has[r1i] = st.has[r2i] = true;
        };
        const double r1Default = smacheratr::defaultNormalized (smacheratr::kClarityRange);
        convert (kTailExtBase + pk::kTailExtClarity, kTailExtBase + pk::kTailExtClarityRange, kTailExtBase + pk::kTailExtClarity2,
                 kTailExtBase + pk::kTailExtClarity2Range, r1Default);
        for (int slot = 0; slot < kRackSlots; ++slot)
        {
            const uint32_t typeId = slotParam (slot, kSlotType);
            if (st.has[typeId] && std::lround (toPlain (typeId, st.norm[typeId])) == kFxSmacheratr)
                convert (slotBlockParam (slot, smacheratr::kClarity), slotBlockParam (slot, smacheratr::kClarityRange),
                         slotBlockParam (slot, smacheratr::kClarity2), slotBlockParam (slot, smacheratr::kClarity2Range), r1Default);
        }
    }
    if (version < 5)
    {
        // Clarity Frequency's range grew: values saved before, in the new range (the rack's Smacheratrs
        // below; a slot migrated just now gets the default, already in the new range)
        const uint32_t endId = kTailExtBase + pk::kTailExtClarityFreq;
        if (st.has[endId])
            st.norm[endId] = smacheratr::clarityFreqFromNarrowRange (st.norm[endId]);
        for (int slot = 0; slot < kRackSlots; ++slot)
        {
            const uint32_t typeId = slotParam (slot, kSlotType);
            const uint32_t fId = slotBlockParam (slot, smacheratr::kClarityFreq);
            if (st.has[typeId] && std::lround (toPlain (typeId, st.norm[typeId])) == kFxSmacheratr)
                st.norm[fId] = smacheratr::clarityFreqFromNarrowRange (st.norm[fId]);
        }
    }
    if (version < 6)
    {
        // parameters that came after the state's version read 0 in the slot: they get their defaults
        struct Added
        {
            int since, type;
            uint32_t id;
        };
        const Added added[] = {{3, kFxSmacheratr, smacheratr::kClarityFreq}, {3, kFxSmacheratr, smacheratr::kClarityWidth},
                               {3, kFxPara, para::kDipStart},             {3, kFxPara, para::kLpFloor},
                               {4, kFxSmacheratr, smacheratr::kClarityRange},
                               {6, kFxSmacheratr, smacheratr::kClarity2Freq},    {6, kFxSmacheratr, smacheratr::kClarity2Width},
                               {6, kFxSmacheratr, smacheratr::kClarity2Range}};
        for (int slot = 0; slot < kRackSlots; ++slot)
        {
            const uint32_t typeId = slotParam (slot, kSlotType);
            if (!st.has[typeId])
                continue;
            const int type = (int)std::lround (toPlain (typeId, st.norm[typeId]));
            for (const Added& a : added)
                if (a.type == type && version < a.since)
                {
                    const auto j = (uint32_t)fxBlockOf (type, a.id);
                    if (st.norm[slotBlockParam (slot, j)] == 0.0)
                        st.norm[slotBlockParam (slot, j)] = fxBlockTable (type).defaultNormalized (j);
                    st.has[slotBlockParam (slot, j)] = true;
                }
            if (version < 3 && type == kFxPara && st.norm[slotBlockParam (slot, para::kLiquid)] >= 0.5)
                st.norm[slotBlockParam (slot, para::kMovement)] = para::toNormalized (para::kMovement, para::kVocal);
        }
    }
    // Gently's Advanced mode (11), Sub band (12), High band and No Overlap (17) in the rack's Smacheratrs, and the
    // last two in its Gentlys: defaults (off: the same sound)
    migrateGentlyInSlots (st.norm, st.has, version);
    // the Sub and High bands without buttons (19): one that was off gets Range 0 (after the defaults above)
    migrateSubHighInSlots (st.norm, st.has, version);
    // Levlr's Bands and drives (13)
    migrateLevlrInSlots (st.norm, st.has, version);
    // Para's slopes and per-filter drives (13), its low-pass slope, gain locks and Fade range (15)
    migrateParaInSlots (st.norm, st.has, version);
    // Multidyn's OTT gain staging, slope and Sub band (13), Style (14), Sub Input (16)
    migrateMultidynInSlots (st.norm, st.has, version);
    if (version < 9)
        moveEndSaturatorIntoRack (st.norm, st.has); // (the rack is what the state has; its saturator after it, into it)
    return true;
}

// 0.5 had a fixed Para -> Multidyn -> M/S EQ after the sampler; 0.6 has the rack. An old state (no
// rack in it) gets its effects that were on, in that order, in the first slots.
void migrateToRack (PluginState& st)
{
    if (st.has[slotParam (0, kSlotType)])
        return;
    int slot = 0;
    auto legacyOn = [&] (uint32_t id) { return st.has[id] && st.norm[id] >= 0.5; };
    auto put = [&] (int type, const pk::ParamTable& t, const std::function<double (uint32_t)>& value) {
        auto set = [&] (uint32_t id, double v) {
            st.norm[id] = v;
            st.has[id] = true;
        };
        set (slotParam (slot, kSlotType), toNormalized (slotParam (slot, kSlotType), (double)type));
        set (slotParam (slot, kSlotOn), 1.0);
        for (uint32_t j = 0; j < t.size (); ++j)
            set (slotBlockParam (slot, j), value (j));
        ++slot;
    };
    auto old = [&] (uint32_t id, const pk::ParamTable& t, uint32_t j) { return st.has[id] ? st.norm[id] : t.defaultNormalized (j); };
    if (legacyOn (kFxParaOn))
    {
        const auto& t = para::paramTable ();
        put (kFxPara, t, [&] (uint32_t j) {
            if (j == para::kMovement && st.has[kParaLiquid] && st.norm[kParaLiquid] >= 0.5)
                return para::toNormalized (para::kMovement, para::kVocal); // Liquid is Vocal movement now
            if (j < para::kHostedParams)
                return old (paraParam (j), t, j);
            switch (j)
            {
                case para::kDragGain: return old (kParaDragGain, t, j);
                case para::kLiquid: return old (kParaLiquid, t, j);
                // (over Fade's old range, as the slot's own then: migrateParaInSlots converts it; never
                // saved, the old default)
                case para::kFade: return st.has[kParaFade] ? st.norm[kParaFade] : defaultNormalized (kParaFade);
                case para::kNotch: return old (kParaNotch, t, j);
                default: return t.defaultNormalized (j);
            }
        });
        st.norm[kFxParaOn] = 0.0;
    }
    if (legacyOn (kFxMdOn))
    {
        const auto& t = fxBlockTable (kFxMultidyn);
        put (kFxMultidyn, t, [&] (uint32_t j) {
            const int64_t id = fxIdAt (kFxMultidyn, j);
            return id >= 0 && id < (int64_t)kLegacyMdParams ? old (multidynParam ((uint32_t)id), t, j) : t.defaultNormalized (j);
        });
        st.norm[kFxMdOn] = 0.0;
    }
    if (legacyOn (kMsOn))
    {
        const auto& t = mseq::paramTable ();
        const uint32_t ids[mseq::kNumParams] = {kMsSideHp, kMsSlope, kMsSideGain, kMsMidGain};
        put (kFxMsEq, t, [&] (uint32_t j) {
            // (the old slope has its three choices still)
            return j == mseq::kSlope && st.has[kMsSlope] ? mseq::slopeFromThreeChoices (st.norm[kMsSlope]) : old (ids[j], t, j);
        });
        st.norm[kMsOn] = 0.0;
    }
}

} // namespace smemplr
