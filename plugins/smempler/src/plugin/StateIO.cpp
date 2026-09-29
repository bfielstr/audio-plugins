#include "StateIO.h"

#include "Rack.h"

#include <functional>

#include "base/source/fstreamer.h"

#include <algorithm>

namespace smempler {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x534d5052; // 'SMPR'
// 2: Multidyn's RMS Window and Soften in its rack slots (they read 0 in a version 1 state)
// 3: Smacheratr's Clarity Frequency / Width and Para's Dip Start / Low-Pass Floor in the rack; Para's
//    Liquid is its Vocal movement
constexpr int32 kVersion = 3;

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
    // 0.1.x stored the loop fade type outside the parameters.
    if (!st.has[kLoopFadePower])
    {
        st.norm[kLoopFadePower] = st.constantPowerFade ? 1.0 : 0.0;
        st.has[kLoopFadePower] = true;
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
    if (version < 3)
    {
        // parameters that came after version 2 read 0 in the slot: they get their defaults
        struct Added
        {
            int type;
            uint32_t id;
        };
        const Added added[] = {{kFxSmacheratr, smacheratr::kClarityFreq}, {kFxSmacheratr, smacheratr::kClarityWidth},
                               {kFxPara, para::kDipStart},             {kFxPara, para::kLpFloor}};
        for (int slot = 0; slot < kRackSlots; ++slot)
        {
            const uint32_t typeId = slotParam (slot, kSlotType);
            if (!st.has[typeId])
                continue;
            const int type = (int)std::lround (toPlain (typeId, st.norm[typeId]));
            for (const Added& a : added)
                if (a.type == type)
                {
                    const auto j = (uint32_t)fxBlockOf (type, a.id);
                    if (st.norm[slotBlockParam (slot, j)] == 0.0)
                        st.norm[slotBlockParam (slot, j)] = fxBlockTable (type).defaultNormalized (j);
                    st.has[slotBlockParam (slot, j)] = true;
                }
            if (type == kFxPara && st.norm[slotBlockParam (slot, para::kLiquid)] >= 0.5)
                st.norm[slotBlockParam (slot, para::kMovement)] = para::toNormalized (para::kMovement, para::kVocal);
        }
    }
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
                case para::kFade: return old (kParaFade, t, j);
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
        put (kFxMsEq, t, [&] (uint32_t j) { return old (ids[j], t, j); });
        st.norm[kMsOn] = 0.0;
    }
}

} // namespace smempler
